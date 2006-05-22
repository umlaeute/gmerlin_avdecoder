/*****************************************************************
 
  subtitle.c
 
  Copyright (c) 2006 by Burkhard Plaum - plaum@ipf.uni-stuttgart.de
 
  http://gmerlin.sourceforge.net
 
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 
*****************************************************************/

#include <stdio.h>
#include <stdlib.h>

#include <avdec_private.h>

int bgav_num_subtitle_streams(bgav_t * bgav, int track)
  {
  return bgav->tt->tracks[track].num_subtitle_streams;
  }

int bgav_set_subtitle_stream(bgav_t * b, int stream, bgav_stream_action_t action)
  {
  if((stream >= b->tt->current_track->num_subtitle_streams) || (stream < 0))
    return 0;
  b->tt->current_track->subtitle_streams[stream].action = action;
  return 1;
  }

const gavl_video_format_t * bgav_get_subtitle_format(bgav_t * bgav, int stream)
  {
  if(bgav->tt->current_track->subtitle_streams[stream].type ==
     BGAV_STREAM_SUBTITLE_TEXT)
    return (gavl_video_format_t*)0;
  return &(bgav->tt->current_track->subtitle_streams[stream].data.subtitle.format);
  }

const char * bgav_get_subtitle_language(bgav_t * b, int s)
  {
  return (b->tt->current_track->subtitle_streams[s].language[0] != '\0') ?
    b->tt->current_track->subtitle_streams[s].language : (char*)0;
  }

int bgav_read_subtitle_overlay(bgav_t * b, gavl_overlay_t * ovl, int stream)
  {
  bgav_stream_t * s = &(b->tt->current_track->subtitle_streams[stream]);
  return s->data.subtitle.decoder->decoder->decode(s, ovl);
  }

int bgav_read_subtitle_text(bgav_t * b, char ** ret, int *ret_alloc,
                            gavl_time_t * start_time, gavl_time_t * duration,
                            int stream)
  {
  int out_len;
  bgav_packet_t * p;
  bgav_stream_t * s = &(b->tt->current_track->subtitle_streams[stream]);

  if(s->packet_buffer)
    {
    if(!bgav_packet_buffer_peek_packet_read(s->packet_buffer))
      return 0;
    
    p = bgav_packet_buffer_get_packet_read(s->packet_buffer);
    bgav_packet_get_text_subtitle(p, ret, ret_alloc, start_time, duration);
    }
  else if(s->data.subtitle.subreader)
    {
    p = bgav_subtitle_reader_read_text(s);
    }
  //  fprintf(stderr, "Got packet %p\n", p);
  /* Convert packet to subtitle */

  if(!p)
    return 0;

  //  fprintf(stderr, "convert_string_realloc 2 (ret: %p, ret_alloc: %d)...",
  //          *ret, *ret_alloc);
  if(!bgav_convert_string_realloc(s->data.subtitle.cnv,
                                  (const char *)p->data, p->data_size,
                                  &out_len,
                                  ret, ret_alloc))
    return 0;
  //  fprintf(stderr, "done\n");

  
  //  fprintf(stderr, "Got subtitle: %s\n", *ret);
  
  *start_time = gavl_time_unscale(s->timescale, p->timestamp_scaled);
  *duration   = gavl_time_unscale(s->timescale, p->duration_scaled);
  
  if(s->packet_buffer)
    {
    bgav_demuxer_done_packet_read(s->demuxer, p);
    }

  
  return 1;
  }

int bgav_has_subtitle(bgav_t * b, int stream)
  {
  bgav_stream_t * s = &(b->tt->current_track->subtitle_streams[stream]);

  
  if(s->packet_buffer)
    {
    if(s->type == BGAV_STREAM_SUBTITLE_TEXT)
      {
      if(!bgav_packet_buffer_peek_packet_read(s->packet_buffer))
        return 0;
      else
        return 1;
      }
    else
      return s->data.subtitle.decoder->decoder->has_subtitle(s);
    }
  else if(s->data.subtitle.subreader)
    {
    return bgav_subtitle_reader_has_subtitle(s);
    }
  else
    return 0;
  }


void bgav_subtitle_dump(bgav_stream_t * s)
  {
  }

int bgav_subtitle_start(bgav_stream_t * s)
  {
  bgav_subtitle_overlay_decoder_t * dec;
  bgav_subtitle_overlay_decoder_context_t * ctx;

  if(s->type == BGAV_STREAM_SUBTITLE_TEXT)
    {
    if(s->data.subtitle.subreader)
      if(!bgav_subtitle_reader_start(s))
        return 0;
    
    if(s->data.subtitle.encoding)
      s->data.subtitle.cnv =
        bgav_charset_converter_create(s->data.subtitle.encoding, "UTF-8");
    else
      s->data.subtitle.cnv =
        bgav_charset_converter_create(s->opt->default_subtitle_encoding, "UTF-8");
    
    return 1;
    }
  else
    {
    dec = bgav_find_subtitle_overlay_decoder(s);
    if(!dec)
      {
      fprintf(stderr, "No subtitle overlay decoder found for fourcc ");
      bgav_dump_fourcc(s->fourcc);
      fprintf(stderr, "\n");
      return 0;
      }
    ctx = calloc(1, sizeof(*ctx));
    s->data.subtitle.decoder = ctx;
    s->data.subtitle.decoder->decoder = dec;
    //  fprintf(stderr, "Opening codec %s\n", dec->name);
    
    if(!dec->init(s))
      return 0;
    }
  return 1;
  }

void bgav_subtitle_stop(bgav_stream_t * s)
  {
  if(s->data.subtitle.cnv)
    bgav_charset_converter_destroy(s->data.subtitle.cnv);
  if(s->data.subtitle.encoding)
    free(s->data.subtitle.encoding);
  if(s->data.subtitle.subreader)
    bgav_subtitle_reader_close(s);
  }

void bgav_subtitle_resync(bgav_stream_t * s)
  {
  /* Nothing to do here */
  if(s->type == BGAV_STREAM_SUBTITLE_TEXT)
    return;

  if(s->data.subtitle.decoder &&
     s->data.subtitle.decoder->decoder->resync)
    s->data.subtitle.decoder->decoder->resync(s);

  
  }

int bgav_subtitle_skipto(bgav_stream_t * s, gavl_time_t * time)
  {
  return 0;
  }

const char * bgav_get_subtitle_info(bgav_t * b, int s)
  {
  return b->tt->current_track->subtitle_streams[s].info;
  }
