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

#include AVCODEC_HEADER
#include VAAPI_HEADER

#include <bgav_vaapi.h>

#include <va/va.h>


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
      profile = -1;
      break;
    }
  return profile;
  }

static int has_profile(vaDisplay dpy, VAProfile profile)
  {

  }

static int has_entrypoint(vaDisplay dpy, VAProfile profile,
                            VAEntrypoint entrypoint)
  {

  }

int bgav_vaapi_init(bgav_vaapi_t * v, AVCodecContext * avctx)
  {
  int profile = get_profile(avctx->codec_id);
  if(profile < 0)
    return 0;

  
  }

void bgav_vaapi_cleanup(bgav_vaapi_t * v)
  {

  }
