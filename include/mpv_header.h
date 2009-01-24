/*****************************************************************
 * gmerlin-avdecoder - a general purpose multimedia decoding library
 *
 * Copyright (c) 2001 - 2008 Members of the Gmerlin project
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


#define MPV_PROBE_SIZE 5 /* Sync code + extension type */

const uint8_t * bgav_mpv_find_startcode( const uint8_t *p,
                                         const uint8_t *end );

void bgav_mpv_get_framerate(int code, int * timescale, int *frame_duration);

/*
 *  Return values of the parse functions:
 *   0: Not enough data
 *  -1: Parse error
 *   1: Success
 */

typedef struct
  {
  int progressive_sequence;

  int horizontal_size_ext;
  int vertical_size_ext;
  
  uint32_t bitrate_ext;
  int timescale_ext;
  int frame_duration_ext;
  
  } bgav_mpv_sequence_extension_t;

typedef struct
  {
  int mpeg2;
  uint32_t bitrate; /* In 400 bits per second */

  int horizontal_size_value;
  int vertical_size_value;

  int frame_rate_index;
  
  bgav_mpv_sequence_extension_t ext;
  } bgav_mpv_sequence_header_t;

int bgav_mpv_sequence_header_probe(const uint8_t * buffer);
int bgav_mpv_sequence_header_parse(const bgav_options_t * opt,
                                   bgav_mpv_sequence_header_t *,
                                   const uint8_t * buffer, int len);

int bgav_mpv_sequence_extension_probe(const uint8_t * buffer);
int bgav_mpv_sequence_extension_parse(const bgav_options_t * opt,
                                      bgav_mpv_sequence_extension_t *,
                                      const uint8_t * buffer, int len);

typedef struct
  {
  int top_field_first;
  int repeat_first_field;
  int progressive_frame;
  } bgav_mpv_picture_extension_t;

typedef struct
  {
  int coding_type;
  bgav_mpv_picture_extension_t ext;
  } bgav_mpv_picture_header_t;

int bgav_mpv_picture_header_probe(const uint8_t * buffer);
int bgav_mpv_picture_header_parse(const bgav_options_t * opt,
                                  bgav_mpv_picture_header_t *,
                                  const uint8_t * buffer, int len);



int bgav_mpv_picture_extension_probe(const uint8_t * buffer);
int bgav_mpv_picture_extension_parse(const bgav_options_t * opt,
                                     bgav_mpv_picture_extension_t *,
                                     const uint8_t * buffer, int len);

int bgav_mpv_gop_header_probe(const uint8_t * buffer);

