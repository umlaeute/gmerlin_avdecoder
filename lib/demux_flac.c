/*****************************************************************
 
  demux_flac.c
 
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

/*
 *  Flac support is implemented the following way:
 *  We parse the toplevel format here, without the
 *  need for FLAC specific libraries. Then we put the
 *  whole header into the extradata of the stream
 *
 *  The flac audio decoder will then be done using FLAC
 *  software
 */

#include <stdlib.h>
#include <string.h>
#include <avdec_private.h>

typedef struct
  {
  uint16_t min_blocksize;
  uint16_t max_blocksize;

  uint32_t min_framesize;
  uint32_t max_framesize;

  uint32_t samplerate;
  int num_channels;
  int bits_per_sample;
  int64_t total_samples;
  } streaminfo_t;

#define STREAMINFO_SIZE 38

static int streaminfo_read(bgav_input_context_t * ctx,
                    streaminfo_t * ret)
  {
  uint64_t tmp_1;
  
  if(!bgav_input_read_16_be(ctx, &(ret->min_blocksize)) ||
     !bgav_input_read_16_be(ctx, &(ret->max_blocksize)) ||
     !bgav_input_read_24_be(ctx, &(ret->min_framesize)) ||
     !bgav_input_read_24_be(ctx, &(ret->max_framesize)) ||
     !bgav_input_read_64_be(ctx, &(tmp_1)))
    return 0;

  ret->samplerate      =   tmp_1 >> 44;
  ret->num_channels    = ((tmp_1 >> 41) & 0x7) + 1;
  ret->bits_per_sample = ((tmp_1 >> 36) & 0x1f) + 1;
  ret->total_samples   = (tmp_1 & 0xfffffffffLL);

  /* Skip MD5 */
  bgav_input_skip(ctx, 16);
  
  return 1;
  }
#if 0
static void streaminfo_dump(streaminfo_t * s)
  {
  fprintf(stderr, "FLAC Streaminfo\n");
  fprintf(stderr, "Blocksize [%d/%d]\n", s->min_blocksize,
          s->max_blocksize);
  fprintf(stderr, "Framesize [%d/%d]\n", s->min_framesize,
          s->max_framesize);
  fprintf(stderr, "Samplerate:      %d\n", s->samplerate);
  fprintf(stderr, "Num channels:    %d\n", s->num_channels);
  fprintf(stderr, "Bits per sample: %d\n", s->bits_per_sample);
  fprintf(stderr, "Total samples:   %lld\n", s->total_samples);
  }
#endif
/* Seek table */

typedef struct
  {
  int num_entries;
  struct
    {
    uint64_t sample_number;
    uint64_t offset;
    uint16_t num_samples;
    } * entries;
  } seektable_t;

static int seektable_read(bgav_input_context_t * input,
                          seektable_t * ret,
                          int size)
  {
  int i;
  ret->num_entries = size / 18;
  ret->entries = malloc(ret->num_entries * sizeof(*(ret->entries)));

  for(i = 0; i < ret->num_entries; i++)
    {
    if(!bgav_input_read_64_be(input, &(ret->entries[i].sample_number)) ||
       !bgav_input_read_64_be(input, &(ret->entries[i].offset)) ||
       !bgav_input_read_16_be(input, &(ret->entries[i].num_samples)))
      return 0;
    }
  return 1;
  }
#if 0
static void seektable_dump(seektable_t * t)
  {
  int i;
  fprintf(stderr, "Seektable: %d entries\n", t->num_entries);
  for(i = 0; i < t->num_entries; i++)
    {
    fprintf(stderr, "Sample: %lld, Position: %lld, Num samples: %d\n",
            t->entries[i].sample_number,
            t->entries[i].offset,
            t->entries[i].num_samples);
    }
  }
#endif
static int probe_flac(bgav_input_context_t * input)
  {
  uint8_t probe_data[4];

  if(bgav_input_get_data(input, probe_data, 4) < 4)
    return 0;

  if((probe_data[0] == 'f') &&
     (probe_data[1] == 'L') &&
     (probe_data[2] == 'a') &&
     (probe_data[3] == 'C'))
    return 1;
     
  return 0;
  }

typedef struct
  {
  streaminfo_t streaminfo;
  seektable_t seektable;
  int64_t data_start;
  } flac_priv_t;

static int open_flac(bgav_demuxer_context_t * ctx,
                     bgav_redirector_context_t ** redir)
  {
  bgav_stream_t * s;
  uint8_t header[4];
  uint32_t size;
  flac_priv_t * priv;
  bgav_input_context_t * input_mem;
  
  /* Skip header */

  bgav_input_skip(ctx->input, 4);

  priv = calloc(1, sizeof(*priv));
  ctx->priv = priv;
  ctx->tt = bgav_track_table_create(1);
  
  header[0] = 0;
  
  while(!(header[0] & 0x80))
    {
    //    fprintf(stderr, "Pos: %lld\n", ctx->input->position);
    
    if(bgav_input_read_data(ctx->input, header, 4) < 4)
      return 0;
    
    size = header[1];
    size <<= 8;
    size |= header[2];
    size <<= 8;
    size |= header[3];
    
    
    //    fprintf(stderr, "Size: %d\n", size);
    
    //    if(!bgav_input_read_24_be(ctx->input, &size))
    //      return 0;
    
    switch(header[0] & 0x7F)
      {
      case 0: // STREAMINFO
        //        fprintf(stderr, "STREAMINFO\n");
        /* Add audio stream */
        s = bgav_track_add_audio_stream(ctx->tt->current_track);

        s->ext_size = STREAMINFO_SIZE + 4;
        s->ext_data = malloc(s->ext_size);

        s->ext_data[0] = 'f';
        s->ext_data[1] = 'L';
        s->ext_data[2] = 'a';
        s->ext_data[3] = 'C';
        
        memcpy(s->ext_data+4, header, 4);
        
        /* We tell the decoder, that this is the last metadata packet */
        s->ext_data[4] |= 0x80;
        
        if(bgav_input_read_data(ctx->input, s->ext_data + 8,
                                STREAMINFO_SIZE - 4) < 
           STREAMINFO_SIZE - 4)
          goto fail;
        
        input_mem =
          bgav_input_open_memory(s->ext_data + 8, STREAMINFO_SIZE - 4);
        
        if(!streaminfo_read(input_mem, &(priv->streaminfo)))
          goto fail;
        bgav_input_close(input_mem);
        bgav_input_destroy(input_mem);
        //        streaminfo_dump(&(priv->streaminfo));
                
        s->data.audio.format.num_channels = priv->streaminfo.num_channels;
        s->data.audio.format.samplerate   = priv->streaminfo.samplerate;
        s->data.audio.bits_per_sample     = priv->streaminfo.bits_per_sample;
        s->fourcc = BGAV_MK_FOURCC('F', 'L', 'A', 'C');
        
        if(priv->streaminfo.total_samples)
          ctx->tt->current_track->duration =
            gavl_samples_to_time(priv->streaminfo.samplerate,
                                 priv->streaminfo.total_samples);
        
        //        bgav_input_skip(ctx->input, size);
        break;
      case 1: // PADDING
        //        fprintf(stderr, "PADDING\n");
        bgav_input_skip(ctx->input, size);
        break;
      case 2: // APPLICATION
        //        fprintf(stderr, "APPLICATION\n");
        bgav_input_skip(ctx->input, size);
        break;
      case 3: // SEEKTABLE
        //        fprintf(stderr, "SEEKTABLE\n");

        if(!seektable_read(ctx->input, &(priv->seektable), size))
          goto fail;
        //        seektable_dump(&(priv->seektable));
        //        bgav_input_skip(ctx->input, size);
        break;
      case 4: // VORBIS_COMMENT
        //        fprintf(stderr, "VORBIS_COMMENT\n");
        bgav_input_skip(ctx->input, size);
        break;
      case 5: // CUESHEET
        //        fprintf(stderr, "CUESHEET\n");
        bgav_input_skip(ctx->input, size);
        break;
      default:
        bgav_input_skip(ctx->input, size);
      }

    }
  priv->data_start = ctx->input->position;
  
  ctx->stream_description = bgav_strndup("FLAC Format", NULL);

  if(priv->seektable.num_entries && ctx->input->input->seek_byte)
    ctx->can_seek = 1;
  
  return 1;
    fail:
  return 0;
  }

#define PACKET_BYTES 1024

static int next_packet_flac(bgav_demuxer_context_t * ctx)
  {
  bgav_stream_t * s;
  bgav_packet_t * p;

  s = bgav_track_find_stream(ctx->tt->current_track, 0);
  
  /* We play dumb and read just 1024 bytes */

  p = bgav_packet_buffer_get_packet_write(s->packet_buffer, s);
  
  bgav_packet_alloc(p, PACKET_BYTES);
  p->data_size = bgav_input_read_data(ctx->input, 
                                      p->data, PACKET_BYTES);

  if(!p->data_size)
    return 0;

  bgav_packet_done_write(p);
  return 1;
  }

static void seek_flac(bgav_demuxer_context_t * ctx, gavl_time_t time)
  {
  int i;
  flac_priv_t * priv;
  int64_t sample_pos;
  
  
  priv = (flac_priv_t*)(ctx->priv);

  sample_pos = gavl_time_to_samples(priv->streaminfo.samplerate,
                                    time);
  
  for(i = 0; i < priv->seektable.num_entries - 1; i++)
    {
    if((priv->seektable.entries[i].sample_number <= sample_pos) &&
       (priv->seektable.entries[i+1].sample_number >= sample_pos))
      break;
    }
  
  if(priv->seektable.entries[i+1].sample_number == sample_pos)
    i++;
  
  /* Seek to the point */
    
  bgav_input_seek(ctx->input,
                  priv->seektable.entries[i].offset + priv->data_start,
                  SEEK_SET);
  
  ctx->tt->current_track->audio_streams[0].time_scaled = priv->seektable.entries[i].sample_number;
  }

static void close_flac(bgav_demuxer_context_t * ctx)
  {
  flac_priv_t * priv;
  priv = (flac_priv_t*)(ctx->priv);

  if(priv->seektable.num_entries)
    free(priv->seektable.entries);

  if(ctx->tt->current_track->audio_streams[0].ext_data)
    free(ctx->tt->current_track->audio_streams[0].ext_data);

  free(priv);
  }

bgav_demuxer_t bgav_demuxer_flac =
  {
    probe:       probe_flac,
    open:        open_flac,
    next_packet: next_packet_flac,
    seek:        seek_flac,
    close:       close_flac
  };


