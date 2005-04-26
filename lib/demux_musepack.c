/*****************************************************************
 
  demux_musepack.c
 
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

#include <avdec_private.h>

#include <musepack/musepack.h>

/*
 *  Musepack "demuxer": It's somewhat weird because libmusepack doesn't
 *  allow split the format parsing / decoding process.
 *
 *  For this reason, we we do all the decoding here and fake a PCM stream
 *  format such that the real "decoder" will just buffer/copy the samples
 *  to the output
 * 
 *  With this in approach, we'll have complete support for all libmusepack
 *  features (incl. seeking) without too much code duplication.
 */


typedef struct
  {
  bgav_input_context_t * ctx;

  /* We shorten the file at the start and end to hide eventual
     tags from libmusepack */
    
  int start_bytes;
  int end_bytes;
  
  } read_struct;

typedef struct
  {
  mpc_reader     reader;
  mpc_streaminfo si;
  mpc_decoder    dec;
  read_struct    rs;
  } mpc_priv_t;

static int probe_mpc(bgav_input_context_t * input)
  {
  uint8_t test_data[3];
  if(bgav_input_get_data(input, test_data, 3) < 3)
    return 0;

  if((test_data[0] == 'M') &&
     (test_data[1] == 'P') &&
     (test_data[2] == '+'))
    return 1;
  return 0;
  }

static mpc_int32_t mpc_read(void *t, void *ptr, mpc_int32_t size)
  {
  read_struct * r = (read_struct*)t;
  return bgav_input_read_data(r->ctx, ptr, size);
  }

static BOOL mpc_seek(void *t, mpc_int32_t offset)
  {
  read_struct * r = (read_struct*)t;
  bgav_input_seek(r->ctx, offset + r->start_bytes, SEEK_SET);
  return TRUE;
  }

static mpc_int32_t mpc_tell(void *t)
  {
  read_struct * r = (read_struct*)t;
  return r->ctx->position - r->start_bytes;
  }

static mpc_int32_t mpc_get_size(void *t)
  {
  read_struct * r = (read_struct*)t;
  return r->ctx->total_bytes - r->start_bytes - r->end_bytes;
  }

static BOOL mpc_canseek(void *t)
  {
  read_struct * r = (read_struct*)t;
  return r->ctx->input->seek_byte ? TRUE : FALSE;
  }

static int open_mpc(bgav_demuxer_context_t * ctx,
                    bgav_redirector_context_t ** redir)
  {
  int ape_tag_size;
  bgav_metadata_t start_metadata, end_metadata;

  bgav_id3v1_tag_t * id3v1  = (bgav_id3v1_tag_t*)0;
  bgav_ape_tag_t   * apetag = (bgav_ape_tag_t*)0;
    
  bgav_stream_t * s;
  mpc_priv_t * priv;

  priv = calloc(1, sizeof(*priv));
  ctx->priv = priv;
  
  /* Setup reader */

  priv->rs.ctx = ctx->input;
  
  priv->reader.seek     = mpc_seek;
  priv->reader.read     = mpc_read;
  priv->reader.tell     = mpc_tell;
  priv->reader.get_size = mpc_get_size;
  priv->reader.canseek  = mpc_canseek;

  priv->reader.data     = &(priv->rs);

  /* Set up track table */
  
  ctx->tt = bgav_track_table_create(1);
  
  /* Check for tags */

  priv->rs.start_bytes = ctx->input->position;

  if(ctx->input->input->seek_byte && ctx->input->total_bytes)
    {
    /* Check for id3v1 */

    bgav_input_seek(ctx->input, -128, SEEK_END);

    if(bgav_id3v1_probe(ctx->input))
      {
      id3v1 = bgav_id3v1_read(ctx->input);
      priv->rs.end_bytes = 128;
      }
    else
      {
      bgav_input_seek(ctx->input, -32, SEEK_END);
      if(bgav_ape_tag_probe(ctx->input, &ape_tag_size))
        {
        bgav_input_seek(ctx->input, -ape_tag_size, SEEK_END);
        apetag = bgav_ape_tag_read(ctx->input, ape_tag_size);
        priv->rs.end_bytes = ape_tag_size;
        }
      }
    bgav_input_seek(ctx->input, priv->rs.start_bytes, SEEK_SET);
    }

  /* Setup metadata */

  if(ctx->input->id3v2 && ((id3v1) || (apetag)))
    {
    memset(&start_metadata, 0, sizeof(start_metadata));
    memset(&end_metadata, 0, sizeof(end_metadata));

    bgav_id3v2_2_metadata(ctx->input->id3v2, &start_metadata);

    if(id3v1)
      bgav_id3v1_2_metadata(id3v1, &end_metadata);
    else if(apetag)
      bgav_ape_tag_2_metadata(apetag, &end_metadata);
    
    bgav_metadata_merge(&(ctx->tt->current_track->metadata),
                        &start_metadata, &end_metadata);
    bgav_metadata_free(&start_metadata);
    bgav_metadata_free(&end_metadata);
    }
  
  else if(ctx->input->id3v2)
    bgav_id3v2_2_metadata(ctx->input->id3v2,
                          &(ctx->tt->current_track->metadata));
  else if(id3v1)
    bgav_id3v1_2_metadata(id3v1,
                          &(ctx->tt->current_track->metadata));
  else if(apetag)
    bgav_ape_tag_2_metadata(apetag,
                            &(ctx->tt->current_track->metadata));
  
  if(id3v1)
    bgav_id3v1_destroy(id3v1);
  if(apetag)
    bgav_ape_tag_destroy(apetag);
  
  /* Get stream info */
  
  mpc_streaminfo_init(&(priv->si));
  
  if(mpc_streaminfo_read(&(priv->si), &(priv->reader)) != ERROR_CODE_OK)
    return 0;
  
  /* Fire up decoder and set up stream */
  
  mpc_decoder_setup(&(priv->dec), &(priv->reader));
  if(!mpc_decoder_initialize(&(priv->dec), &(priv->si)))
    return 0;
  
  s = bgav_track_add_audio_stream(ctx->tt->current_track);
  
  s->data.audio.format.samplerate   = priv->si.sample_freq;
  s->data.audio.format.num_channels = priv->si.channels;

  s->fourcc = BGAV_MK_FOURCC('m', 'p', 'c', ' '); /* Native float format */

  s->timescale = priv->si.sample_freq;
  
  ctx->tt->current_track->duration =
    gavl_samples_to_time(s->data.audio.format.samplerate,
                         mpc_streaminfo_get_length_samples(&(priv->si)));

  ctx->stream_description = bgav_sprintf("Musepack Format");

  if(ctx->input->input->seek_byte)
    ctx->can_seek = 1;

  return 1;
  }

static int next_packet_mpc(bgav_demuxer_context_t * ctx)
  {
  unsigned int result;
  bgav_stream_t * s;
  bgav_packet_t * p;
  mpc_priv_t * priv;
  priv = (mpc_priv_t *)(ctx->priv);

  s = &(ctx->tt->current_track->audio_streams[0]);

  p = bgav_packet_buffer_get_packet_write(s->packet_buffer, s);

  bgav_packet_alloc(p, MPC_DECODER_BUFFER_LENGTH * sizeof(float));
  
  result = mpc_decoder_decode(&(priv->dec), (float*)(p->data), 0, 0);

  if(!result || (result == (unsigned int)(-1)))
    return 0;

  p->data_size = result * s->data.audio.format.num_channels * sizeof(float);

  bgav_packet_done_write(p);
  
  return 1;
  }

static void seek_mpc(bgav_demuxer_context_t * ctx, gavl_time_t time)
  {
  int64_t sample;
  bgav_stream_t * s;
  
  mpc_priv_t * priv;
  priv = (mpc_priv_t *)(ctx->priv);

  s = &(ctx->tt->current_track->audio_streams[0]);
  
  sample = gavl_time_to_samples(s->data.audio.format.samplerate,
                                time);

  mpc_decoder_seek_sample(&(priv->dec), sample);
  s->time_scaled = sample;
  }

static void close_mpc(bgav_demuxer_context_t * ctx)
  {
  mpc_priv_t * priv;
  priv = (mpc_priv_t *)(ctx->priv);
  free(priv);
  }

bgav_demuxer_t bgav_demuxer_mpc =
  {
    probe:       probe_mpc,
    open:        open_mpc,
    next_packet: next_packet_mpc,
    seek:        seek_mpc,
    close:       close_mpc
  };
