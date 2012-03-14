/*****************************************************************
 * gmerlin-avdecoder - a general purpose multimedia decoding library
 *
 * Copyright (c) 2001 - 2011 Members of the Gmerlin project
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

#include <string.h>


#include <avdec_private.h>
#include <flac_header.h>


int bgav_flac_streaminfo_read(const uint8_t * ptr, bgav_flac_streaminfo_t * ret)
  {
  uint64_t tmp_1;

  ret->min_blocksize = BGAV_PTR_2_16BE(ptr); ptr += 2;
  ret->max_blocksize = BGAV_PTR_2_16BE(ptr); ptr += 2;
  ret->min_framesize = BGAV_PTR_2_24BE(ptr); ptr += 3;
  ret->max_framesize = BGAV_PTR_2_24BE(ptr); ptr += 3;

  tmp_1 = BGAV_PTR_2_64BE(ptr); ptr += 8;
  
  ret->samplerate      =   tmp_1 >> 44;
  ret->num_channels    = ((tmp_1 >> 41) & 0x7) + 1;
  ret->bits_per_sample = ((tmp_1 >> 36) & 0x1f) + 1;
  ret->total_samples   = (tmp_1 & 0xfffffffffLL);

  /* MD5 */
  memcpy(ret->md5, ptr, 16);
  return 1;
  }

void bgav_flac_streaminfo_dump(bgav_flac_streaminfo_t * s)
  {
  bgav_dprintf("FLAC Streaminfo\n");
  bgav_dprintf("  Blocksize [%d/%d]\n", s->min_blocksize,
               s->max_blocksize);
  bgav_dprintf("  Framesize [%d/%d]\n", s->min_framesize,
               s->max_framesize);
  bgav_dprintf("  Samplerate:      %d\n", s->samplerate);
  bgav_dprintf("  Num channels:    %d\n", s->num_channels);
  bgav_dprintf("  Bits per sample: %d\n", s->bits_per_sample);
  bgav_dprintf("  Total samples:   %" PRId64 "\n", s->total_samples);
  }

static int read_utf8(const uint8_t * ptr, int64_t * ret)
  {
  uint8_t mask, val;
  int bytes, i;
  
  /* 0xxxxxxx */
  if((ptr[0] & 0x80) == 0x00)
    {
    *ret = ptr[0];
    return 1;
    }

  mask = 0xff;
  val  = 0xfe;
  bytes = 6;

  /* 11111110 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
  /* 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
  /* 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
  /* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
  /* 1110xxxx 10xxxxxx 10xxxxxx */
  /* 110xxxxx 10xxxxxx */
  
  while(bytes >= 1)
    {
    if((ptr[0] & mask) == val)
      {
      *ret = ptr[0] & ~mask;

      for(i = 1; i <= bytes; i++)
        {
        if((ptr[i] & 0xc0) != 0x80)
          return 0;
        
        (*ret) <<= 6;
        *ret |= (ptr[i] & 0x3f);
        }
      return bytes + 1;
      }

    mask <<= 1;
    val <<= 1;
    bytes--;
    }
  return 0;
  }

/* CRC stuff taken from FLAC sourcecode */

static const uint8_t crc8_table[256] =
  {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
  };

static int crc8(const uint8_t * data, int len)
  {
  int i;
  uint8_t crc = 0;
  for(i = 0; i < len; i++)
    crc = crc8_table[crc ^ data[i]];
  return crc;
  }

int bgav_flac_frame_header_read(const uint8_t * ptr,
                              bgav_flac_streaminfo_t * si,
                              bgav_flac_frame_header_t * ret)
  {
  int len;
  int tmp;
  int crc;
  
  const uint8_t * ptr_start = ptr;
  
  tmp = BGAV_PTR_2_16BE(ptr); ptr += 2;

  /* Check sync code */
  if((tmp & 0xFFFC) != 0xFFF8)
    return 0;
  
  if(tmp & 0x0002) // Mandatory zero
    return 0;

  ret->blocking_strategy_code = tmp & 0x0001;

  /* Block size, sample_rate */
  tmp = *ptr; ptr++;

  ret->block_size_code = (tmp >> 4) & 0x0f;
  ret->samplerate_code = tmp & 0x0f;

  /* Channel, sample size */
  tmp = *ptr; ptr++;
  
  ret->channel_code     = (tmp >> 4) & 0x0f;
  ret->samplesize_code = (tmp >> 1) & 0x07;

  if(tmp & 0x01) // Mandatory zero
    return 0;

  /* Compute values */

  /* Sample number */
  if(ret->blocking_strategy_code == 0) // Fixed blocksize
    {
    if(si->min_blocksize != si->max_blocksize)
      return 0;
    
    len = read_utf8(ptr, &ret->sample_number);
    if(!len || (len > 6))
      return 0;
    ret->sample_number *= si->min_blocksize;
    ptr += len;
    }
  else // Variable blocksize, encode sample number
    {
    len = read_utf8(ptr, &ret->sample_number);
    if(!len)
      return 0;
    ptr += len;
    }

  /* Block size of this frame */

  if(ret->block_size_code == 0)
    return 0;
  else if(ret->block_size_code == 1)
    ret->blocksize = 192;
  else if(ret->block_size_code < 6)
    ret->blocksize = 576 * (1 << (ret->block_size_code - 2));
  else if(ret->block_size_code == 6)
    {
    ret->blocksize = *ptr; ptr++;
    ret->blocksize++;
    }
  else if(ret->block_size_code == 7)
    {
    ret->blocksize = BGAV_PTR_2_16BE(ptr); ptr += 2;
    ret->blocksize++;
    }
  else
    ret->blocksize = 256 * (1 << (ret->block_size_code - 8));
  
  /* Samplerate */
  switch(ret->samplerate_code)
    {
    case 0x0:
      ret->samplerate = si->samplerate;
      break;
    case 0x1:
      ret->samplerate = 88200;
      break;
    case 0x2:
      ret->samplerate = 176400;
      break;
    case 0x3:
      ret->samplerate = 192000;
      break;
    case 0x4:
      ret->samplerate = 8000;
      break;
    case 0x5:
      ret->samplerate = 16000;
      break;
    case 0x6:
      ret->samplerate = 22050;
      break;
    case 0x7:
      ret->samplerate = 24000;
      break;
    case 0x8:
      ret->samplerate = 32000;
      break;
    case 0x9:
      ret->samplerate = 44100;
      break;
    case 0xa:
      ret->samplerate = 48000;
      break;
    case 0xb:
      ret->samplerate = 96000;
      break;
    case 0xc:
      ret->samplerate = *ptr; ptr++;
      ret->samplerate *= 1000;
      break;
    case 0xd:
      ret->samplerate = BGAV_PTR_2_16BE(ptr); ptr += 2;
      break;
    case 0xe:
      ret->samplerate = BGAV_PTR_2_16BE(ptr); ptr += 2;
      ret->samplerate *= 10;
      break;
    case 0xf:
      return 0;
      break;
    }

  /* Get CRC */
  crc = crc8(ptr_start, ptr - ptr_start);
  if(crc != *ptr)
    {
    fprintf(stderr, "CRC mismatch\n");
    return 0;
    }
  
  return 1;
  }
