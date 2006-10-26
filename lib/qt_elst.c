/*****************************************************************
 
  qt_elst.c
 
  Copyright (c) 2003-2006 by Burkhard Plaum - plaum@ipf.uni-stuttgart.de
 
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

#include <avdec_private.h>
#include <qt.h>


#if 0
typedef struct
  {
  uint32_t num_entries;
  
  struct
    {
    uint32_t duration;
    uint32_t media_time;
    uint32_t media_rate;
    } * table;
  } qt_elst_t;
#endif


int bgav_qt_elst_read(qt_atom_header_t * h, bgav_input_context_t * input,
                      qt_elst_t * ret)
  {
  int i;
  READ_VERSION_AND_FLAGS;
  if(!bgav_input_read_32_be(input, &ret->num_entries))
    return 0;

  ret->table = calloc(ret->num_entries, sizeof(*ret->table));
  
  for(i = 0; i < ret->num_entries; i++)
    {
    if(!bgav_input_read_32_be(input, &ret->table[i].duration) ||
       !bgav_input_read_32_be(input, &ret->table[i].media_time) ||
       !bgav_qt_read_fixed32(input, &ret->table[i].media_rate))
      return 0;
    }
  return 1;
  }

void bgav_qt_elst_free(qt_elst_t * e)
  {
  if(e->table) free(e->table);
  }


void bgav_qt_elst_dump(int indent, qt_elst_t * e)
  {
  int i;
   
  
  bgav_diprintf(indent, "elst\n");
  bgav_diprintf(indent+2, "version:     %d\n", e->version);
  bgav_diprintf(indent+2, "flags:       %06x\n", e->flags);
  bgav_diprintf(indent+2, "num_entries: %d\n", e->num_entries);

  for(i = 0; i < e->num_entries; i++)
    {
    bgav_diprintf(indent+4, "duration: %d, media_time: %d, media_rate: %f\n",
                  e->table[i].duration, e->table[i].media_time, e->table[i].media_rate);
    }
  bgav_diprintf(indent, "end of elst\n");
  }
