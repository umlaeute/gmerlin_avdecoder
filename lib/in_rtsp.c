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

#include <stdlib.h>
#include <string.h>

#include <avdec_private.h>
#include <rtsp.h>
#include <rmff.h>
#include <bswap.h>

#define SERVER_TYPE_UNKNOWN 0
#define SERVER_TYPE_REAL    1

extern bgav_demuxer_t bgav_demuxer_rmff;

/* See bottom of this file */

static void real_calc_response_and_checksum (char *response, char *chksum, char *challenge);


typedef struct rtsp_priv_s
  {
  int type;
  char * challenge1;
  bgav_rtsp_t * r;
  bgav_rmff_header_t * rmff_header;
  
  /* Packet */
    
  uint8_t * packet;
  uint8_t * packet_ptr;
  int       packet_len;
  int packet_alloc;
  
  /* Function for getting the next packet */
  
  int (*next_packet)(bgav_input_context_t * ctx, int block);
  
  } rtsp_priv_t;

static void packet_alloc(rtsp_priv_t * priv, int size)
  {
  if(priv->packet_alloc < size)
    {
    priv->packet_alloc = size + 64;
    priv->packet = realloc(priv->packet, priv->packet_alloc);
    }
  priv->packet_ptr = priv->packet;
  priv->packet_len = size;
  }

/* Get one RDT packet */

int next_packet_rdt(bgav_input_context_t * ctx, int block)
  {
  int size;
  int flags1;
  uint32_t timestamp;
  bgav_rmff_packet_header_t ph;
  //  int unknown1;

  char * buf;
    
  int fd;
  uint8_t header[8];
  int seq;
  rtsp_priv_t * priv = (rtsp_priv_t *)(ctx->priv);
  fd = bgav_rtsp_get_fd(priv->r);

  while(1)
    {
  
    if(block)
      {
      if(bgav_read_data_fd(fd, header, 8, ctx->read_timeout) < 8)
        return 0;
      }
    else
      {
      if(bgav_read_data_fd(fd, header, 4, 0) < 4)
        return 0;
      }

    /* Check for a ping request */
    
    if(!strncmp(header, "SET_PARA", 8))
      {
      //      fprintf(stderr, "Got Ping request\n");
    
      size = 0;

      seq = -1;
      while(1)
        {
        if(!bgav_read_line_fd(fd, (char**)(&(priv->packet)),
                              &(priv->packet_alloc),
                              ctx->read_timeout))
          return 0;
        size = strlen(priv->packet);
        if(!size)
          break;

        if (!strncmp(priv->packet,"Cseq:",5))
          sscanf(priv->packet,"Cseq: %u",&seq);
        }
      if(seq < 0)
        seq = 1;
      /* Make the server happy */

      bgav_tcp_send(fd, "RTSP/1.0 451 Parameter Not Understood\r\n",
                    strlen("RTSP/1.0 451 Parameter Not Understood\r\n"));
      buf = bgav_sprintf("CSeq: %u\r\n\r\n", seq);
      bgav_tcp_send(fd, buf, strlen(buf));
      free(buf);
      }
    else if(header[0] == '$')
      {
      //    fprintf(stderr, "Got RDT header\n");
      //    bgav_hexdump(header, 8, 8);
      size = BGAV_PTR_2_16BE(&(header[2]));
    
      flags1 = header[4];

      if ((flags1!=0x40)&&(flags1!=0x42))
        {
      
        //      fprintf(stderr, "got flags1: 0x%02x\n",flags1);
        header[0]=header[5];
        header[1]=header[6];
        header[2]=header[7];

        if(bgav_read_data_fd(fd, &(header[3]), 5, ctx->read_timeout) < 5)
          return 0;

        if(bgav_read_data_fd(fd, &(header[4]), 4, ctx->read_timeout) < 4)
          return 0;
        flags1 = header[4];
        size-=9;
        }

      if(bgav_read_data_fd(fd, header, 6, ctx->read_timeout) < 6)
        return 0;
      timestamp = BGAV_PTR_2_32BE(header);
      size+=2;

      ph.object_version=0;
      ph.length=size;
      ph.stream_number=(flags1>>1)&1;
      ph.timestamp=timestamp;
      ph.reserved=0;
      ph.flags=0;      /* TODO: determine keyframe flag and insert here? */

      //    bgav_rmff_packet_header_dump(&ph);


      packet_alloc(priv, size);
    
      size -= 12;
      bgav_rmff_packet_header_to_pointer(&ph, priv->packet);
      if(bgav_read_data_fd(fd, priv->packet + 12, size,
                           ctx->read_timeout) < size)
        return 0;
      return 1;
      }
    else
      {
      fprintf(stderr, "Unknown chunk\n");
      bgav_hexdump(header, 8, 8);
      return 0;
      }
    }
  
  return 0;
  }


static int open_and_describe(bgav_input_context_t * ctx, const char * url, int * got_redirected)
  {
  const char * var;
  rtsp_priv_t * priv = (rtsp_priv_t *)(ctx->priv);
  
  bgav_rtsp_set_connect_timeout(priv->r, ctx->connect_timeout);
  bgav_rtsp_set_read_timeout(priv->r, ctx->read_timeout);
  bgav_rtsp_set_network_bandwidth(priv->r, ctx->network_bandwidth);
  bgav_rtsp_set_user_agent(priv->r,
                           "RealMedia Player Version 6.0.9.1235 (linux-2.0-libc6-i386-gcc2.95)");

  /* Open URL */

  bgav_rtsp_schedule_field(priv->r,
                           "User-Agent: RealMedia Player Version 6.0.9.1235 (linux-2.0-libc6-i386-gcc2.95)");
  bgav_rtsp_schedule_field(priv->r,
                           "ClientChallenge: 9e26d33f2984236010ef6253fb1887f7");
  bgav_rtsp_schedule_field(priv->r,
                           "PlayerStarttime: [28/03/2003:22:50:23 00:00]");
  bgav_rtsp_schedule_field(priv->r,
                           "CompanyID: KnKV4M4I/B2FjJ1TToLycw==");
  bgav_rtsp_schedule_field(priv->r,
                           "GUID: 00000000-0000-0000-0000-000000000000");
  bgav_rtsp_schedule_field(priv->r,
                           "RegionData: 0");
  bgav_rtsp_schedule_field(priv->r,
                           "ClientID: Linux_2.4_6.0.9.1235_play32_RN01_EN_586");
  bgav_rtsp_schedule_field(priv->r,
                           "Pragma: initiate-session");

  if(!bgav_rtsp_open(priv->r, url, got_redirected))
    return 0;

  if(*got_redirected)
    return 1;
  
  /* Check wether this is a Real Server */

  var = bgav_rtsp_get_answer(priv->r, "RealChallenge1");
  if(var)
    {
    priv->challenge1 = bgav_strndup(var, NULL);
    priv->type = SERVER_TYPE_REAL;
    //    fprintf(stderr, "Real Server, challenge %s\n", challenge1);
    }

  if(priv->type == SERVER_TYPE_UNKNOWN)
    {
    fprintf(stderr, "Don't know how to handle this server\n");
    return 0;
    }
  
  bgav_rtsp_schedule_field(priv->r,
                           "Accept: application/sdp");
  bgav_rtsp_schedule_field(priv->r,
                           "Bandwidth: 10485800");
  bgav_rtsp_schedule_field(priv->r,
                           "GUID: 00000000-0000-0000-0000-000000000000");
  bgav_rtsp_schedule_field(priv->r,
                           "RegionData: 0");
  bgav_rtsp_schedule_field(priv->r,
                           "ClientID: Linux_2.4_6.0.9.1235_play32_RN01_EN_586");
  bgav_rtsp_schedule_field(priv->r,
                           "SupportsMaximumASMBandwidth: 1");
  bgav_rtsp_schedule_field(priv->r,
                           "Language: en-US");
  bgav_rtsp_schedule_field(priv->r,
                           "Require: com.real.retain-entity-for-setup");
    
  if(!bgav_rtsp_request_describe(priv->r, got_redirected))
    return 0;
  
  return 1;
  }

static int open_rtsp(bgav_input_context_t * ctx, const char * url)
  {
  rtsp_priv_t * priv;
  bgav_sdp_t * sdp;
  char * stream_rules = (char*)0;
  const char * var;
  char * field;
  char * session_id = (char*)0;
  char challenge2[64];
  char checksum[34];
  int num_redirections = 0;
  int got_redirected = 0;
  
  priv = calloc(1, sizeof(*priv));
  priv->r = bgav_rtsp_create();

  ctx->priv = priv;

  while(num_redirections < 5)
    {
    got_redirected = 0;

    if(num_redirections)
      {
      if(!open_and_describe(ctx, NULL, &got_redirected))
        return 0;
      }
    else
      {
      if(!open_and_describe(ctx, url, &got_redirected))
        return 0;
      }
    if(got_redirected)
      {
      //      fprintf(stderr, "Got redirected to: %s\n", bgav_rtsp_get_url(priv->r));
      num_redirections++;
      }
    else
      break;
    }

  if(num_redirections == 5)
    goto fail;
  
  var = bgav_rtsp_get_answer(priv->r,"ETag");
  if(!var)
    {
    fprintf(stderr, "real: got no ETag!\n");
    goto fail;
    }
  else
    session_id=bgav_strndup(var, NULL);
  
  sdp = bgav_rtsp_get_sdp(priv->r);
  
  switch(priv->type)
    {
    case SERVER_TYPE_REAL:
      priv->rmff_header =
        bgav_rmff_header_create_from_sdp(sdp,
                                         ctx->network_bandwidth,
                                         &stream_rules);
      if(!priv->rmff_header)
        goto fail;
      ctx->demuxer = bgav_demuxer_create(&bgav_demuxer_rmff, ctx);
      if(!bgav_demux_rm_open_with_header(ctx->demuxer,
                                         priv->rmff_header))
        return 0;
      priv->next_packet = next_packet_rdt;
      break;
    }
  
  //  fprintf(stderr, "Stream rules: %s\n", stream_rules);
  
  /* Setup */
  
  real_calc_response_and_checksum(challenge2, checksum, priv->challenge1);

  field = bgav_sprintf("RealChallenge2: %s, sd=%s", challenge2, checksum);
  bgav_rtsp_schedule_field(priv->r, field);free(field);
  
  field = bgav_sprintf("If-Match: %s", session_id);
  bgav_rtsp_schedule_field(priv->r, field);free(field);
  bgav_rtsp_schedule_field(priv->r, "Transport: x-pn-tng/tcp;mode=play,rtp/avp/tcp;unicast;mode=play");
  field = bgav_sprintf("%s/streamid=0", url);

  if(!bgav_rtsp_request_setup(priv->r,field))
    {
    free(field);
    goto fail;
    }
  free(field);
  
  if(priv->rmff_header->prop.num_streams > 1)
    {
    field = bgav_sprintf("If-Match: %s", session_id);
    bgav_rtsp_schedule_field(priv->r, field);free(field);
    bgav_rtsp_schedule_field(priv->r, "Transport: x-pn-tng/tcp;mode=play,rtp/avp/tcp;unicast;mode=play");
    field = bgav_sprintf("%s/streamid=1", url);
    bgav_rtsp_request_setup(priv->r,field);free(field);
    }

  /* Set Parameter */

  field = bgav_sprintf("Subscribe: %s", stream_rules);
  bgav_rtsp_schedule_field(priv->r, field);free(field);
  if(!bgav_rtsp_request_setparameter(priv->r))
    goto fail;
  /* Play */

  bgav_rtsp_schedule_field(priv->r, "Range: npt=0-");
  if(!bgav_rtsp_request_play(priv->r))
    goto fail;
  
  ctx->do_buffer = 1;

  if(stream_rules)
    free(stream_rules);
  if(priv->challenge1)
    {
    free(priv->challenge1);
    priv->challenge1 = (char*)0;
    }
  if(session_id)
    free(session_id);
  
  return 1;

  fail:
  if(stream_rules)
    free(stream_rules);
  if(priv->challenge1)
    {
    free(priv->challenge1);
    priv->challenge1 = (char*)0;
    }
  if(session_id)
    free(session_id);
  free(priv);
  ctx->priv = (void*)0;
  return 0;
  }

static void close_rtsp(bgav_input_context_t * ctx)
  {
  rtsp_priv_t * priv;
  priv = (rtsp_priv_t*)(ctx->priv);
  if(!priv)
    return;
  if(priv->r)
    bgav_rtsp_close(priv->r);

  /* Header is always destroyed by the demuxer */
  //  if(priv->rmff_header)
  //    bgav_rmff_header_destroy(priv->rmff_header);

  if(priv->packet)
    free(priv->packet);
  
  free(priv);
  }

static int do_read(bgav_input_context_t* ctx,
                   uint8_t * buffer, int len, int block)
  {
  int bytes_read;
  rtsp_priv_t * priv;
  int bytes_to_copy;
  priv = (rtsp_priv_t*)(ctx->priv);
  //  fprintf(stderr, "do read %d\n", len);
  bytes_read = 0;
  while(bytes_read < len)
    {
    if(!priv->packet_len)
      {
      if(!priv->next_packet(ctx, block))
        return bytes_read;
      }

    bytes_to_copy = (priv->packet_len < (len - bytes_read)) ?
      priv->packet_len : (len - bytes_read);
    memcpy(buffer + bytes_read, priv->packet_ptr, bytes_to_copy);

    bytes_read += bytes_to_copy;
    priv->packet_ptr += bytes_to_copy;
    priv->packet_len -= bytes_to_copy;
    }
  return bytes_read;
  }

static int read_rtsp(bgav_input_context_t* ctx,
                     uint8_t * buffer, int len)
  {
  return do_read(ctx, buffer, len, 1);
  }

static int read_nonblock_rtsp(bgav_input_context_t * ctx,
                              uint8_t * buffer, int len)
  {
  return do_read(ctx, buffer, len, 1);
  }


bgav_input_t bgav_input_rtsp =
  {
    open:          open_rtsp,
    read:          read_rtsp,
    read_nonblock: read_nonblock_rtsp,
    close:         close_rtsp,
  };

/* The following is ported from MPlayer */

/* This is some really ugly code moved here not to disturb the reader */

static const unsigned char xor_table[] = {
    0x05, 0x18, 0x74, 0xd0, 0x0d, 0x09, 0x02, 0x53,
    0xc0, 0x01, 0x05, 0x05, 0x67, 0x03, 0x19, 0x70,
    0x08, 0x27, 0x66, 0x10, 0x10, 0x72, 0x08, 0x09,
    0x63, 0x11, 0x03, 0x71, 0x08, 0x08, 0x70, 0x02,
    0x10, 0x57, 0x05, 0x18, 0x54, 0x00, 0x00, 0x00 };


#define BE_32C(x,y) (*((uint32_t*)(x))=be2me_32(y))

static void hash(char *field, char *param) {

  uint32_t a, b, c, d;
 

  /* fill variables */
  a= le2me_32(*(uint32_t*)(field));
  b= le2me_32(*(uint32_t*)(field+4));
  c= le2me_32(*(uint32_t*)(field+8));
  d= le2me_32(*(uint32_t*)(field+12));

#ifdef LOG
  printf("real: hash input: %x %x %x %x\n", a, b, c, d);
  printf("real: hash parameter:\n");
  hexdump(param, 64);
  printf("real: hash field:\n");
  hexdump(field, 64+24);
#endif
  
  a = ((b & c) | (~b & d)) + le2me_32(*((uint32_t*)(param+0x00))) + a - 0x28955B88;
  a = ((a << 0x07) | (a >> 0x19)) + b;
  d = ((a & b) | (~a & c)) + le2me_32(*((uint32_t*)(param+0x04))) + d - 0x173848AA;
  d = ((d << 0x0c) | (d >> 0x14)) + a;
  c = ((d & a) | (~d & b)) + le2me_32(*((uint32_t*)(param+0x08))) + c + 0x242070DB;
  c = ((c << 0x11) | (c >> 0x0f)) + d;
  b = ((c & d) | (~c & a)) + le2me_32(*((uint32_t*)(param+0x0c))) + b - 0x3E423112;
  b = ((b << 0x16) | (b >> 0x0a)) + c;
  a = ((b & c) | (~b & d)) + le2me_32(*((uint32_t*)(param+0x10))) + a - 0x0A83F051;
  a = ((a << 0x07) | (a >> 0x19)) + b;
  d = ((a & b) | (~a & c)) + le2me_32(*((uint32_t*)(param+0x14))) + d + 0x4787C62A;
  d = ((d << 0x0c) | (d >> 0x14)) + a;
  c = ((d & a) | (~d & b)) + le2me_32(*((uint32_t*)(param+0x18))) + c - 0x57CFB9ED;
  c = ((c << 0x11) | (c >> 0x0f)) + d;
  b = ((c & d) | (~c & a)) + le2me_32(*((uint32_t*)(param+0x1c))) + b - 0x02B96AFF;
  b = ((b << 0x16) | (b >> 0x0a)) + c;
  a = ((b & c) | (~b & d)) + le2me_32(*((uint32_t*)(param+0x20))) + a + 0x698098D8;
  a = ((a << 0x07) | (a >> 0x19)) + b;
  d = ((a & b) | (~a & c)) + le2me_32(*((uint32_t*)(param+0x24))) + d - 0x74BB0851;
  d = ((d << 0x0c) | (d >> 0x14)) + a;
  c = ((d & a) | (~d & b)) + le2me_32(*((uint32_t*)(param+0x28))) + c - 0x0000A44F;
  c = ((c << 0x11) | (c >> 0x0f)) + d;
  b = ((c & d) | (~c & a)) + le2me_32(*((uint32_t*)(param+0x2C))) + b - 0x76A32842;
  b = ((b << 0x16) | (b >> 0x0a)) + c;
  a = ((b & c) | (~b & d)) + le2me_32(*((uint32_t*)(param+0x30))) + a + 0x6B901122;
  a = ((a << 0x07) | (a >> 0x19)) + b;
  d = ((a & b) | (~a & c)) + le2me_32(*((uint32_t*)(param+0x34))) + d - 0x02678E6D;
  d = ((d << 0x0c) | (d >> 0x14)) + a;
  c = ((d & a) | (~d & b)) + le2me_32(*((uint32_t*)(param+0x38))) + c - 0x5986BC72;
  c = ((c << 0x11) | (c >> 0x0f)) + d;
  b = ((c & d) | (~c & a)) + le2me_32(*((uint32_t*)(param+0x3c))) + b + 0x49B40821;
  b = ((b << 0x16) | (b >> 0x0a)) + c;
  
  a = ((b & d) | (~d & c)) + le2me_32(*((uint32_t*)(param+0x04))) + a - 0x09E1DA9E;
  a = ((a << 0x05) | (a >> 0x1b)) + b;
  d = ((a & c) | (~c & b)) + le2me_32(*((uint32_t*)(param+0x18))) + d - 0x3FBF4CC0;
  d = ((d << 0x09) | (d >> 0x17)) + a;
  c = ((d & b) | (~b & a)) + le2me_32(*((uint32_t*)(param+0x2c))) + c + 0x265E5A51;
  c = ((c << 0x0e) | (c >> 0x12)) + d;
  b = ((c & a) | (~a & d)) + le2me_32(*((uint32_t*)(param+0x00))) + b - 0x16493856;
  b = ((b << 0x14) | (b >> 0x0c)) + c;
  a = ((b & d) | (~d & c)) + le2me_32(*((uint32_t*)(param+0x14))) + a - 0x29D0EFA3;
  a = ((a << 0x05) | (a >> 0x1b)) + b;
  d = ((a & c) | (~c & b)) + le2me_32(*((uint32_t*)(param+0x28))) + d + 0x02441453;
  d = ((d << 0x09) | (d >> 0x17)) + a;
  c = ((d & b) | (~b & a)) + le2me_32(*((uint32_t*)(param+0x3c))) + c - 0x275E197F;
  c = ((c << 0x0e) | (c >> 0x12)) + d;
  b = ((c & a) | (~a & d)) + le2me_32(*((uint32_t*)(param+0x10))) + b - 0x182C0438;
  b = ((b << 0x14) | (b >> 0x0c)) + c;
  a = ((b & d) | (~d & c)) + le2me_32(*((uint32_t*)(param+0x24))) + a + 0x21E1CDE6;
  a = ((a << 0x05) | (a >> 0x1b)) + b;
  d = ((a & c) | (~c & b)) + le2me_32(*((uint32_t*)(param+0x38))) + d - 0x3CC8F82A;
  d = ((d << 0x09) | (d >> 0x17)) + a;
  c = ((d & b) | (~b & a)) + le2me_32(*((uint32_t*)(param+0x0c))) + c - 0x0B2AF279;
  c = ((c << 0x0e) | (c >> 0x12)) + d;
  b = ((c & a) | (~a & d)) + le2me_32(*((uint32_t*)(param+0x20))) + b + 0x455A14ED;
  b = ((b << 0x14) | (b >> 0x0c)) + c;
  a = ((b & d) | (~d & c)) + le2me_32(*((uint32_t*)(param+0x34))) + a - 0x561C16FB;
  a = ((a << 0x05) | (a >> 0x1b)) + b;
  d = ((a & c) | (~c & b)) + le2me_32(*((uint32_t*)(param+0x08))) + d - 0x03105C08;
  d = ((d << 0x09) | (d >> 0x17)) + a;
  c = ((d & b) | (~b & a)) + le2me_32(*((uint32_t*)(param+0x1c))) + c + 0x676F02D9;
  c = ((c << 0x0e) | (c >> 0x12)) + d;
  b = ((c & a) | (~a & d)) + le2me_32(*((uint32_t*)(param+0x30))) + b - 0x72D5B376;
  b = ((b << 0x14) | (b >> 0x0c)) + c;
  
  a = (b ^ c ^ d) + le2me_32(*((uint32_t*)(param+0x14))) + a - 0x0005C6BE;
  a = ((a << 0x04) | (a >> 0x1c)) + b;
  d = (a ^ b ^ c) + le2me_32(*((uint32_t*)(param+0x20))) + d - 0x788E097F;
  d = ((d << 0x0b) | (d >> 0x15)) + a;
  c = (d ^ a ^ b) + le2me_32(*((uint32_t*)(param+0x2c))) + c + 0x6D9D6122;
  c = ((c << 0x10) | (c >> 0x10)) + d;
  b = (c ^ d ^ a) + le2me_32(*((uint32_t*)(param+0x38))) + b - 0x021AC7F4;
  b = ((b << 0x17) | (b >> 0x09)) + c;
  a = (b ^ c ^ d) + le2me_32(*((uint32_t*)(param+0x04))) + a - 0x5B4115BC;
  a = ((a << 0x04) | (a >> 0x1c)) + b;
  d = (a ^ b ^ c) + le2me_32(*((uint32_t*)(param+0x10))) + d + 0x4BDECFA9;
  d = ((d << 0x0b) | (d >> 0x15)) + a;
  c = (d ^ a ^ b) + le2me_32(*((uint32_t*)(param+0x1c))) + c - 0x0944B4A0;
  c = ((c << 0x10) | (c >> 0x10)) + d;
  b = (c ^ d ^ a) + le2me_32(*((uint32_t*)(param+0x28))) + b - 0x41404390;
  b = ((b << 0x17) | (b >> 0x09)) + c;
  a = (b ^ c ^ d) + le2me_32(*((uint32_t*)(param+0x34))) + a + 0x289B7EC6;
  a = ((a << 0x04) | (a >> 0x1c)) + b;
  d = (a ^ b ^ c) + le2me_32(*((uint32_t*)(param+0x00))) + d - 0x155ED806;
  d = ((d << 0x0b) | (d >> 0x15)) + a;
  c = (d ^ a ^ b) + le2me_32(*((uint32_t*)(param+0x0c))) + c - 0x2B10CF7B;
  c = ((c << 0x10) | (c >> 0x10)) + d;
  b = (c ^ d ^ a) + le2me_32(*((uint32_t*)(param+0x18))) + b + 0x04881D05;
  b = ((b << 0x17) | (b >> 0x09)) + c;
  a = (b ^ c ^ d) + le2me_32(*((uint32_t*)(param+0x24))) + a - 0x262B2FC7;
  a = ((a << 0x04) | (a >> 0x1c)) + b;
  d = (a ^ b ^ c) + le2me_32(*((uint32_t*)(param+0x30))) + d - 0x1924661B;
  d = ((d << 0x0b) | (d >> 0x15)) + a;
  c = (d ^ a ^ b) + le2me_32(*((uint32_t*)(param+0x3c))) + c + 0x1fa27cf8;
  c = ((c << 0x10) | (c >> 0x10)) + d;
  b = (c ^ d ^ a) + le2me_32(*((uint32_t*)(param+0x08))) + b - 0x3B53A99B;
  b = ((b << 0x17) | (b >> 0x09)) + c;
  
  a = ((~d | b) ^ c)  + le2me_32(*((uint32_t*)(param+0x00))) + a - 0x0BD6DDBC;
  a = ((a << 0x06) | (a >> 0x1a)) + b; 
  d = ((~c | a) ^ b)  + le2me_32(*((uint32_t*)(param+0x1c))) + d + 0x432AFF97;
  d = ((d << 0x0a) | (d >> 0x16)) + a; 
  c = ((~b | d) ^ a)  + le2me_32(*((uint32_t*)(param+0x38))) + c - 0x546BDC59;
  c = ((c << 0x0f) | (c >> 0x11)) + d; 
  b = ((~a | c) ^ d)  + le2me_32(*((uint32_t*)(param+0x14))) + b - 0x036C5FC7;
  b = ((b << 0x15) | (b >> 0x0b)) + c; 
  a = ((~d | b) ^ c)  + le2me_32(*((uint32_t*)(param+0x30))) + a + 0x655B59C3;
  a = ((a << 0x06) | (a >> 0x1a)) + b; 
  d = ((~c | a) ^ b)  + le2me_32(*((uint32_t*)(param+0x0C))) + d - 0x70F3336E;
  d = ((d << 0x0a) | (d >> 0x16)) + a; 
  c = ((~b | d) ^ a)  + le2me_32(*((uint32_t*)(param+0x28))) + c - 0x00100B83;
  c = ((c << 0x0f) | (c >> 0x11)) + d; 
  b = ((~a | c) ^ d)  + le2me_32(*((uint32_t*)(param+0x04))) + b - 0x7A7BA22F;
  b = ((b << 0x15) | (b >> 0x0b)) + c; 
  a = ((~d | b) ^ c)  + le2me_32(*((uint32_t*)(param+0x20))) + a + 0x6FA87E4F;
  a = ((a << 0x06) | (a >> 0x1a)) + b; 
  d = ((~c | a) ^ b)  + le2me_32(*((uint32_t*)(param+0x3c))) + d - 0x01D31920;
  d = ((d << 0x0a) | (d >> 0x16)) + a; 
  c = ((~b | d) ^ a)  + le2me_32(*((uint32_t*)(param+0x18))) + c - 0x5CFEBCEC;
  c = ((c << 0x0f) | (c >> 0x11)) + d; 
  b = ((~a | c) ^ d)  + le2me_32(*((uint32_t*)(param+0x34))) + b + 0x4E0811A1;
  b = ((b << 0x15) | (b >> 0x0b)) + c; 
  a = ((~d | b) ^ c)  + le2me_32(*((uint32_t*)(param+0x10))) + a - 0x08AC817E;
  a = ((a << 0x06) | (a >> 0x1a)) + b; 
  d = ((~c | a) ^ b)  + le2me_32(*((uint32_t*)(param+0x2c))) + d - 0x42C50DCB;
  d = ((d << 0x0a) | (d >> 0x16)) + a; 
  c = ((~b | d) ^ a)  + le2me_32(*((uint32_t*)(param+0x08))) + c + 0x2AD7D2BB;
  c = ((c << 0x0f) | (c >> 0x11)) + d; 
  b = ((~a | c) ^ d)  + le2me_32(*((uint32_t*)(param+0x24))) + b - 0x14792C6F;
  b = ((b << 0x15) | (b >> 0x0b)) + c; 

#ifdef LOG
  printf("real: hash output: %x %x %x %x\n", a, b, c, d);
#endif
  
  a += le2me_32(*((uint32_t *)(field+0)));
  *((uint32_t *)(field+0)) = le2me_32(a);
  b += le2me_32(*((uint32_t *)(field+4)));
  *((uint32_t *)(field+4)) = le2me_32(b);
  c += le2me_32(*((uint32_t *)(field+8)));
  *((uint32_t *)(field+8)) = le2me_32(c);
  d += le2me_32(*((uint32_t *)(field+12)));
  *((uint32_t *)(field+12)) = le2me_32(d);

#ifdef LOG
  printf("real: hash field:\n");
  hexdump(field, 64+24);
#endif
}



static void call_hash (char *key, char *challenge, int len)
  {

  uint32_t *ptr1, *ptr2;
  uint32_t a, b, c, d;
  uint32_t tmp;

  ptr1=(uint32_t*)(key+16);
  ptr2=(uint32_t*)(key+20);
  
  a = le2me_32(*ptr1);
  b = (a >> 3) & 0x3f;
  a += len * 8;
  *ptr1 = le2me_32(a);
  
  if (a < (len << 3))
  {
#ifdef LOG
    printf("not verified: (len << 3) > a true\n");
#endif
    ptr2 += 4;
  }

  tmp = le2me_32(*ptr2);
  tmp += (len >> 0x1d);
  *ptr2 = le2me_32(tmp);
  a = 64 - b;
  c = 0;  
  if (a <= len)
  {

    memcpy(key+b+24, challenge, a);
    hash(key, key+24);
    c = a;
    d = c + 0x3f;
    
    while ( d < len ) {

#ifdef LOG
      printf("not verified:  while ( d < len )\n");
#endif
      hash(key, challenge+d-0x3f);
      d += 64;
      c += 64;
    }
    b = 0;
  }
  
  memcpy(key+b+24, challenge+c, len-c);
}



static void calc_response (char *result, char *field) {

  char buf1[128];
  char buf2[128];
  int i;

  memset (buf1, 0, 64);
  *buf1 = 128;
  
  memcpy (buf2, field+16, 8);
  
  i = ( le2me_32(*((uint32_t*)(buf2))) >> 3 ) & 0x3f;
 
  if (i < 56) {
    i = 56 - i;
  } else {
#ifdef LOG
    printf("not verified: ! (i < 56)\n");
#endif
    i = 120 - i;
  }

  call_hash (field, buf1, i);
  call_hash (field, buf2, 8);

  memcpy (result, field, 16);

}


static void calc_response_string (char *result, char *challenge)
  {
  char field[128];
  char zres[20];
  int  i;
      
  /* initialize our field */
  BE_32C (field,      0x01234567);
  BE_32C ((field+4),  0x89ABCDEF);
  BE_32C ((field+8),  0xFEDCBA98);
  BE_32C ((field+12), 0x76543210);
  BE_32C ((field+16), 0x00000000);
  BE_32C ((field+20), 0x00000000);

  /* calculate response */
  call_hash(field, challenge, 64);
  calc_response(zres,field);
 
  /* convert zres to ascii string */
  for (i=0; i<16; i++ ) {
    char a, b;
    
    a = (zres[i] >> 4) & 15;
    b = zres[i] & 15;

    result[i*2]   = ((a<10) ? (a+48) : (a+87)) & 255;
    result[i*2+1] = ((b<10) ? (b+48) : (b+87)) & 255;
  }
}

static void real_calc_response_and_checksum (char *response, char *chksum, char *challenge)
  {
  int   ch_len, table_len, resp_len;
  int   i;
  char *ptr;
  char  buf[128];

  /* initialize return values */
  memset(response, 0, 64);
  memset(chksum, 0, 34);
  
  /* initialize buffer */
  memset(buf, 0, 128);
  ptr=buf;
  BE_32C(ptr, 0xa1e9149d);
  ptr+=4;
  BE_32C(ptr, 0x0e6b3b59);
  ptr+=4;

  /* some (length) checks */
  if (challenge != NULL)
    {
    ch_len = strlen (challenge);
    
    if (ch_len == 40) /* what a hack... */
      {
      challenge[32]=0;
      ch_len=32;
      }
    if ( ch_len > 56 )
      ch_len=56;
    
    /* copy challenge to buf */
    memcpy(ptr, challenge, ch_len);
    }
  
  if (xor_table != NULL)
    {
    table_len = strlen(xor_table);
    
    if (table_len > 56) table_len=56;
    
    /* xor challenge bytewise with xor_table */
    for (i=0; i<table_len; i++)
      ptr[i] = ptr[i] ^ xor_table[i];
    }
  
  calc_response_string (response, buf);
  
  /* add tail */
  resp_len = strlen (response);
  strcpy (&response[resp_len], "01d0a8e3");
  
  /* calculate checksum */
  for (i=0; i<resp_len/4; i++)
    chksum[i] = response[i*4];
  }

