/*****************************************************************

  demux_dv.c

  Copyright (c) 2005 by Burkhard Plaum - plaum@ipf.uni-stuttgart.de

  http://gmerlin.sourceforge.net

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.

*****************************************************************/

#include <string.h>
#include <stdlib.h>

#include <avdec_private.h>
#include <dvframe.h>


/* DV demuxer */

typedef struct
  {
  bgav_dv_dec_t * d;
  int have_frame;
  int64_t frame_pos;
  int frame_size;
  uint8_t * frame_buffer;
  } dv_priv_t;

static int probe_dv(bgav_input_context_t * input)
  {
  char * pos;
  /* There seems to be no way to do proper probing of the stream.
     Therefore, we accept only local files with .dv as extension */

  if(input->filename)
    {
    pos = strrchr(input->filename, '.');
    if(!pos)
      return 0;
    if(!strcmp(pos, ".dv"))
      return 1;
    }
  return 0;
  }

static int open_dv(bgav_demuxer_context_t * ctx,
                    bgav_redirector_context_t ** redir)
  {
  int64_t total_frames;
  uint8_t header[DV_HEADER_SIZE];
  bgav_stream_t * as, * vs;
  dv_priv_t * priv;
  
  priv = calloc(1, sizeof(*priv));
  ctx->priv = priv;

  priv->d = bgav_dv_dec_create();
  
  /* Read frame */
  if(bgav_input_get_data(ctx->input, header, DV_HEADER_SIZE) < DV_HEADER_SIZE)
    return 0;
  bgav_dv_dec_set_header(priv->d, header);
  priv->frame_size = bgav_dv_dec_get_frame_size(priv->d);
  priv->frame_buffer = malloc(priv->frame_size);

  if(bgav_input_read_data(ctx->input, priv->frame_buffer, 
                          priv->frame_size) < priv->frame_size)
    return 0;
  bgav_dv_dec_set_frame(priv->d, priv->frame_buffer);
  
  /* Create track */
  
  ctx->tt = bgav_track_table_create(1);
  
  /* Set up streams */
  as = bgav_track_add_audio_stream(ctx->tt->current_track);
  bgav_dv_dec_init_audio(priv->d, as);

  vs = bgav_track_add_video_stream(ctx->tt->current_track);
  bgav_dv_dec_init_video(priv->d, vs);

  /* Set duration */

  if(ctx->input->total_bytes)
    {
    total_frames = ctx->input->total_bytes / priv->frame_size;
    ctx->tt->current_track->duration =
      gavl_frames_to_time(vs->data.video.format.timescale, vs->data.video.format.frame_duration,
                          total_frames);
    }
  
  if(ctx->input->input->seek_byte)
    ctx->can_seek = 1;
  
  ctx->stream_description = bgav_sprintf("DV Format");
  
  return 1;
  
  }

static int next_packet_dv(bgav_demuxer_context_t * ctx)
  {
  bgav_packet_t *ap, *vp;
  bgav_stream_t *as, *vs;
  dv_priv_t * priv;
  priv = (dv_priv_t *)(ctx->priv);

  /*
   *  demuxing dv is easy: we copy the video frame and
   *  extract the audio data
   */
  
  as = ctx->tt->current_track->audio_streams;
  vs = ctx->tt->current_track->video_streams;
  
  ap = bgav_packet_buffer_get_packet_write(as->packet_buffer, as);
  vp = bgav_packet_buffer_get_packet_write(vs->packet_buffer, vs);
 
  bgav_packet_alloc(vp, priv->frame_size);
    
  vp->data_size = bgav_input_read_data(ctx->input, vp->data, priv->frame_size);
  if(vp->data_size < priv->frame_size)
    return 0;

  bgav_dv_dec_set_frame(priv->d, vp->data);
  
  if(!bgav_dv_dec_get_audio_packet(priv->d, ap))
    return 0;

  bgav_dv_dec_get_video_packet(priv->d, vp);
  
  ap->keyframe = 1;
  vp->keyframe = 1;
  
  bgav_packet_done_write(ap);
  bgav_packet_done_write(vp);
  //  fprintf(stderr, "done\n");
  
  return 1;
  }

static void seek_dv(bgav_demuxer_context_t * ctx, gavl_time_t time)
  {
  int64_t file_position;
  dv_priv_t * priv;
  priv = (dv_priv_t *)(ctx->priv);
  bgav_stream_t * as, * vs;
  int64_t frame_pos;

  //  fprintf(stderr, "Seek time: %lld\n", time);
  
  vs = ctx->tt->current_track->video_streams;
  as = ctx->tt->current_track->audio_streams;
  
  frame_pos = gavl_time_to_frames(vs->data.video.format.timescale,
                                  vs->data.video.format.frame_duration,
                                  time);
  
  file_position = frame_pos * priv->frame_size;

  //  fprintf(stderr, "**** File position: %lld /%lld\n", file_position, ctx->input->total_bytes);
  
  bgav_dv_dec_set_frame_counter(priv->d, frame_pos);
  bgav_input_seek(ctx->input, file_position, SEEK_SET);

  vs->time_scaled = frame_pos * vs->data.video.format.frame_duration;
  as->time_scaled = (vs->time_scaled * as->data.audio.format.samplerate) / vs->data.video.format.timescale;

  //  fprintf(stderr, "Audio time: %lld\n", gavl_time_unscale(as->data.audio.format.samplerate, as->time_scaled));
  //  fprintf(stderr, "Video time: %lld\n", gavl_time_unscale(vs->data.video.format.timescale, vs->time_scaled));
  
  }

static void close_dv(bgav_demuxer_context_t * ctx)
  {
  dv_priv_t * priv;
  priv = (dv_priv_t *)(ctx->priv);
  if(priv->frame_buffer)
    free(priv->frame_buffer);
  if(priv->d)
    bgav_dv_dec_destroy(priv->d);
  
  free(priv);
  }

bgav_demuxer_t bgav_demuxer_dv =
  {
    probe:       probe_dv,
    open:        open_dv,
    next_packet: next_packet_dv,
    seek:        seek_dv,
    close:       close_dv
  };

