/*****************************************************************
 * gmerlin-avdecoder - a general purpose multimedia decoding library
 *
 * Copyright (c) 2001 - 2008 Members of the Gmerlin project
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

#include <config.h>

#include <stdlib.h>
#include <string.h>

#include <avdec_private.h>

static int file_index_seek(bgav_file_index_t * idx, int64_t time)
  {
  int pos1, pos2, tmp;

  pos1 = 0;
  
  pos2 = idx->num_entries - 1;
  
  /* Binary search */
  while(1)
    {
    tmp = (pos1 + pos2)/2;
    
    if(idx->entries[tmp].time < time)
      pos2 = tmp;
    else
      pos1 = tmp;
    
    if(abs(pos1 - pos2) <= 4)
      break;
    }

  /* Decrease until we are the keyframe before this frame */
  while((idx->entries[pos2].time > time) && pos2)
    pos2--;
  
  return pos2;
  }

int bgav_can_seek_sample(bgav_t * bgav)
  {
  /* TODO */
  return 0;
  }

int64_t bgav_audio_duration(bgav_t * bgav, int stream)
  {
  return bgav->tt->cur->audio_streams[stream].duration;
  }

int64_t bgav_video_duration(bgav_t * bgav, int stream)
  {
  return bgav->tt->cur->video_streams[stream].duration;
  }

int64_t bgav_subtitle_duration(bgav_t * bgav, int stream)
  {
  return bgav->tt->cur->subtitle_streams[stream].duration;
  }

void bgav_seek_audio(bgav_t * bgav, int stream, int64_t sample)
  {
  bgav_stream_t * s;
  int index_pos;
  s = &bgav->tt->cur->audio_streams[stream];
  bgav_stream_clear(s);
  
  index_pos = file_index_seek(s->file_index, sample);

  /* Decrease until we have the keyframe before this frame */
  while(!s->file_index->entries[index_pos].keyframe && index_pos)
    index_pos--;

  /* Decrease until a real packet starts */
  while(index_pos &&
        (s->file_index->entries[index_pos-1].position ==
         s->file_index->entries[index_pos].position))
    index_pos--;

  s->time_scaled = s->file_index->entries[index_pos].time;
  
  s->index_position = index_pos;
  }

void bgav_seek_video(bgav_t * bgav, int stream, int64_t time)
  {
  bgav_stream_t * s;
  int index_pos;
  s = &bgav->tt->cur->audio_streams[stream];
  bgav_stream_clear(s);

  index_pos = file_index_seek(s->file_index, time);
  
  /* Decrease until we have the keyframe before this frame */
  while(!s->file_index->entries[index_pos].keyframe && index_pos)
    index_pos--;

  /* Decrease until a real packet starts */
  while(index_pos &&
        (s->file_index->entries[index_pos-1].position ==
         s->file_index->entries[index_pos].position))
    index_pos--;
  
  s->time_scaled = s->file_index->entries[index_pos].time;
  s->index_position = index_pos;
  }

int64_t bgav_video_keyframe_before(bgav_t * bgav, int stream, int64_t time)
  {
  bgav_stream_t * s;
  s = &bgav->tt->cur->audio_streams[stream];
  return -1;
  }

int64_t bgav_video_keyframe_after(bgav_t * bgav, int stream, int64_t time)
  {
  bgav_stream_t * s;
  s = &bgav->tt->cur->audio_streams[stream];
  return -1;
  
  }

void bgav_seek_subtitle(bgav_t * bgav, int stream, int64_t time)
  {
  bgav_stream_t * s;
  s = &bgav->tt->cur->audio_streams[stream];
  bgav_stream_clear(s);
  
  }
