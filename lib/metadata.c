
/*****************************************************************
 
  metadata.c
 
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


#define MY_FREE(ptr) if(ptr)free(ptr);ptr=NULL;

void bgav_metadata_free(bgav_metadata_t * m)
  {
  MY_FREE(m->author);
  MY_FREE(m->title);
  MY_FREE(m->comment);
  MY_FREE(m->copyright);
  MY_FREE(m->album);
  MY_FREE(m->artist);
  MY_FREE(m->date);
  MY_FREE(m->genre);
  memset(m, 0, sizeof(*m));
  }

#define MERGE_S(s) \
if(dst->s) free(dst->s);\
if(src1->s) \
  dst->s=bgav_strndup(src1->s,NULL);\
else if(src2->s) \
  dst->s=bgav_strndup(src2->s,NULL);\
else \
  dst->s=NULL;

#define MERGE_I(s) \
if(src1->s) \
  dst->s=src1->s;\
else if(src2->s) \
  dst->s=src2->s;\
else \
  dst->s=0;

void bgav_metadata_merge(bgav_metadata_t * dst,
                         bgav_metadata_t * src1,
                         bgav_metadata_t * src2)
  {
  MERGE_S(author);
  MERGE_S(title);
  MERGE_S(comment);
  MERGE_S(copyright);
  MERGE_S(album);
  MERGE_S(artist);
  MERGE_S(date);
  MERGE_S(genre);

  MERGE_I(track);
  }

#define PS(label, str) if(str)fprintf(stderr, "%s%s\n", label, str);
#define PI(label, i)   if(i)fprintf(stderr, "%s%d\n", label, i);

void bgav_metadata_dump(bgav_metadata_t*m)
  {
  fprintf(stderr, "==== Metadata =====\n");
  
  PS("Author:    ", m->author);
  PS("Title:     ", m->title);
  PS("Comment:   ", m->comment);
  PS("Copyright: ", m->copyright);
  PS("Album:     ", m->album);
  PS("Artist:    ", m->artist);
  PS("Genre:     ", m->genre);
  PI("Track:     ", m->track);
  PS("Date:      ", m->date);
  fprintf(stderr, "==== End Metadata ==\n");
  }