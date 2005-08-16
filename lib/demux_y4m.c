/*****************************************************************
 
  demux_y4m.c
 
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

#include <string.h>

#include <avdec_private.h>
#include <yuv4mpeg.h>

typedef struct
  {
  y4m_stream_info_t si;
  y4m_frame_info_t fi;

  y4m_cb_reader_t reader;
  uint8_t * tmp_planes[4]; /* For YUVA4444 */
  
  int64_t frame_counter;
  } y4m_t;

/* Read function to pass to the mjpegutils */

static ssize_t read_func(void * data, void * buf, size_t len)
  {
  int result;
  bgav_input_context_t * inp = (bgav_input_context_t *)data;
  result = bgav_input_read_data(inp, (uint8_t*)buf, len);

  //  fprintf(stderr, "Read func %d %d\n", len, result);  
  
  if(result < len)
    return len - result;
  return 0;
  }

static int probe_y4m(bgav_input_context_t * input)
  {
  uint8_t probe_data[9];

  if(bgav_input_get_data(input, probe_data, 9) < 9)
    return 0;

  if(!strncmp((char*)probe_data, "YUV4MPEG2", 9))
    return 1;
  
  return 0;
  }

#if 0

/* For debugging only (incomplete) */
static void dump_stream_header(y4m_stream_info_t * si)
  {
  fprintf(stderr, "YUV4MPEG2 stream header\n");
  fprintf(stderr, "  Image size: %dx%d\n", y4m_si_get_width(si), y4m_si_get_height(si));
  fprintf(stderr, "  Interlace mode: %d\n", y4m_si_get_interlace(si));
  fprintf(stderr, "  Chroma: %d\n", y4m_si_get_chroma(si));
  }
#endif

static int open_y4m(bgav_demuxer_context_t * ctx,
                    bgav_redirector_context_t ** redir)
  {
  int result;
  y4m_t * priv;
  bgav_stream_t * s;
  y4m_ratio_t r;
  
  /* Tell the lib to accept new streams */

  y4m_accept_extensions(1);
  
  /* Allocate private data */
  
  priv = calloc(1, sizeof(*priv));
  ctx->priv = priv;
  y4m_init_stream_info(&(priv->si));

  /* Set up the reader */

  priv->reader.read = read_func;
  priv->reader.data = ctx->input;

  /* Read the stream header */

  if((result = y4m_read_stream_header_cb(&(priv->reader),  &(priv->si))) != Y4M_OK)
    {
    fprintf(stderr, "Reading stream header failed %d\n", result);
    return 0;
    }

  /* Create track table */

  ctx->tt = bgav_track_table_create(1);
  
  /* Set up the stream */
  s = bgav_track_add_video_stream(ctx->tt->current_track);
  s->data.video.format.image_width  = y4m_si_get_width(&priv->si);
  s->data.video.format.image_height = y4m_si_get_height(&priv->si);
  
  s->data.video.format.frame_width  = s->data.video.format.image_width;
  s->data.video.format.frame_height = s->data.video.format.image_height;
  
  result = y4m_si_get_interlace(&priv->si);
  switch(result)
    {
    case Y4M_ILACE_NONE:
      s->data.video.format.interlace_mode = GAVL_INTERLACE_NONE;
      break;
    case Y4M_ILACE_TOP_FIRST:
      s->data.video.format.interlace_mode = GAVL_INTERLACE_TOP_FIRST;
      break;
    case Y4M_ILACE_BOTTOM_FIRST:
      s->data.video.format.interlace_mode = GAVL_INTERLACE_BOTTOM_FIRST;
      break;
    case Y4M_ILACE_MIXED:
      fprintf(stderr, "Warning: Unsupported interlace mode detected, treating as progressive\n");
      s->data.video.format.interlace_mode = GAVL_INTERLACE_NONE;
      break;
    }

  result = y4m_si_get_chroma(&priv->si);

  switch(result)
    {
    case Y4M_CHROMA_420JPEG:
      s->data.video.format.pixelformat = GAVL_YUV_420_P;
      s->data.video.format.chroma_placement = GAVL_CHROMA_PLACEMENT_DEFAULT;
      break;
    case Y4M_CHROMA_420MPEG2:
      s->data.video.format.pixelformat = GAVL_YUV_420_P;
      s->data.video.format.chroma_placement = GAVL_CHROMA_PLACEMENT_MPEG2;
      break;
    case Y4M_CHROMA_420PALDV:
      s->data.video.format.pixelformat = GAVL_YUV_420_P;
      s->data.video.format.chroma_placement = GAVL_CHROMA_PLACEMENT_DVPAL;
      break;
    case Y4M_CHROMA_444:
      s->data.video.format.pixelformat = GAVL_YUV_444_P;
      break;
    case Y4M_CHROMA_422:
      s->data.video.format.pixelformat = GAVL_YUV_422_P;
      break;
    case Y4M_CHROMA_411:
      s->data.video.format.pixelformat = GAVL_YUV_411_P;
      break;
    case Y4M_CHROMA_MONO:
      /* Monochrome isn't supported by gavl, we choose the format with the
         smallest chroma planes to save memory */
      s->data.video.format.pixelformat = GAVL_YUV_410_P;
      break;
    case Y4M_CHROMA_444ALPHA:
      /* Must be converted to packed */
      s->data.video.format.pixelformat = GAVL_YUVA_32;
      priv->tmp_planes[0] = malloc(s->data.video.format.image_width *
                                 s->data.video.format.image_height * 4);
      
      priv->tmp_planes[1] = priv->tmp_planes[0] +
        s->data.video.format.image_width * s->data.video.format.image_height;
      priv->tmp_planes[2] = priv->tmp_planes[1] +
        s->data.video.format.image_width * s->data.video.format.image_height;
      priv->tmp_planes[3] = priv->tmp_planes[2] +
        s->data.video.format.image_width * s->data.video.format.image_height;
      break;
    }

  r = y4m_si_get_sampleaspect(&priv->si);
  s->data.video.format.pixel_width  = r.n;
  s->data.video.format.pixel_height = r.d;
  
  r = y4m_si_get_framerate(&priv->si);
  s->data.video.format.timescale      = r.n;
  s->data.video.format.frame_duration = r.d;

  s->fourcc = BGAV_MK_FOURCC('g','a','v','l');

  /* Initialize frame info (will be needed for reading later on) */
  y4m_init_frame_info(&(priv->fi));
    
  return 1;
  }

static void convert_yuva4444(uint8_t ** dst, uint8_t ** src, int size)
  {
  int i;
  uint8_t *y, *u, *v, *a, *d;
  
  y = src[0];
  u = src[1];
  v = src[2];
  a = src[3];

  d = dst[0];
  
  for(i = 0; i < size; i++)
    {
    *(d++) = *(y++);
    *(d++) = *(u++);
    *(d++) = *(v++);
    *(d++) = *(a++);
    }
  }

static int next_packet_y4m(bgav_demuxer_context_t * ctx)
  {
  bgav_packet_t * p;
  bgav_stream_t * s;
  y4m_t * priv;
    
  priv = (y4m_t*)(ctx->priv);
  
  s = ctx->tt->current_track->video_streams;
  
  p = bgav_packet_buffer_get_packet_write(s->packet_buffer, s);

  if(!p->video_frame)
    {
    p->video_frame = gavl_video_frame_create_nopadd(&(s->data.video.format));
    
    /* For monochrome format also need to clear the chroma planes */
    gavl_video_frame_clear(p->video_frame, &(s->data.video.format));
    }
  
  if(s->data.video.format.pixelformat == GAVL_YUVA_32)
    {
    if(y4m_read_frame_cb(&(priv->reader), &(priv->si),
                         &(priv->fi), priv->tmp_planes) != Y4M_OK)
      return 0;
    convert_yuva4444(p->video_frame->planes, priv->tmp_planes,
                     s->data.video.format.image_width * 
                     s->data.video.format.image_height);
    }
  else
    {
    if(y4m_read_frame_cb(&(priv->reader), &(priv->si),
                         &(priv->fi), p->video_frame->planes) != Y4M_OK)
      return 0;
    }

  p->timestamp_scaled = priv->frame_counter * s->data.video.format.frame_duration;
  priv->frame_counter++;
    
  bgav_packet_done_write(p);

  
  
  return 1;
  }

static void close_y4m(bgav_demuxer_context_t * ctx)
  {
  y4m_t * priv;
  priv = (y4m_t *)(ctx->priv);
  y4m_fini_stream_info(&(priv->si));
  y4m_fini_frame_info(&(priv->fi));
  if(priv->tmp_planes[0])
    free(priv->tmp_planes[0]);
  free(priv);
  }

bgav_demuxer_t bgav_demuxer_y4m =
  {
    probe:       probe_y4m,
    open:        open_y4m,
    next_packet: next_packet_y4m,
    close:       close_y4m
  };