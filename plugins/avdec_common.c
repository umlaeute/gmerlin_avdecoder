/*****************************************************************
 * gmerlin-avdecoder - a general purpose multimedia decoding library
 *
 * Copyright (c) 2001 - 2012 Members of the Gmerlin project
 * gmerlin-general@lists.sourceforge.net
 * http://gmerlin.sourceforge.net
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * *****************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#include <avdec.h>
#include "avdec_common.h"
#include <gavl/metatags.h>

static void log_callback(void*data, bgav_log_level_t level,
                         const char * log_domain,
                         const char * message)
  {
  char * domain;
  bg_log_level_t l = 0;
  
  switch(level)
    {
    case BGAV_LOG_DEBUG:
      l = BG_LOG_DEBUG;
      break;
    case BGAV_LOG_WARNING:
      l = BG_LOG_WARNING;
      break;
    case BGAV_LOG_ERROR:
      l = BG_LOG_ERROR;
      break;
    case BGAV_LOG_INFO:
      l = BG_LOG_INFO;
      break;
    }

  domain = bg_sprintf("avdecoder.%s", log_domain);
  bg_log(l, domain, "%s", message);
  free(domain);
  }
     
void * bg_avdec_create()
  {
  avdec_priv * ret = calloc(1, sizeof(*ret));
  ret->opt = bgav_options_create();
  bgav_options_set_log_callback(ret->opt, log_callback, NULL);
  return ret;
  }

void bg_avdec_close(void * priv)
  {
  avdec_priv * avdec = priv;
  int i;
  if(avdec->dec)
    {
    bgav_close(avdec->dec);
    avdec->dec = NULL;
    }
  if(avdec->track_info)
    {
    for(i = 0; i < avdec->num_tracks; i++)
      bg_track_info_free(&(avdec->track_info[i]));
    free(avdec->track_info);
    avdec->track_info = NULL;
    }
  }

int bg_avdec_get_audio_compression_info(void * priv, int stream,
                                        gavl_compression_info_t * info)
  {
  avdec_priv * avdec = priv;
  return bgav_get_audio_compression_info(avdec->dec, stream, info);
  }

int bg_avdec_get_video_compression_info(void * priv, int stream,
                                        gavl_compression_info_t * info)
  {
  avdec_priv * avdec = priv;
  return bgav_get_video_compression_info(avdec->dec, stream, info);
  }

int bg_avdec_get_overlay_compression_info(void * priv, int stream,
                                          gavl_compression_info_t * info)
  {
  avdec_priv * avdec = priv;
  return bgav_get_overlay_compression_info(avdec->dec, stream, info);
  }

gavl_video_source_t *
bg_avdec_get_video_source(void * priv, int stream)
  {
  avdec_priv * avdec = priv;
  return bgav_get_video_source(avdec->dec, stream);
  }

gavl_audio_source_t *
bg_avdec_get_audio_source(void * priv, int stream)
  {
  avdec_priv * avdec = priv;
  return bgav_get_audio_source(avdec->dec, stream);
  }

gavl_packet_source_t *
bg_avdec_get_video_packet_source(void * priv, int stream)
  {
  avdec_priv * avdec = priv;
  return bgav_get_video_packet_source(avdec->dec, stream);
  }

gavl_packet_source_t *
bg_avdec_get_audio_packet_source(void * priv, int stream)
  {
  avdec_priv * avdec = priv;
  return bgav_get_audio_packet_source(avdec->dec, stream);
  }

gavl_packet_source_t *
bg_avdec_get_text_packet_source(void * priv, int stream)
  {
  avdec_priv * avdec = priv;
  return bgav_get_text_packet_source(avdec->dec, stream);
  }

gavl_video_source_t *
bg_avdec_get_overlay_source(void * priv, int stream)
  {
  avdec_priv * avdec = priv;
  return bgav_get_overlay_source(avdec->dec, stream);
  }

gavl_packet_source_t *
bg_avdec_get_overlay_packet_source(void * priv, int stream)
  {
  avdec_priv * avdec = priv;
  return bgav_get_overlay_packet_source(avdec->dec, stream);
  }

void bg_avdec_destroy(void * priv)
  {
  avdec_priv * avdec = priv;
  bg_avdec_close(priv);

  if(avdec->dec)
    {
    bgav_close(avdec->dec);
    }
  if(avdec->opt)
    {
    bgav_options_destroy(avdec->opt);
    }
  free(avdec);
  }

#ifdef NEW_STREAMINFO_API
gavl_dictionary_t * bg_avdec_get_media_info(void * p)
  {
  avdec_priv * avdec = p;
  return bgav_get_media_info(avdec->dec);
  }

#else
int bg_avdec_get_num_tracks(void * p)
  {
  avdec_priv * avdec = p;
  return avdec->num_tracks;
  }

bg_track_info_t * bg_avdec_get_track_info(void * priv, int track)
  {
  avdec_priv * avdec = priv;
  
  if(track == -1)
    return avdec->current_track;
  
  if((track < 0) || (track >= avdec->num_tracks))
    return NULL;
  return &(avdec->track_info[track]);
  }
#endif


const gavl_edl_t * bg_avdec_get_edl(void * priv)
  {
  avdec_priv * avdec = priv;
  return bgav_get_edl(avdec->dec);
  }

int bg_avdec_read_video(void * priv,
                            gavl_video_frame_t * frame,
                            int stream)
  {
  avdec_priv * avdec = priv;
  return bgav_read_video(avdec->dec, frame, stream);
  }

void bg_avdec_skip_video(void * priv, int stream, int64_t * time,
                         int scale, int exact)
  {
  avdec_priv * avdec = priv;
  bgav_skip_video(avdec->dec, stream, time, scale, exact);
  }

int bg_avdec_has_still(void * priv,
                       int stream)
  {
  avdec_priv * avdec = priv;
  return bgav_video_has_still(avdec->dec, stream);
  }


int bg_avdec_read_audio(void * priv,
                            gavl_audio_frame_t * frame,
                            int stream,
                            int num_samples)
  {
  avdec_priv * avdec = priv;
  return bgav_read_audio(avdec->dec, frame, stream, num_samples);
  }



static bgav_stream_action_t get_stream_action(bg_stream_action_t action)
  {
  switch(action)
    {
    case BG_STREAM_ACTION_OFF:
      return BGAV_STREAM_MUTE;
      break;
    case BG_STREAM_ACTION_DECODE:
      return BGAV_STREAM_DECODE;
      break;
    case BG_STREAM_ACTION_READRAW:
      return BGAV_STREAM_READRAW;
      break;
    }
  return -1;
  }

int bg_avdec_set_audio_stream(void * priv,
                                  int stream,
                                  bg_stream_action_t action)
  {
  bgav_stream_action_t act;
  avdec_priv * avdec = priv;
  act = get_stream_action(action);
  return bgav_set_audio_stream(avdec->dec, stream, act);
  }

int bg_avdec_set_video_stream(void * priv,
                              int stream,
                              bg_stream_action_t action)
  {
  avdec_priv * avdec = priv;
  bgav_stream_action_t  act;
  act = get_stream_action(action);

  return bgav_set_video_stream(avdec->dec, stream, act);
  }

int bg_avdec_set_text_stream(void * priv,
                             int stream,
                             bg_stream_action_t action)
  {
  bgav_stream_action_t  act;
  avdec_priv * avdec = priv;
  act = get_stream_action(action);

  return bgav_set_text_stream(avdec->dec, stream, act);
  }

int bg_avdec_set_overlay_stream(void * priv,
                                int stream,
                                bg_stream_action_t action)
  {
  bgav_stream_action_t  act;
  avdec_priv * avdec = priv;
  act = get_stream_action(action);

  return bgav_set_overlay_stream(avdec->dec, stream, act);
  }

int bg_avdec_start(void * priv)
  {
  int i;
  const gavl_video_format_t * format;
  avdec_priv * avdec = priv;
  
  if(!bgav_start(avdec->dec))
    {
    return 0;
    }
  for(i = 0; i < avdec->current_track->num_vstreams; i++)
    {
    gavl_video_format_copy(&(avdec->current_track->vstreams[i].fmt),
                           bgav_get_video_format(avdec->dec, i));

    gavl_dictionary_merge2(&avdec->current_track->vstreams[i].md,
                           bgav_get_video_metadata(avdec->dec, i));
    
    }
  for(i = 0; i < avdec->current_track->num_astreams; i++)
    {
    gavl_audio_format_copy(&(avdec->current_track->astreams[i].fmt),
                           bgav_get_audio_format(avdec->dec, i));

    gavl_dictionary_merge2(&avdec->current_track->astreams[i].md,
                         bgav_get_audio_metadata(avdec->dec, i));
    
    }

  for(i = 0; i < avdec->current_track->num_tstreams; i++)
    {
    gavl_dictionary_merge2(&avdec->current_track->tstreams[i].md,
                         bgav_get_text_metadata(avdec->dec, i));
    
    }

  for(i = 0; i < avdec->current_track->num_ostreams; i++)
    {
    gavl_dictionary_merge2(&avdec->current_track->ostreams[i].md,
                         bgav_get_overlay_metadata(avdec->dec, i));
    
   format = bgav_get_overlay_format(avdec->dec, i);
    gavl_video_format_copy(&avdec->current_track->ostreams[i].fmt,
                           format);
    }
  return 1;
  }

void bg_avdec_seek(void * priv, int64_t * t, int scale)
  {
  avdec_priv * avdec = priv;
  bgav_seek_scaled(avdec->dec, t, scale);
  }

int bg_avdec_init(avdec_priv * avdec)
  {
  int i, j;
  const bgav_metadata_t * m;
  
  avdec->num_tracks = bgav_num_tracks(avdec->dec);
  avdec->track_info = calloc(avdec->num_tracks, sizeof(*(avdec->track_info)));
  
  for(i = 0; i < avdec->num_tracks; i++)
    {
    bg_track_info_set_num_audio_streams(&avdec->track_info[i], bgav_num_audio_streams(avdec->dec, i));
    bg_track_info_set_num_video_streams(&avdec->track_info[i], bgav_num_video_streams(avdec->dec, i));
    bg_track_info_set_num_text_streams(&avdec->track_info[i], bgav_num_text_streams(avdec->dec, i));
    bg_track_info_set_num_overlay_streams(&avdec->track_info[i], bgav_num_overlay_streams(avdec->dec, i));
        
    if(avdec->track_info[i].num_astreams)
      {
      for(j = 0; j < avdec->track_info[i].num_astreams; j++)
        {
        gavl_audio_format_copy(&(avdec->track_info[i].astreams[j].fmt),
                               bgav_get_audio_format_t(avdec->dec, i, j));

        gavl_dictionary_copy(&(avdec->track_info[i].astreams[j].md),
                           bgav_get_audio_metadata_t(avdec->dec, i, j));
        
        }
      }
    if(avdec->track_info[i].num_vstreams)
      {
      for(j = 0; j < avdec->track_info[i].num_vstreams; j++)
        {
        gavl_video_format_copy(&(avdec->track_info[i].vstreams[j].fmt),
                               bgav_get_video_format_t(avdec->dec, i, j));

        gavl_dictionary_copy(&(avdec->track_info[i].vstreams[j].md),
                           bgav_get_video_metadata_t(avdec->dec, i, j));
        
        }
      }
    if(avdec->track_info[i].num_tstreams)
      {
      for(j = 0; j < avdec->track_info[i].num_tstreams; j++)
        {
        int idx = j + avdec->track_info[i].num_ostreams;
        gavl_dictionary_copy(&(avdec->track_info[i].tstreams[j].md),
                           bgav_get_text_metadata_t(avdec->dec, i, idx));
        }
      }
    if(avdec->track_info[i].num_ostreams)
      {
      for(j = 0; j < avdec->track_info[i].num_ostreams; j++)
        {
        gavl_dictionary_copy(&(avdec->track_info[i].ostreams[j].md),
                           bgav_get_overlay_metadata_t(avdec->dec, i, j));
        }
      }
    
    /* Get metadata */
    
    m = bgav_get_metadata(avdec->dec, i);
    
    gavl_dictionary_copy(&avdec->track_info[i].md, m);

    }
  return 1;
  }

void
bg_avdec_set_parameter(void * p, const char * name,
                       const gavl_value_t * val)
  {
  avdec_priv * avdec = p;
  bg_avdec_option_set_parameter(avdec->opt, name, val);
  }



int bg_avdec_set_track(void * priv, int track)
  {
  int i;
  avdec_priv * avdec = priv;
  
  if(!bgav_select_track(avdec->dec, track))
    return 0;
  avdec->current_track = &(avdec->track_info[track]);
  
  /* Get formats (need them for compressed output */
  for(i = 0; i < avdec->current_track->num_astreams; i++)
    gavl_audio_format_copy(&(avdec->current_track->astreams[i].fmt),
                           bgav_get_audio_format(avdec->dec, i));

  for(i = 0; i < avdec->current_track->num_vstreams; i++)
    gavl_video_format_copy(&(avdec->current_track->vstreams[i].fmt),
                           bgav_get_video_format(avdec->dec, i));

  
  return 1;
  }

gavl_frame_table_t * bg_avdec_get_frame_table(void * priv, int stream)
  {
  avdec_priv * avdec = priv;
  return bgav_get_frame_table(avdec->dec, stream);
  }

static void metadata_change_callback(void * priv,
                                     const gavl_dictionary_t * metadata)
  {
  avdec_priv * avdec;
  avdec = priv;

  /* Merge metadata */

  if(avdec->current_track)
    {
    gavl_dictionary_copy(bg_track_info_get_metadata_nc(avdec->current_track),
                         metadata);

    fprintf(stderr, "Metadata callback %p %p\n",
            avdec->bg_callbacks, avdec->bg_callbacks->metadata_changed);
    

    if(avdec->bg_callbacks && avdec->bg_callbacks->metadata_changed)
      {
      avdec->bg_callbacks->metadata_changed(avdec->bg_callbacks->data,
                                            bg_track_info_get_metadata(avdec->current_track));
      }
    }
  }



void bg_avdec_set_callbacks(void * priv,
                            bg_input_callbacks_t * callbacks)
  {
  bgav_options_t * opt;
  avdec_priv * avdec = priv;
  avdec->bg_callbacks = callbacks;

  if(!callbacks)
    return;
  
  
  bgav_options_set_buffer_callback(avdec->opt,
                           avdec->bg_callbacks->buffer_notify,
                           avdec->bg_callbacks->data);

  bgav_options_set_user_pass_callback(avdec->opt,
                             avdec->bg_callbacks->user_pass,
                             avdec->bg_callbacks->data);
  
  bgav_options_set_aspect_callback(avdec->opt,
                                   avdec->bg_callbacks->aspect_changed,
                                   avdec->bg_callbacks->data);
  
  if(avdec->bg_callbacks->metadata_changed)
    {
    bgav_options_set_metadata_change_callback(avdec->opt,
                                      metadata_change_callback,
                                      priv);
    }
  if(avdec->dec)
    {
    opt = bgav_get_options(avdec->dec);
    bgav_options_copy(opt, avdec->opt);
    }
  }

bg_device_info_t * bg_avdec_get_devices(bgav_device_info_t * info)
  {
  int i = 0;
  bg_device_info_t * ret = NULL;
  if(!info)
    return ret;
  
  while(info[i].device)
    {
    ret = bg_device_info_append(ret,
                                info[i].device,
                                info[i].name);
    i++;
    }
  return ret;
  }

const char * bg_avdec_get_disc_name(void * priv)
  {
  avdec_priv * avdec = priv;
  if(avdec->dec)
    return bgav_get_disc_name(avdec->dec);
  return NULL;
  }

