/*****************************************************************
 
  avdec_common.c
 
  Copyright (c) 2003-2004 by Burkhard Plaum - plaum@ipf.uni-stuttgart.de
 
  http://gmerlin.sourceforge.net
 
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 
*****************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>
#include <avdec.h>
#include "avdec_common.h"

void * bg_avdec_create()
  {
  avdec_priv * ret = calloc(1, sizeof(*ret));
  return ret;
  }

void bg_avdec_close(void * priv)
  {
  avdec_priv * avdec;
  int i;
  avdec = (avdec_priv*)(priv);
  if(avdec->dec)
    {
    bgav_close(avdec->dec);
      avdec->dec = (bgav_t*)0;
    }
  if(avdec->track_info)
    {
    for(i = 0; i < avdec->num_tracks; i++)
      bg_track_info_free(&(avdec->track_info[i]));
    free(avdec->track_info);
    avdec->track_info = (bg_track_info_t*)0;
    }
  }

void bg_avdec_destroy(void * priv)
  {
  avdec_priv * avdec;
  bg_avdec_close(priv);

  avdec = (avdec_priv*)(priv);
  free(avdec);
  }

bg_track_info_t * bg_avdec_get_track_info(void * priv, int track)
  {
  avdec_priv * avdec;
  avdec = (avdec_priv*)(priv);
  return &(avdec->track_info[track]);
  }

int bg_avdec_read_video(void * priv,
                            gavl_video_frame_t * frame,
                            int stream)
  {
  avdec_priv * avdec;
  avdec = (avdec_priv*)(priv);
  return bgav_read_video(avdec->dec, frame, stream);
  }

int bg_avdec_read_audio(void * priv,
                            gavl_audio_frame_t * frame,
                            int stream,
                            int num_samples)
  {
  avdec_priv * avdec;
  avdec = (avdec_priv*)(priv);
  return bgav_read_audio(avdec->dec, frame, stream, num_samples);
  }

static int get_stream_action(bg_stream_action_t action)
  {
  switch(action)
    {
    case BG_STREAM_ACTION_OFF:
      return BGAV_STREAM_MUTE;
      break;
    case BG_STREAM_ACTION_DECODE:
      return BGAV_STREAM_DECODE;
      break;
    case BG_STREAM_ACTION_STANDBY:
      return BGAV_STREAM_SYNC;
      break;
    }
  return -1;
  }

int bg_avdec_set_audio_stream(void * priv,
                                  int stream,
                                  bg_stream_action_t action)
  {
  int act;
  avdec_priv * avdec;
  avdec = (avdec_priv*)(priv);
  act = get_stream_action(action);
  return bgav_set_audio_stream(avdec->dec, stream, act);
  }

int bg_avdec_set_video_stream(void * priv,
                              int stream,
                              bg_stream_action_t action)
  {
  avdec_priv * avdec;
  int act;
  avdec = (avdec_priv*)(priv);
  act = get_stream_action(action);
  return bgav_set_video_stream(avdec->dec, stream, act);
  }

void bg_avdec_start(void * priv)
  {
  int i;
  const char * str;
  avdec_priv * avdec;
  avdec = (avdec_priv*)(priv);
  
  if(avdec->bg_callbacks)
    {
    //    fprintf(stderr, "**** SET CALLBACKS *****\n");
    
    bgav_set_name_change_callback(avdec->dec,
                                  avdec->bg_callbacks->name_changed,
                                  avdec->bg_callbacks->data);
    bgav_set_track_change_callback(avdec->dec,
                                   avdec->bg_callbacks->track_changed,
                                   avdec->bg_callbacks->data);
    bgav_set_buffer_callback(avdec->dec,
                             avdec->bg_callbacks->buffer_notify,
                             avdec->bg_callbacks->data);
    }


  bgav_start(avdec->dec);
  for(i = 0; i < avdec->current_track->num_video_streams; i++)
    {
    gavl_video_format_copy(&(avdec->current_track->video_streams[i].format),
                           bgav_get_video_format(avdec->dec, i));

    str = bgav_get_video_description(avdec->dec, i);
    if(str)
      avdec->current_track->video_streams[i].description = bg_strdup(NULL, str);
    }
  for(i = 0; i < avdec->current_track->num_audio_streams; i++)
    {
    gavl_audio_format_copy(&(avdec->current_track->audio_streams[i].format),
                           bgav_get_audio_format(avdec->dec, i));
    str = bgav_get_audio_description(avdec->dec, i);
    if(str)
      avdec->current_track->audio_streams[i].description = bg_strdup(NULL, str);
    }
  bgav_dump(avdec->dec);
  }

void bg_avdec_seek(void * priv, gavl_time_t t)
  {
  avdec_priv * avdec;
  avdec = (avdec_priv*)(priv);
  //  fprintf(stderr, "seek_bgav: percentage: %f, duration: %f, pos: %f\n",
  //          percentage, gavl_time_to_seconds(avdec->track_info->duration),
  //          gavl_time_to_seconds(pos));
  bgav_seek(avdec->dec, t);
  }

int bg_avdec_init(avdec_priv * avdec)
  {
  int i;
  const char * str;
  avdec->num_tracks = bgav_num_tracks(avdec->dec);
  avdec->track_info = calloc(avdec->num_tracks, sizeof(*(avdec->track_info)));

  for(i = 0; i < avdec->num_tracks; i++)
    {
    avdec->track_info[i].num_audio_streams = bgav_num_audio_streams(avdec->dec, i);
    avdec->track_info[i].num_video_streams = bgav_num_video_streams(avdec->dec, i);
    avdec->track_info[i].seekable = bgav_can_seek(avdec->dec);
    
    if(avdec->track_info[i].num_audio_streams)
      {
      avdec->track_info[i].audio_streams =
        calloc(avdec->track_info[i].num_audio_streams,
               sizeof(*avdec->track_info[i].audio_streams));
      }
    
    if(avdec->track_info[i].num_video_streams)
      {
      avdec->track_info[i].video_streams =
        calloc(avdec->track_info[i].num_video_streams,
               sizeof(*avdec->track_info[i].video_streams));
      }
    avdec->track_info[i].duration = bgav_get_duration(avdec->dec, i);
    avdec->track_info[i].name =
      bg_strdup(avdec->track_info[i].name,
                bgav_get_track_name(avdec->dec, i));

    /* Get metadata */
    
    str = bgav_get_author(avdec->dec, i);
    if(str)
      avdec->track_info[i].metadata.author = bg_strdup(avdec->track_info[i].metadata.author, str);

    str = bgav_get_artist(avdec->dec, i);
    if(str)
      avdec->track_info[i].metadata.artist = bg_strdup(avdec->track_info[i].metadata.artist, str);

    str = bgav_get_artist(avdec->dec, i);
    if(str)
      avdec->track_info[i].metadata.artist = bg_strdup(avdec->track_info[i].metadata.artist, str);

    str = bgav_get_album(avdec->dec, i);
    if(str)
      avdec->track_info[i].metadata.album = bg_strdup(avdec->track_info[i].metadata.album, str);

    str = bgav_get_genre(avdec->dec, i);
    if(str)
      avdec->track_info[i].metadata.genre = bg_strdup(avdec->track_info[i].metadata.genre, str);
    
    str = bgav_get_title(avdec->dec, i);
    //    fprintf(stderr, "Title: %s\n", str);
    if(str)
      avdec->track_info[i].metadata.title = bg_strdup(avdec->track_info[i].metadata.title, str);
    
    str = bgav_get_copyright(avdec->dec, i);
    if(str)
      avdec->track_info[i].metadata.copyright = bg_strdup(avdec->track_info[i].metadata.copyright, str);
    
    str = bgav_get_comment(avdec->dec, i);
    if(str)
      avdec->track_info[i].metadata.comment = bg_strdup(avdec->track_info[i].metadata.comment, str);

    str = bgav_get_date(avdec->dec, i);
    if(str)
      avdec->track_info[i].metadata.date = bg_strdup(avdec->track_info[i].metadata.date, str);
    avdec->track_info[i].metadata.track = bgav_get_track(avdec->dec, i);
    }
  return 1;
  }

void
bg_avdec_set_parameter(void * p, char * name,
                    bg_parameter_value_t * val)
  {
  avdec_priv * avdec;
  avdec = (avdec_priv*)(p);
  if(!name)
    return;
  else if(!strcmp(name, "realcodec_path"))
    {
    bgav_set_dll_path_real(val->val_str);
    }
  else if(!strcmp(name, "xanimcodec_path"))
    {
    bgav_set_dll_path_xanim(val->val_str);
    }
  else if(!strcmp(name, "connect_timeout"))
    {
    avdec->connect_timeout = val->val_i;
    }
  else if(!strcmp(name, "read_timeout"))
    {
    avdec->read_timeout = val->val_i;
    }
  else if(!strcmp(name, "network_buffer_size"))
    {
    avdec->network_buffer_size = val->val_i;
    }
  else if(!strcmp(name, "device"))
    {
    avdec->device = bg_strdup(avdec->device, val->val_str);
    }
  else if(!strcmp(name, "network_bandwidth"))
    {
    if(!strcmp(val->val_str, "14.4 Kbps (Modem)"))
      avdec->network_bandwidth = 14400;
    else if(!strcmp(val->val_str, "19.2 Kbps (Modem)"))
      avdec->network_bandwidth = 19200;
    else if(!strcmp(val->val_str, "28.8 Kbps (Modem)"))
      avdec->network_bandwidth = 28800;
    else if(!strcmp(val->val_str, "33.6 Kbps (Modem)"))
      avdec->network_bandwidth = 33600;
    else if(!strcmp(val->val_str, "34.4 Kbps (Modem)"))
      avdec->network_bandwidth = 34430;
    else if(!strcmp(val->val_str, "57.6 Kbps (Modem)"))
      avdec->network_bandwidth = 57600;
    else if(!strcmp(val->val_str, "115.2 Kbps (ISDN)"))
      avdec->network_bandwidth = 115200;
    else if(!strcmp(val->val_str, "262.2 Kbps (Cable/DSL)"))
      avdec->network_bandwidth = 262200;
    else if(!strcmp(val->val_str, "393.2 Kbps (Cable/DSL)"))
      avdec->network_bandwidth = 393216;
    else if(!strcmp(val->val_str, "524.3 Kbps (Cable/DSL)"))
      avdec->network_bandwidth = 524300;
    else if(!strcmp(val->val_str, "1.5 Mbps (T1)"))
      avdec->network_bandwidth = 1544000;
    else if(!strcmp(val->val_str, "10.5 Mbps (LAN)"))
      avdec->network_bandwidth = 10485800;
    }

  else if(!strcmp(name, "http_shoutcast_metadata"))
    {
    avdec->http_shoutcast_metadata = val->val_i;
    }
  }

int bg_avdec_get_num_tracks(void * p)
  {
  avdec_priv * avdec;
  avdec = (avdec_priv*)(p);
  return avdec->num_tracks;
  }

int bg_avdec_set_track(void * priv, int track)
  {
  const char * str;
  avdec_priv * avdec;
  avdec = (avdec_priv*)(priv);
  bgav_select_track(avdec->dec, track);
  avdec->current_track = &(avdec->track_info[track]);
  
  str = bgav_get_description(avdec->dec);
  if(str)
    avdec->track_info[track].description = bg_strdup(avdec->track_info[track].description, str);


  return 1;
  }

void bg_avdec_set_callbacks(void * priv,
                            bg_input_callbacks_t * callbacks)
  {
  avdec_priv * avdec;
  avdec = (avdec_priv*)(priv);
  avdec->bg_callbacks = callbacks;
  }