/*****************************************************************
 
  video_rtjpeg.c
 
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

/* Ported from libquicktime */
#include <stdlib.h>

#include <RTjpeg.h>
#include <avdec_private.h>

#define BLOCK_SIZE 16

#define PADD(x) ((x+BLOCK_SIZE-1)/BLOCK_SIZE)*BLOCK_SIZE

typedef struct
  {
  gavl_video_frame_t * frame;
  RTjpeg_t * rtjpeg;
  } rtjpeg_priv_t;

static int init_rtjpeg(bgav_stream_t * s)
  {
  rtjpeg_priv_t * priv;
  priv = calloc(1, sizeof(*priv));
  
  s->data.video.decoder->priv = priv;

  priv->rtjpeg = RTjpeg_init();

  s->data.video.format.frame_width = PADD(s->data.video.format.image_width);
  s->data.video.format.frame_height = PADD(s->data.video.format.image_height);
  s->data.video.format.colorspace = GAVL_YUV_420_P;
  
  priv->frame = gavl_video_frame_create(&(s->data.video.format));

  s->description = bgav_sprintf("RTjpeg");
  
  return 1;
  }
  
static int decode_rtjpeg(bgav_stream_t * s, gavl_video_frame_t * f)
  {
  rtjpeg_priv_t * priv;
  bgav_packet_t * p;
  priv = (rtjpeg_priv_t*)(s->data.video.decoder->priv);

  /* We assume one frame per packet */
  
  p = bgav_demuxer_get_packet_read(s->demuxer, s);
  if(!p)
    return 0;

  /* Skip frame */
  if(!f)
    {
    bgav_demuxer_done_packet_read(s->demuxer, p);
    return 1;
    }

  RTjpeg_decompress(priv->rtjpeg, p->data, priv->frame->planes);  
  gavl_video_frame_copy(&(s->data.video.format),
                        f, priv->frame);
  bgav_demuxer_done_packet_read(s->demuxer, p);
  return 1;
  }


static void close_rtjpeg(bgav_stream_t * s)
  {
  rtjpeg_priv_t * priv;
  priv = (rtjpeg_priv_t*)(s->data.video.decoder->priv);

  RTjpeg_close(priv->rtjpeg);
  gavl_video_frame_destroy(priv->frame);
  free(priv);
  }

static bgav_video_decoder_t rtjpeg_decoder =
  {
    name:   "rtjpeg video decoder",
    fourccs:  (uint32_t[]){ BGAV_MK_FOURCC('R', 'T', 'J', '0'), 0x00  },
    init:   init_rtjpeg,
    decode: decode_rtjpeg,
    close:  close_rtjpeg,
  };

void bgav_init_video_decoders_rtjpeg()
  {
  bgav_video_decoder_register(&rtjpeg_decoder);
  }