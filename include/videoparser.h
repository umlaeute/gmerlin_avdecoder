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

#define PARSER_NEED_DATA      0
#define PARSER_HAVE_HEADER    1
#define PARSER_HAVE_PACKET    2
#define PARSER_EOF            3

typedef struct bgav_video_parser_s bgav_video_parser_t;

bgav_video_parser_t * bgav_video_parser_create(uint32_t fourcc, int timescale,
                                               const bgav_options_t * opt);

void bgav_video_parser_destroy(bgav_video_parser_t *);

void bgav_video_parser_reset(bgav_video_parser_t *, int64_t pts);

int bgav_video_parser_parse(bgav_video_parser_t * parser);

void bgav_video_parser_add_packet(bgav_video_parser_t * parser,
                                  bgav_packet_t * p);

void bgav_video_parser_add_data(bgav_video_parser_t * parser,
                                uint8_t * data, int len, int64_t position);

const uint8_t * bgav_video_parser_get_header(bgav_video_parser_t * parser,
                                             int * len);

void bgav_video_parser_get_packet(bgav_video_parser_t * parser,
                                  bgav_packet_t * p);

int bgav_video_parser_get_out_scale(bgav_video_parser_t * parser);

void bgav_video_parser_set_eof(bgav_video_parser_t * parser);