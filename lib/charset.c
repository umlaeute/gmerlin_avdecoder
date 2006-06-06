/*****************************************************************
 
  charset.c
 
  Copyright (c) 2003-2006 by Burkhard Plaum - plaum@ipf.uni-stuttgart.de
 
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

#include <iconv.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

struct bgav_charset_converter_s
  {
  iconv_t cd;
  };
  

bgav_charset_converter_t *
bgav_charset_converter_create(const char * in_charset,
                              const char * out_charset)
  {
  bgav_charset_converter_t * ret = calloc(1, sizeof(*ret));
  ret->cd = iconv_open(out_charset, in_charset);
  return ret;
  }

void bgav_charset_converter_destroy(bgav_charset_converter_t * cnv)
  {
  iconv_close(cnv->cd);
  free(cnv);
  }

/*

void bgav_charset_converter_destroy(bgav_charset_converter_t *);

char * bgav_convert_string(bgav_charset_converter_t *,
                           const char * in_string, int in_len,
                           int * out_len);
*/

#define BYTES_INCREMENT 10

static int do_convert(iconv_t cd, char * in_string, int len, int * out_len,
               char ** ret, int * ret_alloc)
  {

  char *inbuf;
  char *outbuf;
  int output_pos;
  
  size_t inbytesleft;
  size_t outbytesleft;

  if(!(*ret_alloc) < len + BYTES_INCREMENT)
    *ret_alloc = len + BYTES_INCREMENT;
  
  inbytesleft  = len;
  outbytesleft = *ret_alloc;

  *ret    = realloc(*ret, *ret_alloc);
  inbuf  = in_string;
  outbuf = *ret;
  
  while(1)
    {
    if(iconv(cd, &inbuf, &inbytesleft,
             &outbuf, &outbytesleft) == (size_t)-1)
      {
      switch(errno)
        {
        case E2BIG:
          output_pos = (int)(outbuf - *ret);

          *ret_alloc   += BYTES_INCREMENT;
          outbytesleft += BYTES_INCREMENT;

          *ret = realloc(*ret, *ret_alloc);
          outbuf = &((*ret)[output_pos]);
          break;
        case EILSEQ:
          fprintf(stderr, "Invalid Multibyte sequence\n");
          return 0;
          break;
        case EINVAL:
          fprintf(stderr, "Incomplete Multibyte sequence\n");
          return 0;
          break;
        }
      }
    if(!inbytesleft)
      break;
    }
  /* Zero terminate */

  output_pos = (int)(outbuf - *ret);
  
  if(outbytesleft < 2)
    {
    *ret_alloc+=2;
    *ret = realloc(*ret, *ret_alloc);
    outbuf = &((*ret)[output_pos]);
    }
  outbuf[0] = '\0';
  outbuf[1] = '\0';
  if(out_len)
    *out_len = outbuf - *ret;
  return 1;
  }

char * bgav_convert_string(bgav_charset_converter_t * cnv,
                           const char * str, int len,
                           int * out_len)
  {
  int result;
  char * ret = (char*)0;
  int ret_alloc = 0;
  char * tmp_string;

  if(len < 0)
    len = strlen(str);

  tmp_string = malloc(len+1);
  memcpy(tmp_string, str, len);
  tmp_string[len] = '\0';
  result = do_convert(cnv->cd, tmp_string, len, out_len, &ret, &ret_alloc);
  free(tmp_string);
  
  if(!result)
    {
    return (char*)0;
    if(ret)
      free(ret);
    }
  return ret;
  }

int bgav_convert_string_realloc(bgav_charset_converter_t * cnv,
                                const char * str, int len,
                                int * out_len,
                                char ** ret, int * ret_alloc)
  {
  int result;
  char * tmp_string;
  if(len < 0)
    len = strlen(str);
  
  tmp_string = malloc(len+1);
  memcpy(tmp_string, str, len);
  tmp_string[len] = '\0';
  result = do_convert(cnv->cd, tmp_string, len, out_len, ret, ret_alloc);
  free(tmp_string);
  
  return result;
  }

/* Charset detection. This detects UTF-8 and UTF-16 for now */

static int utf8_validate(const uint8_t * str)
  {
  while(1)
    {
    if(*str == '\0')
      return 1;
    /* 0xxxxxxx */
    if(!(str[0] & 0x80))
      str++;

    /* 110xxxxx 10xxxxxx */
    else if((str[0] & 0xe0) == 0xc0)
      {
      if((str[1] & 0xc0) == 0x80)
        str+=2;
      else
        return 0;
      }
    
    /* 1110xxxx 10xxxxxx 10xxxxxx */
    else if((str[0] & 0xf0) == 0xe0)
      {
      if(((str[1] & 0xc0) == 0x80) &&
         ((str[2] & 0xc0) == 0x80))
        str+=3;
      else
        return 0;
      }
    /* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */

    else if((str[0] & 0xf8) == 0xf0)
      {
      if(((str[1] & 0xc0) == 0x80) &&
         ((str[2] & 0xc0) == 0x80) &&
         ((str[3] & 0xc0) == 0x80))
        str+=4;
      else
        return 0;
      }
    else
      return 0;
    }
  return 1;
  }

void bgav_input_detect_charset(bgav_input_context_t * ctx)
  {
  char * line = (char*)0;
  int line_alloc = 0;
  
  int64_t old_position;
  uint8_t first_bytes[2];
  
  /* We need byte accurate seeking */
  if(!ctx->input->seek_byte || !ctx->total_bytes || ctx->charset)
    return;

  old_position = ctx->position;
  
  bgav_input_seek(ctx, 0, SEEK_SET);

  if(bgav_input_get_data(ctx, first_bytes, 2) < 2)
    return;

  if((first_bytes[0] == 0xff) && (first_bytes[1] == 0xfe))
    {
    ctx->charset = bgav_strdup("UTF-16LE");
    bgav_input_seek(ctx, old_position, SEEK_SET);
    return;
    }
  else if((first_bytes[0] == 0xfe) && (first_bytes[1] == 0xff))
    {
    ctx->charset = bgav_strdup("UTF-16BE");
    bgav_input_seek(ctx, old_position, SEEK_SET);
    return;
    }
  else
    {
    while(bgav_input_read_line(ctx, &line, &line_alloc, 0, (int*)0))
      {
      if(!utf8_validate((uint8_t*)line))
        {
        bgav_input_seek(ctx, old_position, SEEK_SET);
        if(line) free(line);
        return;
        }
      }
    ctx->charset = bgav_strdup("UTF-8");
    bgav_input_seek(ctx, old_position, SEEK_SET);
    if(line) free(line);
    return;
    }
  bgav_input_seek(ctx, old_position, SEEK_SET);
  if(line) free(line);
  }
