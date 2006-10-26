/*****************************************************************
 
  demux_wavpack.c
 
  Copyright (c) 2006 by Burkhard Plaum - plaum@ipf.uni-stuttgart.de
 
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define LOG_DOMAIN "wavpack"

typedef struct
  {
  uint32_t fourcc;
  uint32_t block_size;
  uint16_t version;
  uint8_t  track_number;
  uint8_t  track_sub_index;
  uint32_t total_samples;
  uint32_t sample_offset;
  uint32_t num_samples;
  uint32_t flags;
  uint32_t crc;
  } wvpk_header_t;

#define HEADER_SIZE 32

static const int wv_rates[16] = {
     6000,  8000,  9600, 11025, 12000, 16000, 22050, 24000,
    32000, 44100, 48000, 64000, 88200, 96000, 192000, -1
};

#define WV_MONO   0x0004
#define WV_HYBRID 0x0008
#define WV_JOINT  0x0010
#define WV_CROSSD 0x0020
#define WV_HSHAPE 0x0040
#define WV_FLOAT  0x0080
#define WV_INT32  0x0100
#define WV_HBR    0x0200
#define WV_HBAL   0x0400
#define WV_MCINIT 0x0800
#define WV_MCEND  0x1000

#define WV_EXTRA_SIZE 12 /* Bytes from the header, which must be
                            passed to the decoder */

static void parse_header(wvpk_header_t * ret, uint8_t * data)
  {
  ret->fourcc          = BGAV_PTR_2_FOURCC(data); data+=4;
  ret->block_size      = BGAV_PTR_2_32LE(data); data+=4;
  ret->version         = BGAV_PTR_2_16LE(data); data+=2;
  ret->track_number    = *data; data++;
  ret->track_sub_index = *data; data++;
  ret->total_samples   = BGAV_PTR_2_32LE(data); data+=4;
  ret->sample_offset   = BGAV_PTR_2_32LE(data); data+=4;
  ret->num_samples     = BGAV_PTR_2_32LE(data); data+=4;
  ret->flags           = BGAV_PTR_2_32LE(data); data+=4;
  ret->crc             = BGAV_PTR_2_32LE(data);
  }

#if 0

static void dump_header(wvpk_header_t * h)
  {
  bgav_dprintf("wavpack header\n");
  
  bgav_dprintf("  fourcc:          ");
  bgav_dump_fourcc(h->fourcc);
  bgav_dprintf("\n");

  bgav_dprintf("  block_size:      %d\n", h->block_size);
  bgav_dprintf("  version:         %d\n", h->version);
  bgav_dprintf("  track_number:    %d\n", h->track_number);
  bgav_dprintf("  track_sub_index: %d\n", h->track_sub_index);
  bgav_dprintf("  total_samples:   %d\n", h->total_samples);
  bgav_dprintf("  sample_offset:   %d\n", h->sample_offset);
  bgav_dprintf("  num_samples:     %d\n", h->num_samples);
  bgav_dprintf("  flags:           %08x\n", h->flags);
  bgav_dprintf("  crc:             %08x\n", h->crc);
  }
#endif

static int probe_wavpack(bgav_input_context_t * input)
  {
  uint32_t fourcc;
  if(bgav_input_get_fourcc(input, &fourcc) &&
     (fourcc == BGAV_MK_FOURCC('w','v','p','k')))
    return 1;
  return 0;
  }

static int open_wavpack(bgav_demuxer_context_t * ctx,
                   bgav_redirector_context_t ** redir)
  {
  bgav_stream_t * s;
  uint8_t header[HEADER_SIZE];
  wvpk_header_t h;
  
  if(bgav_input_get_data(ctx->input, header, HEADER_SIZE) < HEADER_SIZE)
    return 0;

  parse_header(&h, header);
  //  dump_header(&h);

  /* Use header data to set up stream */
  if(h.flags & WV_FLOAT)
    {
    bgav_log(ctx->opt, BGAV_LOG_ERROR, LOG_DOMAIN, "Floating point data is not supported");
    return 0;
    }
  
  if(h.flags & WV_HYBRID)
    {
    bgav_log(ctx->opt, BGAV_LOG_ERROR, LOG_DOMAIN, "Hybrid coding mode is not supported");
    return 0;
    }
  
  if(h.flags & WV_INT32)
    {
    bgav_log(ctx->opt, BGAV_LOG_ERROR, LOG_DOMAIN, "Integer point data is not supported");
    return 0;
    }
  
  /* Create the track and the stream */
  ctx->tt = bgav_track_table_create(1);
  s = bgav_track_add_audio_stream(ctx->tt->current_track, ctx->opt);

  s->data.audio.format.num_channels = 1 + !(h.flags & WV_MONO);
  s->data.audio.format.samplerate   = wv_rates[(h.flags >> 23) & 0xF];
  s->fourcc = BGAV_MK_FOURCC('w','v','p','k');
  s->data.audio.bits_per_sample = ((h.flags & 3) + 1) << 3;
  
  ctx->stream_description = bgav_sprintf("Wavpack");
  ctx->tt->current_track->duration =
    gavl_time_unscale(s->data.audio.format.samplerate, h.total_samples);

  if(ctx->input->input->seek_byte)
    ctx->can_seek = 1;
  
  return 1;
  }

static int next_packet_wavpack(bgav_demuxer_context_t * ctx)
  {
  uint8_t header[HEADER_SIZE];
  wvpk_header_t h;
  bgav_packet_t * p;
  bgav_stream_t * s;
  int size;
  
  if(bgav_input_read_data(ctx->input, header, HEADER_SIZE) < HEADER_SIZE)
    return 0; // EOF

  s = &(ctx->tt->current_track->audio_streams[0]);
  p = bgav_packet_buffer_get_packet_write(s->packet_buffer, s);

  /* The last 12 bytes of the header must be copied to the
     packet */

  parse_header(&h, header);
  //  dump_header(&h);

  if(h.fourcc != BGAV_MK_FOURCC('w', 'v', 'p', 'k'))
    {
    bgav_log(ctx->opt, BGAV_LOG_ERROR, LOG_DOMAIN, "Lost sync");
    return 0;
    }
  size = h.block_size - 24;
  
  bgav_packet_alloc(p, size + WV_EXTRA_SIZE);
  memcpy(p->data, header + (HEADER_SIZE - WV_EXTRA_SIZE), WV_EXTRA_SIZE);

  if(bgav_input_read_data(ctx->input, p->data + WV_EXTRA_SIZE, size) < size)
    return 0; // EOF
  
  p->data_size = WV_EXTRA_SIZE + size;
  
  bgav_packet_done_write(p);


  return 1;
  }

static void seek_wavpack(bgav_demuxer_context_t * ctx, gavl_time_t time)
  {
  int64_t current_pos;
  int64_t time_scaled;
  bgav_stream_t * s;
  
  uint8_t header[HEADER_SIZE];
  wvpk_header_t h;

  s = &(ctx->tt->current_track->audio_streams[0]);

  current_pos = 0;
  time_scaled = gavl_time_scale(s->timescale, time);

  bgav_input_seek(ctx->input, 0, SEEK_SET);

  while(1)
    {
    if(bgav_input_get_data(ctx->input, header, HEADER_SIZE) < HEADER_SIZE)
      return;
    parse_header(&h, header);
    if(current_pos + h.num_samples > time_scaled)
      break;

    bgav_input_skip(ctx->input, HEADER_SIZE);
    bgav_input_skip(ctx->input, h.block_size - 24);
    current_pos += h.num_samples;
    }
  s->time_scaled = current_pos;
  }

static void close_wavpack(bgav_demuxer_context_t * ctx)
  {

  }

bgav_demuxer_t bgav_demuxer_wavpack =
  {
    probe:       probe_wavpack,
    open:        open_wavpack,
    next_packet: next_packet_wavpack,
    seek:        seek_wavpack,
    close:       close_wavpack
  };