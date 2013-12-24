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

#include <avdec_private.h>
#include <http.h>
#include <hls.h>
#include <string.h>
#include <stdlib.h>

typedef struct
  {
  char * url;
  double duration;
  int seq;
  } hls_url_t;

struct bgav_hls_s
  {
  char * url;
  
  bgav_http_t * stream_socket;
  bgav_http_t * m3u8_socket;
  
  hls_url_t * urls;
  int urls_alloc;
  int num_urls;
  
  int seq;
  
  int64_t bc;  // Byte counter
  int64_t end; // End of segment

  bgav_input_context_t * ctx;
  };

int bgav_hls_detect(bgav_input_context_t * ctx)
  {
  const char * var;
  char * probe_buffer = NULL;
  int result = 0;
  
  if(!(var = gavl_metadata_get(&ctx->metadata, GAVL_META_MIMETYPE)))
    goto end;
  
  if(strncasecmp(var, "application/x-mpegurl", 21) && 
     strncasecmp(var, "application/vnd.apple.mpegurl", 29))
    goto end;

  if(ctx->total_bytes <= 0)
    goto end;
  
  probe_buffer = malloc(ctx->total_bytes+1);

  if(bgav_input_get_data(ctx, (uint8_t*)probe_buffer, ctx->total_bytes) <
     ctx->total_bytes)
    goto end;

  probe_buffer[ctx->total_bytes] = '\0';
  
  if(strstr(probe_buffer, "\n#EXT-X-STREAM-INF") ||
     strstr(probe_buffer, "\n#EXT-X-I-FRAME-STREAM-INF"))
    goto end;
  
  result = 1;
  end:

  if(probe_buffer)
    free(probe_buffer);
  
  return result;
  }

static void free_urls(bgav_hls_t * h)
  {
  int i;
  for(i = 0; i < h->urls_alloc; i++)
    {
    if(h->urls[i].url)
      {
      free(h->urls[i].url);
      h->urls[i].url = NULL;
      }
    }
  }

static void parse_urls(bgav_hls_t * h, const char * m3u8)
  {
  int i;
  char ** lines;
  char * pos;
  int seq = 0;
  free_urls(h);

  /* Split m3u into lines */
  
  lines = bgav_stringbreak(m3u8, '\n');
  i = 0;

  while(lines[i])
    {
    if((pos = strchr(lines[i], '\r')))
      *pos = '\0';
    i++;
    }

  h->num_urls = 0;
  
  /* Parse the header */

  i = 0;
  while(lines[i])
    {
    if(!strncasecmp(lines[i], "#EXT-X-MEDIA-SEQUENCE", 21))
      {
      if((pos = strchr(lines[i], ':')))
        {
        pos++;
        seq = atoi(pos);
        }
      }
    else if(!strncasecmp(lines[i], "#EXTINF", 7))
      break;
    i++;
    }

  /* Parse the urls */

  while(lines[i])
    {
    // URL info
    if(strncasecmp(lines[i], "#EXTINF", 7))
      {

      }
    else if(!strncasecmp(lines[i], "http://", 7))
      {
      
      }
    else if(!strncasecmp(lines[i], "#EXT-X-ENDLIST", 14))
      {
      
      }
    i++;
    }
  
  }

bgav_hls_t * bgav_hls_create(bgav_input_context_t * ctx)
  {
  char * m3u8 = NULL;
  
  bgav_hls_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->ctx = ctx;
  
  fprintf(stderr, "URL: %s\n", ctx->url);

  /* Read the initial m3u8 */
  m3u8 = malloc(ctx->total_bytes + 1);
  if(bgav_input_read_data(ctx, (uint8_t*)m3u8, ctx->total_bytes) <
     ctx->total_bytes)
    goto fail;

  m3u8[ctx->total_bytes] = '\0';
  parse_urls(ret, m3u8);
  free(m3u8);
  
  return ret;

  fail:

  if(m3u8)
    free(m3u8);
  
  bgav_hls_close(ret);
  return NULL;
  }

int bgav_hls_read(bgav_hls_t * h, uint8_t * data, int len, int block)
  {
  return 0;
  }

void bgav_hls_close(bgav_hls_t * h)
  {
  free_urls(h);

  if(h->urls)
    free(h->urls);
  
  free(h);
  }

