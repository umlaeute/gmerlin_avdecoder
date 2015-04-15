/*****************************************************************
 * gmerlin-avdecoder - a general purpose multimedia decoding library
 *
 * Copyright (c) 2001 - 2012 Members of the Gmerlin project
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

#include <config.h>
#include <avdec_private.h>

#include <X11/Xlib.h>

#include AVCODEC_HEADER
#include VAAPI_HEADER

#include <va/va.h>
#include <bgav_vaapi.h>


#include <gavl/hw_vaapi_x11.h>

static int get_profile(enum AVCodecID id)
  {
  int profile;
  switch(id)
    {
    case CODEC_ID_MPEG2VIDEO:
      profile = VAProfileMPEG2Main;
      break;
    case CODEC_ID_MPEG4:
    case CODEC_ID_H263:
      profile = VAProfileMPEG4AdvancedSimple;
      break;
    case CODEC_ID_H264:
      profile = VAProfileH264High;
      break;
    case CODEC_ID_WMV3:
      profile = VAProfileVC1Main;
      break;
    case CODEC_ID_VC1:
      profile = VAProfileVC1Advanced;
      break;
    default:
      profile = VAProfileNone;
      break;
    }
  return profile;
  }

static int has_profile(VADisplay dpy, VAProfile profile)
  {
  VAProfile * p = NULL;
  int num, i;
  VAStatus status;
  int ret = 0;

  num = vaMaxNumProfiles(dpy);

  p = calloc(num, sizeof(*p));

  if((status = vaQueryConfigProfiles(dpy, p, &num)) != VA_STATUS_SUCCESS)
    goto fail;

  for(i = 0; i < num; i++)
    {
    if(p[i] == profile)
      {
      ret = 1;
      break;
      }
    }

  fail:

  if(p)
    free(p);
  return ret;
  }

static int has_entrypoint(VADisplay dpy, VAProfile profile,
                          VAEntrypoint entrypoint)
  {
  int ret = 0;
  int num;
  VAStatus status;
  VAEntrypoint * ep = NULL;
  int i;

  num = vaMaxNumEntrypoints(dpy);
  ep = calloc(num, sizeof(*ep));
  
  if((status = vaQueryConfigEntrypoints(dpy, profile, ep, &num)) != VA_STATUS_SUCCESS)
    goto fail;

  for(i = 0; i < num; i++)
    {
    if(ep[i] == entrypoint)
      {
      ret = 1;
      break;
      }
    }
  fail:
  if(ep)
    free(ep);

  return ret;
  }

static VAEntrypoint get_entrypoint(enum PixelFormat pfmt)
  {
  switch(pfmt)
    {
    case AV_PIX_FMT_VAAPI_MOCO:
      return VAEntrypointVLD;
      break;
    case AV_PIX_FMT_VAAPI_IDCT:
      return VAEntrypointIDCT;
      break;
    case AV_PIX_FMT_VAAPI_VLD:
      return VAEntrypointMoComp;
      break;
    default:
      break;
    }
  return 0;
  }

int bgav_vaapi_init(bgav_vaapi_t * v, AVCodecContext * avctx, enum PixelFormat pfmt)
  {
  VAProfile profile;
  VAEntrypoint ep;
  VAConfigAttrib attr;
  VAStatus status;

  if((profile = get_profile(avctx->codec_id)) == VAProfileNone)
    goto fail;

  /* Connect to hardware */
  if(!(v->hwctx = gavl_hw_ctx_create_vaapi_x11(NULL)))
    goto fail;
  v->vaapi_ctx.display = gavl_hw_ctx_vaapi_x11_get_va_display(v->hwctx);

  /* Make IDs invalid */
  v->vaapi_ctx.config_id = VA_INVALID_ID;
  v->vaapi_ctx.context_id = VA_INVALID_ID;

  if(!has_profile(v->vaapi_ctx.display, profile))
    goto fail;

  if(!(ep = get_entrypoint(pfmt)))
    goto fail;

  if(!has_entrypoint(v->vaapi_ctx.display, profile, ep))
    goto fail;

  /* Get surface format */
  
  attr.type = VAConfigAttribRTFormat;
  vaGetConfigAttributes(v->vaapi_ctx.display, profile, ep, &attr, 1);

  if(!(attr.value & VA_RT_FORMAT_YUV420))
    goto fail;
  
  /* Create config */
  
  if(!(status = vaCreateConfig(v->vaapi_ctx.display, profile, ep,
                               &attr, 1, &v->vaapi_ctx.config_id)) != VA_STATUS_SUCCESS)
    goto fail;

  /* Create surface */
  

  /* Create context */

  
  
  return 1; 
  
  fail: // Cleanup

  bgav_vaapi_cleanup(v);
  return 0;
  }

void bgav_vaapi_cleanup(bgav_vaapi_t * v)
  {
  if(!v->hwctx)
    return;

  if(v->vaapi_ctx.config_id != VA_INVALID_ID)
    vaDestroyConfig(v->vaapi_ctx.display, v->vaapi_ctx.config_id);

  if(v->vaapi_ctx.context_id != VA_INVALID_ID)
    vaDestroyConfig(v->vaapi_ctx.display, v->vaapi_ctx.context_id);

  gavl_hw_ctx_destroy(v->hwctx);
  }
