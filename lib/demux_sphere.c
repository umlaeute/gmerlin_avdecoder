/*****************************************************************
  demux_shere.c
 
  Copyright (c) 2005 by Michael Gruenert - one78@web.de
 
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
#include <stdio.h>
#include <avdec_private.h>
#include <string.h>
#include <ctype.h>

#define SAMPLES2READ 1024
#define HEADERSIZE 1024

/* ircam demuxer */

typedef struct
  {
  uint64_t HeaderSize;
  uint64_t SampleRate;
  uint8_t Channels;
  uint8_t BitsPerSample;
  uint8_t SampleNBytes;
  char * SampleByteFormat;
  char * SampleCoding;
  uint64_t SampleCount;
  } sphere_header_t;

static int probe_sphere(bgav_input_context_t * input)
  {
  uint8_t test_data[7];
  if(bgav_input_get_data(input, test_data, 7) < 7)
    return 0;
  if((test_data[0] == 'N') && (test_data[1] == 'I') && (test_data[2] == 'S') && (test_data[3] == 'T') &&
     (test_data[4] == '_') && (test_data[5] == '1') && (test_data[6] == 'A'))
    return 1;
  return 0;
  }

static int check_key(const char * buf, const char * objetct)
  { 
  if(!strncmp(buf, objetct, strlen(objetct)))
    return 1;
  return 0;
  }

static int find_string(const char * buf, char **string)
  {
  int i;
  char *ptr;
  ptr = (char*)buf;

  while(!isspace(*ptr))    /* first space */
    {
    if(*ptr == '\0')
      return 0;
    ptr++;
    }
  ptr++;

  while(!isdigit(*ptr))    /* digit at -s */
    {
    if(*ptr == '\0')
      return 0;
    ptr++;
    }
  
  i = atoi(ptr);
  
  *string = calloc(i+1, sizeof(char));
  
  while(!isspace(*ptr))    /* first space */
    {
    if(*ptr == '\0')
      return 0;
    ptr++;
    }
  ptr++;

  strncpy(*string, ptr, i);
  return 1;
  }

static int find_num(const char * buf)
  {
  char *ptr;
  ptr = (char*)buf;
  while(!isspace(*ptr))    /* first space */
    {
    if(*ptr == '\0')
      return 0;
    ptr++;
    }
  ptr++;
  while(!isspace(*ptr))    /* second space */ 
    {
    if(*ptr == '\0')
      return 0;
    ptr++;
    }
  ptr++;
  return atoi(ptr);
  }

static int sphere_header_read(bgav_input_context_t * input, sphere_header_t * ret)
  {
  char * buffer = (char*)0;
  int buffer_alloc = 0;

  memset(ret, 0, sizeof(*ret));
    
  while(1)
    {
    if(!bgav_input_read_line(input, &buffer, &buffer_alloc, 0, (int*)0))
      return 0;
    if(check_key(buffer, "NIST_1A"))
      {
      if(!bgav_input_read_line(input, &buffer, &buffer_alloc, 0, (int*)0))
        return 0;
      ret->HeaderSize = atoi(buffer);
      }
    if(check_key(buffer, "sample_rate "))
      ret->SampleRate = find_num(buffer);
    if(check_key(buffer, "channel_count "))
      ret->Channels = find_num(buffer);      
    if(check_key(buffer, "sample_sig_bits "))
      ret->BitsPerSample = find_num(buffer);      
    if(check_key(buffer, "sample_n_bytes "))
      ret->SampleNBytes = find_num(buffer);
    if(check_key(buffer, "sample_byte_format "))
      if(!find_string(buffer, &ret->SampleByteFormat))
        return 0;
    if(check_key(buffer, "sample_count "))
      ret->SampleCount = find_num(buffer);
    if(check_key(buffer, "sample_coding "))
      if(!find_string(buffer, &ret->SampleCoding))
        return 0;
    if(check_key(buffer, "end_head"))
      break;
    if(input->position > ret->HeaderSize)
      {
      return 0;
      break;
      }
    }
  return 1;
  }

#if 0
static void sphere_header_dump(sphere_header_t * h)
  {
  bgav_dprintf("NIST SPHERE Header\n");
  bgav_dprintf("  HeaderSize:       %lld\n",h->HeaderSize);
  bgav_dprintf("  SampleRate:       %lld\n",h->SampleRate);
  bgav_dprintf("  Channels:         %d\n",h->Channels);
  bgav_dprintf("  BitsPerSample:    %d\n",h->BitsPerSample);
  bgav_dprintf("  SampleNBytes:     %d\n",h->SampleNBytes);
  bgav_dprintf("  SampleByteFormat: %s\n",h->SampleByteFormat);
  bgav_dprintf("  SampleCoding:     %s\n",h->SampleCoding);
  bgav_dprintf("  SampleCount:      %lld\n",h->SampleCount);
  }
#endif

static int open_sphere(bgav_demuxer_context_t * ctx,
                       bgav_redirector_context_t ** redir)
  {
  sphere_header_t h;
  bgav_stream_t * as;
  int bytes_per_sample;
  int64_t total_samples;
  
  ctx->tt = bgav_track_table_create(1);

  
  if(!sphere_header_read(ctx->input, &h))
    return 0;

#if 0
  sphere_header_dump(&h);
#endif
  
  if(h.HeaderSize != HEADERSIZE)
    return 0;
  
  as = bgav_track_add_audio_stream(ctx->tt->current_track, ctx->opt);

  as->data.audio.format.num_channels = h.Channels;
  
  if(h.SampleCoding && strcmp(h.SampleCoding, "pcm"))
    {
    if(!strcmp(h.SampleCoding, "ulaw") || !strcmp(h.SampleCoding, "mu-law"))
      {
      as->fourcc = BGAV_MK_FOURCC('u','l','a','w');
      as->data.audio.block_align = as->data.audio.format.num_channels;
      }
    else if(!strcmp(h.SampleCoding, "alaw"))
      {
      as->fourcc = BGAV_MK_FOURCC('a','l','a','w');
      as->data.audio.block_align = as->data.audio.format.num_channels;
      }
    }
  else /* pcm */
    {
    bytes_per_sample = 0;
    if(h.SampleNBytes)
      bytes_per_sample = h.SampleNBytes;
    else if(h.SampleByteFormat)
      bytes_per_sample = strlen(h.SampleByteFormat);

    if(!bytes_per_sample)
      {
      fprintf(stderr, "Bytes per sample is zero!\n");
      return 0;
      }
    
    as->data.audio.block_align = as->data.audio.format.num_channels * bytes_per_sample;
    as->data.audio.bits_per_sample = bytes_per_sample * 8;

    /* Endianess */
    if((bytes_per_sample > 1) && (!h.SampleByteFormat))
      return 0;
    
    if(strstr(h.SampleByteFormat, "10"))
      as->data.audio.endianess = BGAV_ENDIANESS_BIG;
    else if(strstr(h.SampleByteFormat, "01"))
      as->data.audio.endianess = BGAV_ENDIANESS_LITTLE;
    else
      return 0;
    
    
    switch(bytes_per_sample)
      {
      case 1:
        as->fourcc = BGAV_MK_FOURCC('t','w','o','s');
        break;
      case 2:
        if(as->data.audio.endianess == BGAV_ENDIANESS_BIG)
          as->fourcc = BGAV_MK_FOURCC('t','w','o','s'); /* BE */
        else
          as->fourcc = BGAV_MK_FOURCC('s','o','w','t'); /* LE */
        break;
      case 3:
        as->fourcc = BGAV_MK_FOURCC('i','n','2','4');
        break;
      case 4:
        as->fourcc = BGAV_MK_FOURCC('i','n','3','2');
        break;
      }
    }

  as->data.audio.format.samplerate = h.SampleRate;

  if(ctx->input->total_bytes)
    {
    total_samples = (ctx->input->total_bytes - HEADERSIZE) / as->data.audio.block_align;
    ctx->tt->current_track->duration =  gavl_samples_to_time(as->data.audio.format.samplerate, total_samples);
    if(ctx->input->input->seek_byte)
      ctx->can_seek = 1;
    }
  else if(h.SampleCount)
    {
    ctx->tt->current_track->duration =  gavl_samples_to_time(as->data.audio.format.samplerate, h.SampleCount);
    if(ctx->input->input->seek_byte)
      ctx->can_seek = 1;
    }

  bgav_input_skip(ctx->input, HEADERSIZE - ctx->input->position);

  ctx->stream_description = bgav_sprintf("NIST SPHERE");
 
  if(h.SampleByteFormat)
    free(h.SampleByteFormat);
  if(h.SampleCoding)
    free(h.SampleCoding);
   
  return 1;
  }


static int64_t samples_to_bytes(bgav_stream_t * s, int samples)
  {
  return  s->data.audio.block_align * samples;
  }

static int next_packet_sphere(bgav_demuxer_context_t * ctx)
  {
  bgav_packet_t * p;
  bgav_stream_t * s;
  int bytes_read;
  int bytes_to_read;

  s = &(ctx->tt->current_track->audio_streams[0]);
  p = bgav_stream_get_packet_write(s);

  bytes_to_read = samples_to_bytes(s, SAMPLES2READ);

  if((ctx->input->total_bytes > 0) && (ctx->input->position + bytes_to_read > ctx->input->total_bytes))
    bytes_to_read = ctx->input->total_bytes - ctx->input->position;

  if(bytes_to_read <= 0)
    return 0;
  
  bgav_packet_alloc(p, bytes_to_read);
  
  p->pts = (ctx->input->position - HEADERSIZE) / s->data.audio.block_align;

  p->keyframe = 1;
  bytes_read = bgav_input_read_data(ctx->input, p->data, bytes_to_read);
  p->data_size = bytes_read;

  if(bytes_read < s->data.audio.block_align)
    return 0;
  
  bgav_packet_done_write(p);
  return 1;
  }

static void seek_sphere(bgav_demuxer_context_t * ctx, gavl_time_t time)
  {
  bgav_stream_t * s;
  int64_t position;
  int64_t sample;

  s = &(ctx->tt->current_track->audio_streams[0]);

  sample = gavl_time_to_samples(s->data.audio.format.samplerate, time);
    
  position =  s->data.audio.block_align * sample + HEADERSIZE;
  bgav_input_seek(ctx->input, position, SEEK_SET);
  s->time_scaled = sample;
  }

static void close_sphere(bgav_demuxer_context_t * ctx)
  {
  return;
  }

bgav_demuxer_t bgav_demuxer_sphere =
  {
    probe:       probe_sphere,
    open:        open_sphere,
    next_packet: next_packet_sphere,
    seek:        seek_sphere,
    close:       close_sphere
  };
