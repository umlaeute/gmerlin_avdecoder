/*****************************************************************
 
  id3.h
 
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

/* ID3V1 */

typedef struct bgav_id3v1_tag_s bgav_id3v1_tag_t;

bgav_id3v1_tag_t * bgav_id3v1_read(bgav_input_context_t * input);

int bgav_id3v1_probe(bgav_input_context_t * input);

void bgav_id3v1_destroy(bgav_id3v1_tag_t *);

void bgav_id3v1_2_metadata(bgav_id3v1_tag_t*, bgav_metadata_t * m);

const char * bgav_id3v1_get_genre(int id);

/* ID3V2 */

typedef struct bgav_id3v2_tag_s bgav_id3v2_tag_t;

/* Offset must be 128 bytes before the end */

int bgav_id3v2_probe(bgav_input_context_t * input);

void bgav_id3v2_dump(bgav_id3v2_tag_t * t);

bgav_id3v2_tag_t * bgav_id3v2_read(bgav_input_context_t * input);

void bgav_id3v2_destroy(bgav_id3v2_tag_t*);
void bgav_id3v2_2_metadata(bgav_id3v2_tag_t*, bgav_metadata_t*m);

int bgav_id3v2_total_bytes(bgav_id3v2_tag_t*);

/*
  APE Tags
  http://hydrogenaudio.org/musepack/klemm/www.personal.uni-jena.de/~pfk/mpp/sv8/apetag.html
*/

typedef struct bgav_ape_tag_s bgav_ape_tag_t;

/* Offset must be 32 bytes before the end */

int bgav_ape_tag_probe(bgav_input_context_t * input, int * tag_size);

bgav_ape_tag_t * bgav_ape_tag_read(bgav_input_context_t * input, int tag_size);

void bgav_ape_tag_2_metadata(bgav_ape_tag_t * tag, bgav_metadata_t * m);

void bgav_ape_tag_destroy(bgav_ape_tag_t * tag);

void bgav_ape_tag_dump(bgav_ape_tag_t * tag);

