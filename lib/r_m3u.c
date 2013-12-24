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

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

#include <avdec_private.h>

#define PROBE_BYTES 10

static int probe_m3u(bgav_input_context_t * input)
  {
  char probe_buffer[PROBE_BYTES];
  char * pos;
  const char * mimetype;
  int result = 0;
  
  /* Most likely, we get this via http, so we can check the mimetype */
  if((mimetype = gavl_metadata_get(&input->metadata, GAVL_META_MIMETYPE)))
    {
    if(strcmp(mimetype, "audio/x-pn-realaudio-plugin") &&
       strcmp(mimetype, "video/x-pn-realvideo-plugin") &&
       strcmp(mimetype, "audio/x-pn-realaudio") &&
       strcmp(mimetype, "video/x-pn-realvideo") &&
       strcmp(mimetype, "audio/x-mpegurl") &&
       strcmp(mimetype, "audio/mpegurl") &&
       strcmp(mimetype, "audio/m3u") &&
       strncasecmp(mimetype, "application/x-mpegurl", 21) && // HLS
       strncasecmp(mimetype, "application/vnd.apple.mpegurl", 29)) // HLS
      return 0;
    }
  else if(input->filename)
    {
    pos = strrchr(input->filename, '.');
    if(!pos)
      return 0;
    if(strcasecmp(pos, ".m3u") &&
       strcasecmp(pos, ".m3u8") &&
       strcasecmp(pos, ".ram"))
      return 0;
    }
  
  if(bgav_input_get_data(input, (uint8_t*)probe_buffer,
                         PROBE_BYTES) < PROBE_BYTES)
    goto end;
  
  /* Some streams with the above mimetype are in realtiy
     different streams, so we check this here */
  if(strncmp(probe_buffer, "mms://", 6) &&
     strncmp(probe_buffer, "http://", 7) &&
     strncmp(probe_buffer, "rtsp://", 7) &&
     (probe_buffer[0] != '#'))
    goto end;

  /* Detect m3u8 stream playlist */
#if 0
  if(!strncasecmp(probe_buffer, "#EXTM3U", 7) &&
     !strstr(probe_buffer, "#EXT-X-STREAM-INF") &&
     !strstr(probe_buffer, "#EXT-X-I-FRAME-STREAM-INF"))
    {
    goto end;
    
    }
#endif

  result = 1;
  
  end:
  return result;
  
  }

static void add_url(bgav_redirector_context_t * r)
  {
  r->num_urls++;
  r->urls = realloc(r->urls, r->num_urls * sizeof(*(r->urls)));
  memset(r->urls + r->num_urls - 1, 0, sizeof(*(r->urls)));
  }

static char * strip_spaces(char * buffer)
  {
  char * ret;
  char * end;
  
  ret = buffer;
  while(isspace(*ret))
    ret++;
  end = &ret[strlen(ret)-1];
  while(isspace(*end) && (end > ret))
    end--;
  end++;
  *end = '\0';
  return ret;
  }

static int parse_m3u(bgav_redirector_context_t * r)
  {
  char * buffer = NULL;
  uint32_t buffer_alloc = 0;
  char * pos;

  while(1)
    {
    if(!bgav_input_read_line(r->input, &buffer, &buffer_alloc, 0, NULL))
      break;
    pos = strip_spaces(buffer);
    if((*pos == '#') || (*pos == '\0'))
      continue;
    if(!strcmp(pos, "--stop--"))
      break;

    else
      {
      add_url(r);
      r->urls[r->num_urls-1].url = gavl_strdup(pos);
      }
    }

  if(buffer)
    free(buffer);
  return !!(r->num_urls);
  }

const bgav_redirector_t bgav_redirector_m3u = 
  {
    .name =  "m3u/ram",
    .probe = probe_m3u,
    .parse = parse_m3u
  };
