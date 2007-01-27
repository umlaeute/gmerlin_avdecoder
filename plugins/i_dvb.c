/*****************************************************************
 
  i_dvb.c
 
  Copyright (c) 2006 by Burkhard Plaum - plaum@ipf.uni-stuttgart.de
 
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
#include <stdio.h>
#include <string.h>

#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>
#include <avdec.h>

#include "avdec_common.h"

#include <cdio/cdio.h> // Version

static int open_dvb(void * priv, const char * location)
  {
  avdec_priv * avdec;
  bgav_options_t * opt;
  avdec = (avdec_priv*)(priv);

  avdec->dec = bgav_create();
  opt = bgav_get_options(avdec->dec);
  
  bgav_options_copy(opt, avdec->opt);
  
  if(!bgav_open_dvb(avdec->dec, location))
    return 0;
  return bg_avdec_init(avdec);
  }

static bg_device_info_t * find_devices_dvb()
  {
  bg_device_info_t * ret;
  bgav_device_info_t * dev;
  dev = bgav_find_devices_dvb();
  ret = bg_avdec_get_devices(dev);
  bgav_device_info_destroy(dev);
  return ret;
  }

static int check_device_dvb(const char * device, char ** name)
  {
  return bgav_check_device_dvb(device, name);
  }

static bg_parameter_info_t parameters[] =
  {
    {
      name:        "dvb_channels_file",
      long_name:   "Channel file",
      type:        BG_PARAMETER_FILE,
      help_string: "The channels file must have the format of the dvb-utils\
 programs (like szap, tzap). If you don't set this file,\
 several locations like $HOME/.tzap/channels.conf will be\
 searched."
    },
    PARAM_DYNRANGE,
    { /* End of parameters */ }
  };

static bg_parameter_info_t * get_parameters_dvb(void * priv)
  {
  return parameters;
  }

bg_input_plugin_t the_plugin =
  {
    common:
    {
      name:          "i_dvb",
      long_name:     "DVB Player",
      type:          BG_PLUGIN_INPUT,
      flags:         BG_PLUGIN_TUNER,
      priority:      BG_PLUGIN_PRIORITY_MAX,
      create:        bg_avdec_create,
      destroy:       bg_avdec_destroy,
      get_parameters: get_parameters_dvb,
      set_parameter:  bg_avdec_set_parameter,
      find_devices: find_devices_dvb,
      check_device: check_device_dvb,
      get_error:    bg_avdec_get_error      
    },
    protocols: "dvb",
    
    set_callbacks: bg_avdec_set_callbacks,
    /* Open file/device */
    open: open_dvb,

    //    set_callbacks: set_callbacks_avdec,
    /* For file and network plugins, this can be NULL */
    get_num_tracks: bg_avdec_get_num_tracks,
    /* Return track information */
    get_track_info: bg_avdec_get_track_info,

    /* Set track */
    set_track:             bg_avdec_set_track,
    /* Set streams */
    set_audio_stream:      bg_avdec_set_audio_stream,
    set_video_stream:      bg_avdec_set_video_stream,
    set_subtitle_stream:   bg_avdec_set_subtitle_stream,

    /*
     *  Start decoding.
     *  Track info is the track, which should be played.
     *  The plugin must take care of the "active" fields
     *  in the stream infos to check out, which streams are to be decoded
     */
    start:                 bg_avdec_start,
    /* Read one audio frame (returns FALSE on EOF) */
    read_audio_samples:    bg_avdec_read_audio,
    /* Read one video frame (returns FALSE on EOF) */
    read_video_frame:      bg_avdec_read_video,

    //    has_subtitle:          bg_avdec_has_subtitle,
    //    read_subtitle_overlay: bg_avdec_read_subtitle_overlay,
    
    /* Stop playback, close all decoders */
    stop:         NULL,
    close:        bg_avdec_close,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;