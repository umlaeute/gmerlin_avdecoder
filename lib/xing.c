/*****************************************************************
 
  xing.c
 
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

#include <string.h>

#include <avdec_private.h>
#include <xing.h>

#define GET_INT32BE(b) \
(i = (b[0] << 24) | (b[1] << 16) | b[2] << 8 | b[3], b += 4, i)


int bgav_xing_header_read(bgav_xing_header_t * xing, unsigned char *buf)
  {
  int i;
  int id, mode;
  
  memset(xing, 0, sizeof(*xing));
  
  /* get selected MPEG header data */
  id = (buf[1] >> 3) & 1;
  mode = (buf[3] >> 6) & 3;
  buf += 4;

  /* Skip the sub band data */
  if (id)
    {
    /* mpeg1 */
    if (mode != 3)
      buf += 32;
    else
      buf += 17;
    }
  else
    {
    /* mpeg2 */
    if (mode != 3)
      buf += 17;
    else
      buf += 9;
    }

  if (strncmp(buf, "Xing", 4))
    return 0;
  buf += 4;
  
  xing->flags = GET_INT32BE(buf);
  
  if (xing->flags & FRAMES_FLAG)
    xing->frames = GET_INT32BE(buf);
  if (xing->frames < 1)
    xing->frames = 1;
  if (xing->flags & BYTES_FLAG)
    xing->bytes = GET_INT32BE(buf);

  if (xing->flags & TOC_FLAG)
    {
    for (i = 0; i < 100; i++)
      xing->toc[i] = buf[i];
    buf += 100;
    }
  return 1;
  }

#define CLAMP(i, min, max) i<min?min:(i>max?max:i)
#define MIN(i1,i2) i1<i2?i1:i2

int64_t bgav_xing_get_seek_position(bgav_xing_header_t * xing, float percent)
  {
          /* interpolate in TOC to get file seek point in bytes */
  int a, seekpoint;
  float fa, fb, fx;
  
  percent = CLAMP(percent, 0.0, 100.0);
  a = MIN(percent, 99);
  
  fa = xing->toc[a];
  
  if (a < 99)
    fb = xing->toc[a + 1];
  else
    fb = 256;
  
  fx = fa + (fb - fa) * (percent - a);
  seekpoint = (1.0f / 256.0f) * fx * xing->bytes;
  
  return seekpoint;
  
  }
