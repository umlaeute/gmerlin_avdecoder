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

int bgav_read_subtitle_overlay(bgav_t * bgav, gavl_overlay_t * ovl, int stream)
  {
  return 0;
  }

int bgav_read_subtitle_text(bgav_t * b, char ** ret, int *ret_alloc,
                            gavl_time_t * start_time, gavl_time_t * duration,
                            int stream)
  {
  bgav_packet_t * p;
  bgav_stream_t * s = &(b->tt->current_track->subtitle_streams[stream]);
  if(!bgav_packet_buffer_peek_packet_read(s->packet_buffer))
    return 0;

  p = bgav_packet_buffer_get_packet_read(s->packet_buffer);
  bgav_packet_get_text_subtitle(p, ret, ret_alloc, start_time, duration);
  bgav_demuxer_done_packet_read(s->demuxer, p);
  return 1;
  }

int bgav_has_subtitle(bgav_t * b, int stream)
  {
  bgav_stream_t * s = &(b->tt->current_track->subtitle_streams[stream]);
  if(!bgav_packet_buffer_peek_packet_read(s->packet_buffer))
    return 0;
  return 1;
  }


void bgav_subtitle_dump(bgav_stream_t * s)
  {
  }

int bgav_subtitle_start(bgav_stream_t * s)
  {
  return 0;
  }

void bgav_subtitle_stop(bgav_stream_t * s)
  {
  if(s->data.subtitle.cnv)
    bgav_charset_converter_destroy(s->data.subtitle.cnv);
  }

void bgav_subtitle_resync(bgav_stream_t * stream)
  {
  }

int bgav_subtitle_skipto(bgav_stream_t * stream, gavl_time_t * time)
  {
  return 0;
  }
