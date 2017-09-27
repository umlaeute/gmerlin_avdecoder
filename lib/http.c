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
#define LOG_DOMAIN "http"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

#include <http.h>

struct bgav_http_s
  {
  const bgav_options_t * opt;
  bgav_http_header_t * header;
  int fd;
  char * keepalive_host;

  int chunked; // Chunked transfer encoding
  int chunk_size;
  int chunk_pos;
  int chunk_error;
  int chunk_eof;
  
  int64_t content_length;
  int can_seek;
  };

static bgav_http_t *
do_connect(bgav_http_t * ret, const char * host, int port, const bgav_options_t * opt,
           bgav_http_header_t * request_header,
           bgav_http_header_t * extra_header)
  {
  const char * var;
  
  //  bgav_http_t * ret = NULL;
  
  if(opt->dump_headers)
    {
    bgav_dprintf("Sending header\n");
    bgav_http_header_dump(request_header);
    if(extra_header)
      bgav_http_header_dump(extra_header);
    }
  
  if(!ret)
    {
    ret = calloc(1, sizeof(*ret));
    ret->opt = opt;
    ret->fd = -1;
    }

  if(ret->header)
    {
    bgav_http_header_destroy(ret->header);
    ret->header = NULL;
    }
  
  if(ret->keepalive_host && strcmp(ret->keepalive_host, host))
    {
    close(ret->fd);
    ret->fd = -1;
    free(ret->keepalive_host);
    ret->keepalive_host = NULL;
    }
  
  if(ret->fd < 0)
    {
    ret->fd = bgav_tcp_connect(ret->opt, host, port);
    if(ret->fd == -1)
      goto fail;
    }
  
  if(!bgav_http_header_send(ret->opt, request_header, extra_header, ret->fd))
    {
    if(ret->keepalive_host) // Keepalive connection got closed by server
      {
      close(ret->fd);
      ret->fd = bgav_tcp_connect(ret->opt, host, port);
      if((ret->fd == -1) ||
         (!bgav_http_header_send(ret->opt, request_header, extra_header, ret->fd)))
        goto fail;
      }
    else
      goto fail;
    }
  
  ret->header = bgav_http_header_create();
  
  if(!bgav_http_header_revc(ret->opt, ret->header, ret->fd))
    {
    bgav_log(ret->opt, BGAV_LOG_ERROR, LOG_DOMAIN, "Reading response failed: %s", strerror(errno));
    goto fail;
    }

  if(opt->dump_headers)
    {
    bgav_dprintf("Got response\n");
    bgav_http_header_dump(ret->header);
    }

  if(ret->keepalive_host)
    {
    free(ret->keepalive_host);
    ret->keepalive_host = NULL;
    }
  
  var = bgav_http_header_get_var(ret->header, "Connection");
  if(var && !strcasecmp(var, "Keep-alive"))
    ret->keepalive_host = gavl_strdup(host);

  ret->content_length = 0;
  
  var = bgav_http_header_get_var(ret->header, "Content-Length");
  if(var)
    ret->content_length = strtoll(var, NULL, 10);
  
  var = bgav_http_header_get_var(ret->header, "Transfer-Encoding");
  if(var && !strcasecmp(var, "chunked"))
    {
    ret->chunked = 1;
    ret->content_length = 0;
    }
  
  return ret;
  
  fail:
  
  if(ret)
    bgav_http_close(ret);
  return NULL;
  }

static char * encode_user_pass(const char * user, const char * pass)
  {
  int userpass_len;
  int userpass_enc_len;
  char * userpass;
  char * ret;

  userpass = bgav_sprintf("%s:%s", user, pass);
  
  userpass_len = strlen(userpass);
  userpass_enc_len = (userpass_len * 4)/3+4;
  
  ret = malloc(userpass_enc_len);
  userpass_enc_len = bgav_base64encode((uint8_t*)userpass,
                                       userpass_len,
                                       (uint8_t*)ret,
                                       userpass_enc_len);
  ret[userpass_enc_len] = '\0';
  return ret;
  }

static bgav_http_t * http_open(bgav_http_t * ret,
                               const char * url, const bgav_options_t * opt,
                               char ** redirect_url,
                               bgav_http_header_t * extra_header)
  {
  int port;
  int status;
  const char * location;

  char * userpass_enc;
    
  char * host     = NULL;
  char * path     = NULL;
  char * line     = NULL;
  char * user     = NULL;
  char * pass     = NULL;
  char * protocol = NULL;
  int default_port = 0;
  
  const char * real_host;
  int real_port;
  
  bgav_http_header_t * request_header = NULL;
  
  port = -1;
  if(!bgav_url_split(url,
                     &protocol,
                     &user, /* User */
                     &pass, /* Pass */
                     &host,
                     &port,
                     &path))
    {
    bgav_log(opt, BGAV_LOG_ERROR, LOG_DOMAIN, "Unvalid URL");
    goto fail;
    }
  if(path && !strcmp(path, ";stream.nsv"))
    {
    free(path);
    path = NULL;
    }

  if(port == -1)
    {
    port = 80;
    default_port = 1;
    }
  
  /* Check for proxy */

  if(opt->http_use_proxy)
    {
    real_host = opt->http_proxy_host;
    real_port = opt->http_proxy_port;
    }
  else
    {
    real_host = host;
    real_port = port;
    }
  
  /* Build request */

  request_header = bgav_http_header_create();

  if(opt->http_use_proxy)
    line = bgav_sprintf("GET %s HTTP/1.1", url);
  else
    line = bgav_sprintf("GET %s HTTP/1.1", ((path) ? path : "/"));
  
  bgav_http_header_add_line_nocpy(request_header, line);
  
  /* Proxy authentication */
  
  if(opt->http_use_proxy && opt->http_proxy_auth)
    {
    userpass_enc = encode_user_pass(opt->http_proxy_user, opt->http_proxy_pass);
    line = bgav_sprintf("Proxy-Authorization: Basic %s", userpass_enc);
    bgav_http_header_add_line_nocpy(request_header, line);
    free(userpass_enc);
    }

  if(default_port)
    line = bgav_sprintf("Host: %s", host);
  else
    line = bgav_sprintf("Host: %s:%d", host, port);
  
  bgav_http_header_add_line_nocpy(request_header, line);
  
  ret = do_connect(ret, real_host, real_port, opt, request_header, extra_header);
  if(!ret)
    goto fail;

  /* Check status code */
  status = bgav_http_header_status_code(ret->header);

  if(status == 401)
    {
    /* Ok, they won't let us in, try to get a username and/or password */
    bgav_http_close(ret);
    ret = NULL;
    
    if((!user || !pass) && opt->user_pass_callback)
      {
      if(user) { free(user); user = NULL; }
      if(pass) { free(pass); pass = NULL; }
      
      if(!opt->user_pass_callback(opt->user_pass_callback_data,
                                  host, &user, &pass))
        goto fail;
      }
    
    if(!user || !pass)
      goto fail;
    
    /* Now, user and pass should be the authentication data */
    
    userpass_enc = encode_user_pass(user, pass);
    line = bgav_sprintf("Authorization: Basic %s", userpass_enc);
    bgav_http_header_add_line_nocpy(request_header, line);
    free(userpass_enc);
    
    ret = do_connect(ret, real_host, real_port, opt, request_header, extra_header);
    if(!ret)
      goto fail;
    /* Check status code */
    status = bgav_http_header_status_code(ret->header);
    }
    
  if(status >= 400) /* Error */
    {
    if(bgav_http_header_status_line(ret->header))
      bgav_log(opt, BGAV_LOG_ERROR, LOG_DOMAIN, "%s",
               bgav_http_header_status_line(ret->header));
    goto fail;
    }
  else if(status >= 300) /* Redirection */
    {
    /* Extract redirect URL */

    if(*redirect_url)
      {
      free(*redirect_url);
      *redirect_url = NULL;
      }
    location = bgav_http_header_get_var(ret->header, "Location");

    if(location)
      *redirect_url = gavl_strdup(location);
    else
      {
      bgav_log(opt, BGAV_LOG_ERROR, LOG_DOMAIN,
               "Got redirection but no URL");
      }
    
    if(host)
      free(host);
    if(path)
      free(path);
    if(protocol)
      free(protocol);
    if(request_header)
      bgav_http_header_destroy(request_header);
    bgav_http_close(ret);
    return NULL;
    }
  else if(status < 200)  /* Error */
    {
    if(bgav_http_header_status_line(ret->header))
      bgav_log(opt, BGAV_LOG_ERROR, LOG_DOMAIN, "%s",
               bgav_http_header_status_line(ret->header));
    goto fail;
    }
  
  //  bgav_http_header_dump(ret->header);
  if(request_header)
    bgav_http_header_destroy(request_header);
  
  if(host)
    free(host);
  if(path)
    free(path);
  if(protocol)
    free(protocol);
  
  return ret;
  
  fail:
  if(*redirect_url)
    {
    free(*redirect_url);
    *redirect_url = NULL;
    }
  
  if(host)
    free(host);
  if(path)
    free(path);
  if(protocol)
    free(protocol);
  if(request_header)
    bgav_http_header_destroy(request_header);
  
  if(ret)
    {
    if(ret->header)
      bgav_http_header_destroy(ret->header);
    free(ret);
    }
  return NULL;
  }

#define MAX_REDIRECTIONS 5

bgav_http_t * bgav_http_reopen(bgav_http_t * ret,
                               const char * url_orig, const bgav_options_t * opt,
                               char ** redirect_url,
                               bgav_http_header_t * extra_header)
  {
  int i;
    
  char * url = NULL;
  char * r = NULL;
  
  if(redirect_url)
    return http_open(ret, url_orig, opt, redirect_url, extra_header);
  
  url = gavl_strdup(url_orig);
    
  for(i = 0; i < MAX_REDIRECTIONS; i++)
    {
    ret = http_open(ret, url, opt, &r, extra_header);
    if(ret)
      break;
    if(r)
      {
      if(url)
        free(url);
      url = r;
      r = NULL;
      }
    }

  if(url)
    free(url);
  if(r)
    free(r);
  return ret;
  }

bgav_http_t * bgav_http_open(const char * url, const bgav_options_t * opt,
                             char ** redirect_url,
                             bgav_http_header_t * extra_header)
  {
  return bgav_http_reopen(NULL, url, opt, redirect_url, extra_header);
  }


void bgav_http_close(bgav_http_t * h)
  {
  if(h->fd >= 0)
    closesocket(h->fd);
  if(h->header)
    bgav_http_header_destroy(h->header);
  if(h->keepalive_host)
    free(h->keepalive_host);
  free(h);
  }

bgav_http_header_t* bgav_http_get_header(bgav_http_t * h)
  {
  return h->header;
  }

static int next_chunk(bgav_http_t * h, int block)
  {
  uint8_t c;
  char buf[16];
  int buf_len = 0;

  //  fprintf(stderr, "Next chunk\n");
  
  if(h->chunk_size)
    {
    /* Read "\r\n" from previous chunk.
       This is always done in blocking mode since we
       don't expect to wait too long */

    if((bgav_read_data_fd(h->opt, h->fd, (uint8_t*)buf, 2, h->opt->read_timeout) < 2) ||
       (buf[0] != '\r') ||
       (buf[1] != '\n'))
      {
      h->chunk_error = 1;
      return 0;
      }
    h->chunk_size = 0;
    }

  /* Read first character of the chunk length */

  if(block)
    {
    if(bgav_read_data_fd(h->opt, h->fd, &c, 1, h->opt->read_timeout) < 1)
      {
      h->chunk_error = 1;
      return 0;
      }
    }
  else if(bgav_read_data_fd(h->opt, h->fd, &c, 1, 0) < 1)
    return 0;
  
  /* Read chunk len */

  buf[0] = c;
  buf_len = 1;

  /* Allow max 4 GB chunks */
  while(buf_len < 8)
    {
    if(bgav_read_data_fd(h->opt, h->fd, &c, 1, h->opt->read_timeout) < 1)
      {
      h->chunk_error = 1;
      return 0;
      }
    if(c == '\n')
      break;
    
    if(c != '\r')
      {
      buf[buf_len] = c;
      buf_len++;
      }
    }

  if(c != '\n')
    {
    h->chunk_error = 1;
    return 0;
    }

  buf[buf_len] = '\0';

  h->chunk_size = strtoul(buf, NULL, 16);
  h->chunk_pos = 0;

  if(!h->chunk_size)
    h->chunk_eof = 1;
  
  // fprintf(stderr, "Chunk size: %d\n", h->chunk_size);
  return 1;
  }

static int read_chunked(bgav_http_t * h, uint8_t * data, int len, int block)
  {
  int bytes_to_read;
  int bytes_read = 0;
  int to;
  int result;
  
  if(h->chunk_error || h->chunk_eof)
    return 0;

  if(block)
    to = h->opt->read_timeout;
  else
    to = 0;
  
  while(bytes_read < len)
    {
    if(h->chunk_pos >= h->chunk_size)
      {
      if(!next_chunk(h, block))
        return bytes_read;
      }
    
    bytes_to_read = len - bytes_read;
    
    if(bytes_to_read > h->chunk_size - h->chunk_pos)
      bytes_to_read = h->chunk_size - h->chunk_pos;
    
    result = bgav_read_data_fd(h->opt, h->fd, data + bytes_read, bytes_to_read, to);

    if(result < 0)
      {
      h->chunk_error = 1;
      return bytes_read;
      }
    
    bytes_read += result;
    h->chunk_pos += result;

    if(!block)
      break;
    
    }
  return bytes_read;
  }

static int read_normal(bgav_http_t * h, uint8_t * data, int len, int block)
  {
  int to;

  if(block)
    to = h->opt->read_timeout;
  else
    to = 0;
  return bgav_read_data_fd(h->opt, h->fd, data, len, to);
  }

int bgav_http_read(bgav_http_t * h, uint8_t * data, int len, int block)
  {
  if(h->chunked)
    return read_chunked(h, data, len, block);
  else
    return read_normal(h, data, len, block);
  }

static char const * const title_vars[] =
  {
    "icy-name",
    "ice-name",
    "x-audiocast-name",
    NULL
  };

static char const * const genre_vars[] =
  {
    "x-audiocast-genre",
    "icy-genre",
    "ice-genre",
    NULL
  };

static char const * const comment_vars[] =
  {
    "ice-description",
    "x-audiocast-description",
    NULL
  };

static char const * const url_vars[] =
  {
    "icy-url",
    NULL
  };

static void set_metadata_string(bgav_http_header_t * header,
                                char const * const vars[],
                                gavl_dictionary_t * m, const char * name)
  {
  const char * val;
  int i = 0;
  while(vars[i])
    {
    val = bgav_http_header_get_var(header, vars[i]);
    if(val)
      {
      gavl_dictionary_set_string(m, name, val);
      return;
      }
    else
      i++;
    }
  }

void bgav_http_set_metadata(bgav_http_t * h, gavl_dictionary_t * m)
  {
  int bitrate = 0;
  const char * var;
  
  var = bgav_http_header_get_var(h->header, "Content-Type");
  if(var)
    {
    /* Special hack for radio-browser.info */
    if(!strcasecmp(var, "application/octet-stream"))
      {
      if((var = bgav_http_header_get_var(h->header, "Content-Disposition")) &&
         (gavl_string_starts_with(var, "attachment;")) &&
         (var = strstr(var, "filename=")) &&
         (var = strrchr(var, '.')))
        {
        if(!strcmp(var, ".pls"))
          gavl_dictionary_set_string(gavl_dictionary_get_src_nc(m, GAVL_META_SRC, 0), GAVL_META_MIMETYPE,
                                     "audio/x-scpls");
        else if(!strcmp(var, ".m3u"))
          gavl_dictionary_set_string(gavl_dictionary_get_src_nc(m, GAVL_META_SRC, 0), GAVL_META_MIMETYPE,
                                     "audio/x-mpegurl");
        }
      }
    else
      gavl_dictionary_set_string(gavl_dictionary_get_src_nc(m, GAVL_META_SRC, 0), GAVL_META_MIMETYPE, var);
    }
  else if(bgav_http_header_get_var(h->header, "icy-notice1"))
    gavl_dictionary_set_string(gavl_dictionary_get_src_nc(m, GAVL_META_SRC, 0), GAVL_META_MIMETYPE, "audio/mpeg");

  
  
  /* Get Metadata */
  
  set_metadata_string(h->header,
                      title_vars, m, GAVL_META_STATION);
  set_metadata_string(h->header,
                      genre_vars, m, GAVL_META_GENRE);
  set_metadata_string(h->header,
                      comment_vars, m, GAVL_META_COMMENT);
  set_metadata_string(h->header,
                      url_vars, m, GAVL_META_RELURL);
  
  if((var = bgav_http_header_get_var(h->header, "icy-br")))
    bitrate = atoi(var);
  else if((var = bgav_http_header_get_var(h->header, "ice-audio-info")))
    {
    var = strstr(var, "bitrate=");
    if(var)
      bitrate = atoi(var + 8);
    }
  if(bitrate)
    gavl_dictionary_set_int(m, GAVL_META_BITRATE, bitrate * 1000);
  }

int64_t bgav_http_total_bytes(bgav_http_t * h)
  {
  return h->content_length;
  }

int bgav_http_can_seek(bgav_http_t * h)
  {
  return h->can_seek;
  }

