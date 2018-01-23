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
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <avdec_private.h>
#include <http.h>
#include <hls.h>

#define NUM_REDIRECTIONS 5

#define LOG_DOMAIN "in_http"

/* Generic http input module */

typedef struct
  {
  int icy_metaint;
  int icy_bytes;
  bgav_http_t * h;

  bgav_charset_converter_t * charset_cnv;

  bgav_hls_t * hls;
  } http_priv;

static bgav_http_header_t * create_header(const bgav_options_t * opt)
  {
  bgav_http_header_t * header = bgav_http_header_create();
  bgav_http_header_add_line(header, "User-Agent: "PACKAGE"/"VERSION);
  bgav_http_header_add_line(header, "Accept: */*");
  
  if(opt->http_shoutcast_metadata)
    bgav_http_header_add_line(header, "Icy-MetaData:1");
  return header;
  }

static int open_http(bgav_input_context_t * ctx, const char * url, char ** r)
  {
  const char * var;
  http_priv * p;
  bgav_http_header_t * header = NULL;
  
  p = calloc(1, sizeof(*p));
  
  header = create_header(ctx->opt);
  
  p->h = bgav_http_open(url, ctx->opt, r, header);
  bgav_http_header_destroy(header);
  
  if(!p->h)
    {
    free(p);
    return 0;
    }
  
  ctx->priv = p;

  ctx->total_bytes = bgav_http_total_bytes(p->h);
  
  header = bgav_http_get_header(p->h);
  bgav_http_set_metadata(p->h, &ctx->m);
  
  //  bgav_http_header_dump(header);
  
  var = bgav_http_header_get_var(header, "icy-metaint");
  if(var)
    {
    p->icy_metaint = atoi(var);
    //    p->icy_bytes = p->icy_metaint;
    /* Then, we'll also need a charset converter */

    p->charset_cnv = bgav_charset_converter_create(ctx->opt,
                                                   "ISO-8859-1",
                                                   BGAV_UTF8);
    }

  var = bgav_http_header_get_var(header, "Accept-Ranges");
  if(!var || strcasecmp(var, "bytes"))
    ctx->flags &= ~BGAV_INPUT_CAN_SEEK_BYTE;
  else
    ctx->flags |= BGAV_INPUT_SEEK_SLOW;
  
  ctx->flags |= BGAV_INPUT_DO_BUFFER;

  ctx->url = gavl_strdup(url);
  return 1;
  }

static int64_t seek_byte_http(bgav_input_context_t * ctx,
                           int64_t pos, int whence)
  {
  bgav_http_header_t * header = NULL;
  http_priv * p = ctx->priv;
  bgav_http_close(p->h);

  header = create_header(ctx->opt);

  bgav_http_header_add_line_nocpy(header,
                                  bgav_sprintf("Range: bytes=%"PRId64"-", ctx->position));
  
  p->h = bgav_http_open(ctx->url, ctx->opt, NULL, header);
  bgav_http_header_destroy(header);
  return ctx->position;
  }

static int read_data(bgav_input_context_t* ctx,
                     uint8_t * buffer, int len, int block)
  {
  http_priv * p = ctx->priv;
  
  if(p->hls)
    return bgav_hls_read(p->hls, buffer, len, block);
  else
    return bgav_http_read(p->h, buffer, len, block);
  }

static void * memscan(void * mem_start, int size, void * key, int key_len)
  {
  void * mem = mem_start;


  while(mem - mem_start < size - key_len)
    {
    if(!memcmp(mem, key, key_len))
      return mem;
    mem++;
    }
  return NULL;
  }

static int read_shoutcast_metadata(bgav_input_context_t* ctx, int block)
  {
  char * meta_buffer;
  
  const char * pos, *end_pos;
  uint8_t icy_len;
  int meta_bytes;
  http_priv * priv;
  priv = ctx->priv;
    
  if(!read_data(ctx, &icy_len, 1, block))
    {
    return 0;
    }
  meta_bytes = icy_len * 16;
  
  //  fprintf(stderr, "Got ICY metadata %d bytes\n", meta_bytes);
  
  if(meta_bytes)
    {
    meta_buffer = malloc(meta_bytes);
    
    /* Metadata block is read in blocking mode!! */
    
    if(read_data(ctx, (uint8_t*)meta_buffer, meta_bytes, 1) < meta_bytes)
      return 0;

    gavl_hexdump((uint8_t*)meta_buffer, meta_bytes, 16);
    
    if(ctx->tt)
      {
      if((pos = memscan(meta_buffer, meta_bytes, "StreamTitle='", 13)))
        {
        pos+=13;
        end_pos = strchr(pos, ';');

        if(end_pos)
          end_pos--; // ; -> '
        }

      if(pos && end_pos)
        {
        gavl_dictionary_set_string_nocopy(ctx->tt->cur->metadata,
                                          GAVL_META_LABEL,
                                          bgav_convert_string(priv->charset_cnv ,
                                                              pos, end_pos - pos,
                                                              NULL));

        bgav_options_metadata_changed(ctx->opt, ctx->tt->cur->metadata);
        
        fprintf(stderr, "Got ICY metadata: %s\n",
                gavl_dictionary_get_string(ctx->tt->cur->metadata, GAVL_META_LABEL));
        }
      }
    free(meta_buffer);
    }
  return 1;
  }

static int do_read(bgav_input_context_t* ctx,
                   uint8_t * buffer, int len, int block)
  {
  int bytes_to_read;
  int bytes_read = 0;

  int result;
  http_priv * p = ctx->priv;

  if(!p->icy_metaint) 
    return read_data(ctx, buffer, len, block);
  else
    {
    while(bytes_read < len)
      {
      /* Read data chunk */
      
      bytes_to_read = len - bytes_read;

      if(p->icy_bytes + bytes_to_read > p->icy_metaint)
        bytes_to_read = p->icy_metaint - p->icy_bytes;

      if(bytes_to_read)
        {
        result = read_data(ctx, buffer + bytes_read, bytes_to_read, block);
        bytes_read += result;
        p->icy_bytes += result;

        if(result < bytes_to_read)
          return bytes_read;
        }

      /* Read metadata */

      if(p->icy_bytes == p->icy_metaint)
        {
        if(!read_shoutcast_metadata(ctx, block))
          return bytes_read;
        else
          p->icy_bytes = 0;
        }
      }
    }
  return bytes_read;
  }

static int read_http(bgav_input_context_t* ctx,
                     uint8_t * buffer, int len)
  {
  return do_read(ctx, buffer, len, 1);
  }

static int read_nonblock_http(bgav_input_context_t * ctx,
                              uint8_t * buffer, int len)
  {
  return do_read(ctx, buffer, len, 0);
  }

static void close_http(bgav_input_context_t * ctx)
  {
  http_priv * p = ctx->priv;

  if(p->h)
    bgav_http_close(p->h);
  
  if(p->charset_cnv)
    bgav_charset_converter_destroy(p->charset_cnv);

  if(p->hls)
    bgav_hls_close(p->hls);

  free(p);
  }

static int finalize_http(bgav_input_context_t * ctx)
  {
  http_priv * p = ctx->priv;
  if(bgav_hls_detect(ctx))
    {
    bgav_log(ctx->opt, BGAV_LOG_INFO, LOG_DOMAIN,
             "Detected http live streaming");
    p->hls = bgav_hls_create(ctx);
    
    if(!p->hls)
      return 0;
    return 1;
    }
  else
    return 1;
  }

const bgav_input_t bgav_input_http =
  {
    .name =          "http",
    .open =          open_http,
    .finalize =      finalize_http,
    .read =          read_http,
    .read_nonblock = read_nonblock_http,
    .seek_byte     = seek_byte_http,
    .close =         close_http,
  };

