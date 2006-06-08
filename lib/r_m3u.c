/*****************************************************************
 
  r_ram.c
 
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
  /* Most likely, we get this via http, so we can check the mimetype */
  if(input->mimetype)
    {
    if(!strcmp(input->mimetype, "audio/x-pn-realaudio-plugin") ||
       !strcmp(input->mimetype, "video/x-pn-realvideo-plugin") ||
       !strcmp(input->mimetype, "audio/x-pn-realaudio") ||
       !strcmp(input->mimetype, "video/x-pn-realvideo") ||
       !strcmp(input->mimetype, "audio/x-mpegurl") ||
       !strcmp(input->mimetype, "audio/mpegurl") ||
       !strcmp(input->mimetype, "audio/m3u"))
      {
      if(bgav_input_get_data(input, (uint8_t*)probe_buffer,
                             PROBE_BYTES) < PROBE_BYTES)
        return 0;

      /* Some streams with the above mimetype are in realtiy
         different streams, so we check this here */
            
      if(!strncmp(probe_buffer, "mms://", 6) ||
         !strncmp(probe_buffer, "http://", 7) ||
         !strncmp(probe_buffer, "rtsp://", 7) ||
         (probe_buffer[0] == '#'))
        return 1;
      }
    }
  /* Take all files which end with .m3u */
  
  if(input->filename)
    {
    pos = strrchr(input->filename, '.');
    if(!pos)
      return 0;
    if(!strcmp(pos, ".m3u"))
      return 1;
    }

  
  if(bgav_input_get_data(input, (uint8_t*)probe_buffer,
                         PROBE_BYTES) < PROBE_BYTES)
    return 0;

  if(!strncmp(probe_buffer, "mms://", 6) ||
     !strncmp(probe_buffer, "http://", 7) ||
     !strncmp(probe_buffer, "rtsp://", 7))
    return 1;
  return 0;
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
  end = &(ret[strlen(ret)-1]);
  while(isspace(*end) && (end > ret))
    end--;
  end++;
  *end = '\0';
  return ret;
  }

static int parse_m3u(bgav_redirector_context_t * r)
  {
  char * buffer = (char*)0;
  int buffer_alloc = 0;
  char * pos;

  //  fprintf(stderr, "parse_m3u\n");
  //  bgav_input_get_dump(r->input, 16);
    
  while(1)
    {
    if(!bgav_input_read_line(r->input, &buffer, &buffer_alloc, 0, (int*)0))
      break;
    pos = strip_spaces(buffer);
    //    fprintf(stderr, "Got line: %s\n", pos);
    if((*pos == '#') || (*pos == '\0'))
      continue;
    if(!strcmp(pos, "--stop--"))
      break;

    else
      {
      add_url(r);
      r->urls[r->num_urls-1].url = bgav_strdup(pos);
      }
    }

  if(buffer)
    free(buffer);
  return !!(r->num_urls);
  }

bgav_redirector_t bgav_redirector_m3u = 
  {
    name:  "m3u/ram",
    probe: probe_m3u,
    parse: parse_m3u
  };
