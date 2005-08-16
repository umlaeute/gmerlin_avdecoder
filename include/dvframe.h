/*****************************************************************

  dvframe.h

  Copyright (c) 2005 by Burkhard Plaum - plaum@ipf.uni-stuttgart.de

  http://gmerlin.sourceforge.net

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.

*****************************************************************/

#define DV_HEADER_SIZE 6

/*
 *  Handler for DV frames
 *  It detects most interesting parameters from the DV
 *  data and can extract audio.
 */

typedef struct bgav_dv_dec_s bgav_dv_dec_t;

bgav_dv_dec_t * bgav_dv_dec_create();
void bgav_dv_dec_destroy(bgav_dv_dec_t*);

/* Sets the header for parsing. Data must be DV_HEADER_SIZE bytes long */

void bgav_dv_dec_set_header(bgav_dv_dec_t*, uint8_t * data);

int bgav_dv_dec_get_frame_size(bgav_dv_dec_t*);

/* Call this after seeking */
void bgav_dv_dec_set_frame_counter(bgav_dv_dec_t*, int64_t count);


/* Sets the frame for parsing. data must be frame_size bytes long */

void bgav_dv_dec_set_frame(bgav_dv_dec_t*, uint8_t * data);

/* Set up audio and video streams */

void bgav_dv_dec_init_audio(bgav_dv_dec_t*, bgav_stream_t * s);
void bgav_dv_dec_init_video(bgav_dv_dec_t*, bgav_stream_t * s);

/* Extract audio and video packets suitable for the decoders */

int bgav_dv_dec_get_audio_packet(bgav_dv_dec_t*, bgav_packet_t * p);
void bgav_dv_dec_get_video_packet(bgav_dv_dec_t*, bgav_packet_t * p);
