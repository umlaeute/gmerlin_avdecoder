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
#include <ctype.h>

typedef struct
  {
  char * url;
  char * title;
  double duration;
  int seq;
  } hls_url_t;

struct bgav_hls_s
  {
  char * url;
  
  bgav_http_t * stream_socket;
  bgav_http_t * m3u8_socket;

  bgav_http_header_t * stream_header;
  
  hls_url_t * urls;
  int urls_alloc;
  int num_urls;
  
  int seq;
  int end_of_sequence; // End of sequence detected
  
  int64_t segment_pos;  // Byte counter
  int64_t segment_size; // End of segment

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
    if(h->urls[i].title)
      {
      free(h->urls[i].title);
      h->urls[i].title = NULL;
      }
    }
  h->num_urls = 0;
  }

static void dump_urls(bgav_hls_t * h)
  {
  int i;
  bgav_dprintf("HLS url list\n");
  for(i = 0; i < h->num_urls; i++)
    {
    bgav_dprintf("  URL %d\n", i+1);
    bgav_dprintf("    Duration %f\n", h->urls[i].duration);
    bgav_dprintf("    Title    %s\n", h->urls[i].title);
    bgav_dprintf("    Sequence %d\n", h->urls[i].seq);
    bgav_dprintf("    URL      %s\n", h->urls[i].url);
    }
  }

static char * extract_url(bgav_hls_t * h, const char * url)
  {
  char * ret = NULL;
  char * base;
  const char * pos1;
  const char * pos2;
  
  if(!strncmp(url, "http://", 7)) // Complete URL
    return gavl_strdup(url);
  else if(*url == '/') // Relative to host
    {
    pos1 = h->ctx->url + 7;
    pos2 = strchr(h->ctx->url, '/');
    if(!pos2)
      pos2 = h->ctx->url + strlen(h->ctx->url);
    
    base = gavl_strndup(h->ctx->url, pos2);
    
    ret = bgav_sprintf("%s/%s", base, url);
    free(base);
    }
  else // Relative m3u8
    {
    pos2 = strrchr(h->ctx->url, '/');
    base = gavl_strndup(h->ctx->url, pos2);
    ret = bgav_sprintf("%s/%s", base, url);
    free(base);
    }
  return ret;
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
    if(!strncasecmp(lines[i], "#EXTINF", 7))
      {
      if(h->urls_alloc < h->num_urls + 1)
        {
        h->urls_alloc += 16;
        h->urls = realloc(h->urls, h->urls_alloc * sizeof(*h->urls));
        memset(h->urls + h->num_urls, 0,
               (h->urls_alloc - h->num_urls) * sizeof(*h->urls));
        }
      
      if((pos = strchr(lines[i], ':')))
        {
        pos++;
        /*
         *  This breaks when the LC_NUMERIC locale is German
         *  for example
         */
        h->urls[h->num_urls].duration = strtod(pos, NULL);

        if((pos = strchr(pos, ',')))
          {
          pos++;

          while(isspace(*pos) && *pos != '\0')
            pos++;
          
          if(*pos != '\0')
            h->urls[h->num_urls].title = gavl_strdup(pos);
          }
        }
      }
    else if(*(lines[i]) != '#')
      {
      h->urls[h->num_urls].url = extract_url(h, lines[i]);
      h->urls[h->num_urls].seq = seq++;
      h->num_urls++;
      }
    else if(!strncasecmp(lines[i], "#EXT-X-ENDLIST", 14))
      {
      h->end_of_sequence = 1;
      break;
      }
    i++;
    }

  bgav_stringbreak_free(lines);
  dump_urls(h);
  }

bgav_hls_t * bgav_hls_create(bgav_input_context_t * ctx)
  {
  char * m3u8 = NULL;
  
  bgav_hls_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->ctx = ctx;
  
  // fprintf(stderr, "URL: %s\n", ctx->url);

  /* Read the initial m3u8 */
  m3u8 = malloc(ctx->total_bytes + 1);
  if(bgav_input_read_data(ctx, (uint8_t*)m3u8, ctx->total_bytes) <
     ctx->total_bytes)
    goto fail;

  m3u8[ctx->total_bytes] = '\0';
  parse_urls(ret, m3u8);
  free(m3u8);
  m3u8 = NULL;
  
  if(!ret->num_urls)
    goto fail;
  
  /* Load first URL */
  //  h->stream_socket = bgav_http_open(

  ret->stream_header = bgav_http_header_create();
  
  bgav_http_header_add_line(ret->stream_header, "User-Agent: "PACKAGE"/"VERSION);
  bgav_http_header_add_line(ret->stream_header, "Accept: */*");
  bgav_http_header_add_line(ret->stream_header, "Connection: Keep-Alive");

  fprintf(stderr, "Loading %s\n", ret->urls[0].url);
  
  ret->stream_socket = bgav_http_reopen(NULL, ret->urls[0].url, ctx->opt,
                                        NULL,
                                        ret->stream_header);

  gavl_metadata_free(&ret->ctx->metadata);
  gavl_metadata_init(&ret->ctx->metadata);
  
  if(!ret->stream_socket)
    goto fail;

  bgav_http_set_metadata(ret->stream_socket, &ctx->metadata);

  ret->ctx->total_bytes = 0;
  ret->segment_size = bgav_http_total_bytes(ret->stream_socket);
  
  return ret;

  fail:

  if(m3u8)
    free(m3u8);
  
  bgav_hls_close(ret);
  return NULL;
  }

int bgav_hls_read(bgav_hls_t * h, uint8_t * data, int len, int block)
  {
  int bytes_to_read;
  int result;
  int bytes_read = 0;
  int idx;
  int i;
  
  while(bytes_read < len)
    {
    if(h->segment_pos >= h->segment_size)
      {
      fprintf(stderr, "End of segment\n");

      /* Check if we should reload the m3u8 */
      
      /* Open next segment */

      h->seq++;

      idx = -1;

      for(i = 0; i < h->num_urls; i++)
        {
        if(h->urls[i].seq == h->seq)
          {
          idx = i;
          break;
          }
        }

      if(idx < 0)
        return bytes_read;

      fprintf(stderr, "Loading %s\n", h->urls[idx].url);
      
      h->stream_socket = bgav_http_reopen(h->stream_socket,
                                          h->urls[idx].url,
                                          h->ctx->opt,
                                          NULL,
                                          h->stream_header);

      if(!h->stream_socket)
        return bytes_read;

      h->segment_size = bgav_http_total_bytes(h->stream_socket);
      h->segment_pos = 0;
      }
    
    bytes_to_read = len - bytes_read;
    if(bytes_to_read > h->segment_size - h->segment_pos)
      bytes_to_read = h->segment_size - h->segment_pos;
    
    result = bgav_http_read(h->stream_socket, data + bytes_read, bytes_to_read, block);
    
    if(result <= 0)
      return bytes_read;

    bytes_read += result;
    h->segment_pos += result;
    }

  return bytes_read;
  }

void bgav_hls_close(bgav_hls_t * h)
  {
  free_urls(h);

  if(h->urls)
    free(h->urls);
  
  if(h->stream_header)
    bgav_http_header_destroy(h->stream_header);

  if(h->stream_socket)
    bgav_http_close(h->stream_socket);
  
  free(h);
  }

