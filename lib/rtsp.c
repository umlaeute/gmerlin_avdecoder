/*****************************************************************
 
  in_rtsp.c
 
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

#include <avdec_private.h>

#include <rtsp.h>
#include <sdp.h>

#include <http.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



/* Real specific stuff */

#include <rmff.h>

struct bgav_rtsp_s
  {
  int fd;
  int cseq;
  
  char * user_agent;
  char * session;
  char * url;

  bgav_http_header_t * answers;
  bgav_http_header_t * request_fields;
  
  bgav_sdp_t sdp;
  
  int connect_timeout;
  int read_timeout;
  
  };

void bgav_rtsp_set_connect_timeout(bgav_rtsp_t * r, int timeout)
  {
  r->connect_timeout = timeout;
  }

void bgav_rtsp_set_read_timeout(bgav_rtsp_t * r, int timeout)
  {
  r->read_timeout = timeout;
  }

void bgav_rtsp_set_network_bandwidth(bgav_rtsp_t * r, int bandwidth)
  {
  //  r->network_bandwidth = bandwidth;
  }

void bgav_rtsp_set_user_agent(bgav_rtsp_t * r, const char * user_agent)
  {
  if(r->user_agent)
    free(r->user_agent);
  r->user_agent = bgav_strndup(user_agent, NULL);
  }

// #define DUMP_REQUESTS

static int rtsp_send_request(bgav_rtsp_t * rtsp,
                             const char * command,
                             const char * what,
                             int * got_redirected)
  {
  const char * var;
  int status;
  char * line = (char*)0;
  rtsp->cseq++;
  line = bgav_sprintf("%s %s RTSP/1.0\r\n", command, what);
#ifdef DUMP_REQUESTS
  fprintf(stderr, "%s", line);
#endif  
  if(!bgav_tcp_send(rtsp->fd, line, strlen(line)))
    goto fail;

  free(line);
  
  line = bgav_sprintf("CSeq: %u\r\n", rtsp->cseq);
#ifdef DUMP_REQUESTS
  fprintf(stderr, "%s", line);
#endif  
  write(rtsp->fd, line, strlen(line));
  free(line);

  if(rtsp->session)
    {
    line = bgav_sprintf("Session: %s\r\n", rtsp->session);
#ifdef DUMP_REQUESTS
    fprintf(stderr, "%s", line);
#endif  
    write(rtsp->fd, line, strlen(line));
    free(line);
    }
 
  bgav_http_header_send(rtsp->request_fields, rtsp->fd);
  if(!bgav_tcp_send(rtsp->fd, "\r\n\r\n", 4))
    goto fail;

#ifdef DUMP_REQUESTS
  bgav_http_header_dump(rtsp->request_fields);
#endif  
  
  bgav_http_header_reset(rtsp->request_fields);
  
  /* Read answers */
  bgav_http_header_reset(rtsp->answers);
  bgav_http_header_revc(rtsp->answers, rtsp->fd, rtsp->read_timeout);

  /* Handle redirection */
  
  if(strstr(rtsp->answers->lines[0].line, "REDIRECT"))
    {
    free(rtsp->url);
    rtsp->url =
      bgav_strndup(bgav_http_header_get_var(rtsp->answers,"Location"),
                   NULL);
    //    fprintf(stderr, "Got redirected to: %s\n", rtsp->url);
    if(got_redirected)
      *got_redirected = 1;
    return 1;
    }

  /* Get the server status */

  status = bgav_http_header_status_code(rtsp->answers);
  if(status != 200)
    {
    fprintf(stderr, "Got status %d\n", status);
    goto fail;
    }
  
#ifdef DUMP_REQUESTS
  fprintf(stderr, "Got answer:\n");
  bgav_http_header_dump(rtsp->answers);
#endif  
  
  //  bgav_http_header_dump(rtsp->answers);

  var = bgav_http_header_get_var(rtsp->answers, "Session");
  if(var && !(rtsp->session)) 
    rtsp->session = bgav_strndup(var, NULL);
  return 1;
  
  fail:
  return 0;
  }

void bgav_rtsp_schedule_field(bgav_rtsp_t * rtsp, const char * field)
  {
  bgav_http_header_add_line(rtsp->request_fields, field);
  }

const char * bgav_rtsp_get_answer(bgav_rtsp_t * rtsp, const char * name)
  {
  return bgav_http_header_get_var(rtsp->answers, name);
  }

int bgav_rtsp_request_describe(bgav_rtsp_t *rtsp, int * got_redirected)
  {
  int content_length;
  const char * var;
  char * buf = (char*)0;
  
  /* Send the "DESCRIBE" request */
  
  if(!rtsp_send_request(rtsp, "DESCRIBE", rtsp->url, got_redirected))
    goto fail;

  if(got_redirected && *got_redirected)
    {
    return 1;
    }
  var = bgav_http_header_get_var(rtsp->answers, "Content-Length");
  if(!var)
    goto fail;
  
  content_length = atoi(var);
    
  //  fprintf(stderr, "Content-length: %d\n", content_length);
  
  buf = malloc(content_length+1);
    
  if(bgav_read_data_fd(rtsp->fd, buf, content_length, rtsp->read_timeout) <
     content_length)
    {
    fprintf(stderr, "Reading session dscription failed\n");
    goto fail;
    }

  buf[content_length] = '\0';
  //  fprintf(stderr, "Session description: ");
  //  fwrite(buf, 1, content_length, stderr);
  
  if(!bgav_sdp_parse(buf, &(rtsp->sdp)))
    goto fail;

  free(buf);

  return 1;
  
  fail:

  if(buf)
    free(buf);
  return 0;
  }

int bgav_rtsp_request_setup(bgav_rtsp_t *r, const char *what)
  {
  return rtsp_send_request(r,"SETUP",what, NULL);
  }

int bgav_rtsp_request_setparameter(bgav_rtsp_t * r)
  {
  return rtsp_send_request(r,"SET_PARAMETER",r->url, NULL);
  }

int bgav_rtsp_request_play(bgav_rtsp_t * r)
  {
  return rtsp_send_request(r,"PLAY",r->url, NULL);
  }

/*
 *  Open connection to the server, get options and handle redirections
 *
 *  Return FALSE if error, TRUE on success.
 *  If we got redirectred, TRUE is returned and got_redirected
 *  is set to 1. The new URL is copied to the rtsp structure
 */

static int do_connect(bgav_rtsp_t * rtsp,
                      int * got_redirected)
  {
  int port = -1;
  char * host = (char*)0;
  char * protocol = (char*)0;
  int ret = 0;
  if(!bgav_url_split(rtsp->url, &protocol, &host,
                     &port, (char**)0))
    goto done;
  
  if(strcmp(protocol, "rtsp"))
    goto done;

  if(port == -1)
    port = 554;

  //  rtsp->cseq = 1;
  rtsp->fd = bgav_tcp_connect(host, port, rtsp->read_timeout);
  if(rtsp->fd < 0)
    goto done;
 
  if(!rtsp_send_request(rtsp, "OPTIONS", rtsp->url, got_redirected))
    goto done;
  
  ret = 1;

  done:

  if(!ret && (rtsp->fd >= 0))
    close(rtsp->fd);
  
  if(host)
    free(host);
  if(protocol)
    free(protocol);
  return ret;
  }

bgav_rtsp_t * bgav_rtsp_create()
  {
  bgav_rtsp_t * ret = (bgav_rtsp_t*)0;
  ret = calloc(1, sizeof(*ret));
  ret->answers = bgav_http_header_create();
  ret->request_fields = bgav_http_header_create();
  ret->fd = -1;
  return ret;
  }


int bgav_rtsp_open(bgav_rtsp_t * rtsp, const char * url, int * got_redirected)
  {
  //  fprintf(stderr, "bgav_rtsp_open %s\n", rtsp->url);
  if(url)
    rtsp->url = bgav_strndup(url, NULL);
  //  fprintf(stderr, "*** BGAV_RTSP_OPEN %s %s\n", url, rtsp->url);
  return do_connect(rtsp, got_redirected);
  }

bgav_sdp_t * bgav_rtsp_get_sdp(bgav_rtsp_t * r)
  {
  return &(r->sdp);
  }


void bgav_rtsp_close(bgav_rtsp_t * r)
  {
  bgav_http_header_destroy(r->answers);
  bgav_http_header_destroy(r->request_fields);
  bgav_sdp_free(&(r->sdp));
  if(r->user_agent) free(r->user_agent);
  if(r->url) free(r->url);
  if(r->session) free(r->session);

  if(r->fd > 0)
    close(r->fd);
  
  free(r);
  }

int bgav_rtsp_get_fd(bgav_rtsp_t * r)
  {
  return r->fd;
  }

const char * bgav_rtsp_get_url(bgav_rtsp_t * r)
  {
  return r->url;
  }
