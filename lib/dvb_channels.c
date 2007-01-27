/*****************************************************************
 
  dvb_channels.c
 
  Copyright (c) 2007 by Burkhard Plaum - plaum@ipf.uni-stuttgart.de
 
  http://gmerlin.sourceforge.net
 
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 
*****************************************************************/

#include <linux/dvb/frontend.h>

#include <avdec_private.h>
#include <dvb_channels.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define LOG_DOMAIN "dvb_channels"


typedef struct {
char *name;
int value;
  } Param;

static const Param inversion_list [] = {
  { "INVERSION_OFF", INVERSION_OFF },
  { "INVERSION_ON", INVERSION_ON },
  { "INVERSION_AUTO", INVERSION_AUTO },
  { NULL, 0 }
};

static const Param bw_list [] = {
  { "BANDWIDTH_6_MHZ", BANDWIDTH_6_MHZ },
  { "BANDWIDTH_7_MHZ", BANDWIDTH_7_MHZ },
  { "BANDWIDTH_8_MHZ", BANDWIDTH_8_MHZ },
  { NULL, 0 }
};

static const Param fec_list [] = {
  { "FEC_1_2", FEC_1_2 },
  { "FEC_2_3", FEC_2_3 },
  { "FEC_3_4", FEC_3_4 },
  { "FEC_4_5", FEC_4_5 },
  { "FEC_5_6", FEC_5_6 },
  { "FEC_6_7", FEC_6_7 },
  { "FEC_7_8", FEC_7_8 },
  { "FEC_8_9", FEC_8_9 },
  { "FEC_AUTO", FEC_AUTO },
  { "FEC_NONE", FEC_NONE },
  { NULL, 0 }
};

static const Param guard_list [] = {
  {"GUARD_INTERVAL_1_16", GUARD_INTERVAL_1_16},
  {"GUARD_INTERVAL_1_32", GUARD_INTERVAL_1_32},
  {"GUARD_INTERVAL_1_4", GUARD_INTERVAL_1_4},
  {"GUARD_INTERVAL_1_8", GUARD_INTERVAL_1_8},
  { NULL, 0 }
};

static const Param hierarchy_list [] = {
  { "HIERARCHY_1", HIERARCHY_1 },
  { "HIERARCHY_2", HIERARCHY_2 },
  { "HIERARCHY_4", HIERARCHY_4 },
  { "HIERARCHY_NONE", HIERARCHY_NONE },
  { NULL, 0 }
};

static const Param atsc_list [] = {
  { "8VSB", VSB_8 },
  { "QAM_256", QAM_256 },
  { "QAM_64", QAM_64 },
  { "QAM", QAM_AUTO },
  { NULL, 0 }
};

static const Param qam_list [] = {
  { "QPSK", QPSK },
  { "QAM_128", QAM_128 },
  { "QAM_16", QAM_16 },
  { "QAM_256", QAM_256 },
  { "QAM_32", QAM_32 },
  { "QAM_64", QAM_64 },
  { NULL, 0 }
};

static const Param transmissionmode_list [] = {
  { "TRANSMISSION_MODE_2K", TRANSMISSION_MODE_2K },
  { "TRANSMISSION_MODE_8K", TRANSMISSION_MODE_8K },
  { NULL, 0 }
};

static int find_param(const Param *list, const char *name)
  {
  while (list->name && strcmp(list->name, name))
    list++;
  return list->value;
  }

static char * find_string(const Param *list, int val)
  {
  while (list->name && (list->value != val))
    list++;
  return list->name;
  }

bgav_dvb_channel_info_t *
bgav_dvb_channels_load(const bgav_options_t * opt,
                       fe_type_t type, int * num)
  {
  int i;
  unsigned long freq;
  int is_open = 0;
  char * home_dir;
  char * filename = (char*)0;

  char * line = (char*)0;
  int line_alloc = 0;
  char ** entries;
  
  bgav_dvb_channel_info_t * ret = (bgav_dvb_channel_info_t*)0;
  bgav_dvb_channel_info_t * channel;
  
  bgav_input_context_t * input;

  input = bgav_input_create(opt);
  
  /* Look for the file */

  if(opt->dvb_channels_file)
    {
    if(!bgav_input_open(input, opt->dvb_channels_file))
      {
      bgav_log(opt, BGAV_LOG_ERROR, LOG_DOMAIN,
               "Channels file %s cannot be opened");
      goto fail;
      }
    }
  else // Test some files
    {
    home_dir = getenv("HOME");

    if(!home_dir)
      {
      bgav_log(opt, BGAV_LOG_ERROR, LOG_DOMAIN,
               "Channels file cannot be found (home directory unset)");
      goto fail;
      }
    
    if(type == FE_QPSK)
      {
      filename = bgav_sprintf("%s/.szap/channels.conf", home_dir);
      
      if(!bgav_check_file_read(filename))
        {
        free(filename);
        filename = (char*)0;
        }
      }
    else if(type == FE_OFDM)
      {
      filename = bgav_sprintf("%s/.tzap/channels.conf", home_dir);
      
      if(!bgav_check_file_read(filename))
        {
        free(filename);
        filename = (char*)0;
        }
      }

    if(!filename)
      {
      filename = bgav_sprintf("%s/.mplayer/channels.conf", home_dir);
      if(!bgav_check_file_read(filename))
        {
        free(filename);
        filename = (char*)0;
        }
      }

    if(!filename)
      {
      filename = bgav_sprintf("%s/.xine/channels.conf", home_dir);
      if(!bgav_check_file_read(filename))
        {
        free(filename);
        filename = (char*)0;
        }
      }
    }

  if(!filename)
    {
    bgav_log(opt, BGAV_LOG_ERROR, LOG_DOMAIN,
             "Channels file cannot be found");
    goto fail;
    }

  if(!bgav_input_open(input, filename))
    {
    bgav_log(opt, BGAV_LOG_ERROR, LOG_DOMAIN,
             "Channels file %s cannot be opened: %s", filename, strerror(errno));
    goto fail;
    }

  /* Read lines */

  *num = 0;
  while(1)
    {
    i = 0;
    if(!bgav_input_read_line(input, &line, &line_alloc, 0, (int*)0))
      break;
    
    entries = bgav_stringbreak(line, ':');

    ret = realloc(ret, ((*num)+1)*sizeof(*ret));
    channel = ret + (*num);
    
    memset(channel, 0, sizeof(*channel));
    
    // Channel name is always first
    channel->name = bgav_strdup(entries[i++]);
    // Frequency is second
    freq = strtoul(entries[i++], NULL, 0);
    
    switch(type)
      {
      case FE_QPSK: // QPSK (DVB-S)
        // <channel name>:<frequency>:<polarisation>:<sat_no>:<sym_rate>:
        // <vpid>:<apid>

        if(freq > 11700)
          {
          channel->front_param.frequency = (freq - 10600)*1000;
          channel->tone = 1;
          }
        else
          {
          channel->front_param.frequency = (freq - 9750)*1000;
          channel->tone = 0;
          }
        channel->front_param.inversion = INVERSION_AUTO;

        /* find out the polarisation */
        channel->pol = (entries[i++][0] == 'h' ? 0 : 1);

        /* satellite number */
        channel->sat_no = strtoul(entries[i++], NULL, 0);

        /* symbol rate */
        channel->front_param.u.qpsk.symbol_rate = strtoul(entries[i++], NULL, 0) * 1000;

        channel->front_param.u.qpsk.fec_inner = FEC_AUTO;
        
        break;
      case FE_QAM:  // QAM (DVB-C)
        // <channel name>:<frequency>:<inversion>:<sym_rate>:<fec>:<qam>:
        // <vpid>:<apid>
        channel->front_param.frequency = freq;

        /* Inversion */
        channel->front_param.inversion = find_param(inversion_list, entries[i++]);

        /* Symbol rate */
        channel->front_param.u.qam.symbol_rate = strtoul(entries[i++], NULL, 0);

        /* FEC */
        channel->front_param.u.qam.fec_inner = find_param(fec_list, entries[i++]);

        /* find out the qam */
        channel->front_param.u.qam.modulation = find_param(qam_list, entries[i++]);
        
        break;
      case FE_OFDM: // QFDM (DVB-T)
        // <channel name>:<frequency>:<inversion>:<bw>:<fec_hp>:
        // <fec_lp>:<qam>:<transmissionm>:<guardlist>:<hierarchinfo>:
        // <vpid>:<apid>

        /* DVB-T frequency is in kHz - workaround broken channels.confs */
        if (freq < 1000000) 
          freq*=1000;
        channel->front_param.frequency = freq;

        /* find out the inversion */
        channel->front_param.inversion = find_param(inversion_list, entries[i++]);

        /* find out the bandwidth */
        channel->front_param.u.ofdm.bandwidth = find_param(bw_list, entries[i++]);

        /* find out the fec_hp */
        channel->front_param.u.ofdm.code_rate_HP = find_param(fec_list, entries[i++]);

        /* find out the fec_lp */
        channel->front_param.u.ofdm.code_rate_LP = find_param(fec_list, entries[i++]);

        /* find out the qam */
        channel->front_param.u.ofdm.constellation = find_param(qam_list, entries[i++]);

        /* find out the transmission mode */
        channel->front_param.u.ofdm.transmission_mode =
          find_param(transmissionmode_list, entries[i++]);

        /* guard list */
        channel->front_param.u.ofdm.guard_interval = find_param(guard_list, entries[i++]);

        /* hierarchy_information */
        channel->front_param.u.ofdm.hierarchy_information =
          find_param(hierarchy_list, entries[i++]);
        
        break;
      case FE_ATSC: // ATSC (DBV-A)
        // <channel name>:<frequency>:<qam>:<vpid>:<apid>
        
        channel->front_param.u.vsb.modulation = find_param(atsc_list, entries[i++]);
        
        break;
      }
    
    channel->video_pid  = atoi(entries[i++]);
    channel->audio_pid  = atoi(entries[i++]);
    channel->service_id = atoi(entries[i++]);
    
    bgav_stringbreak_free(entries);
    (*num)++;
    }
  //  dvb_channels_dump(ret, type, *num);

  fail:

  if(line)
    free(line);
  
  if(filename)
    free(filename);
  if(is_open)
    bgav_input_close(input);
  bgav_input_destroy(input);
  
  return ret;

  }

void dvb_channels_destroy(bgav_dvb_channel_info_t * channels, int num)
  {
  int i;
  for(i = 0; i < num; i++)
    {
    if(channels[i].name)
      free(channels[i].name);
    }
  free(channels);
  }

void dvb_channels_dump(bgav_dvb_channel_info_t * channels, fe_type_t type, int num)
  {
  int i;
  for(i = 0; i < num; i++)
    {
    bgav_dprintf("Channel %d:  %s\n", i+1, channels[i].name);
    bgav_dprintf("  Frequency: %ld\n", channels[i].front_param.frequency);
    bgav_dprintf("  Inversion: %s\n", find_string(inversion_list,
                                                  channels[i].front_param.inversion));
    switch(type)
      {
      case FE_QPSK:
        bgav_dprintf("  Polarization:     %s\n", (channels[i].pol ? "Vertical": "Horizontal"));
        bgav_dprintf("  satellite number: %d\n", channels[i].sat_no);
        bgav_dprintf("  Symbol rate:      %d\n", channels[i].front_param.u.qpsk.symbol_rate);
        bgav_dprintf("  FEC:              &s\n",
                     find_string(fec_list, channels[i].front_param.u.qpsk.fec_inner));
        
        break;
      case FE_QAM:
        bgav_dprintf("  Symbol rate:      %d\n",
                     channels[i].front_param.u.qam.symbol_rate);
        bgav_dprintf("  FEC:              %s\n",
                     find_string(fec_list, channels[i].front_param.u.qam.fec_inner));
        bgav_dprintf("  Modulation:       %s\n",
                     find_string(fec_list, channels[i].front_param.u.qam.modulation));
        break;
      case FE_OFDM:
        /* find out the bandwidth */
        bgav_dprintf("  Bandwidth:         %s\n",
                     find_string(bw_list, channels[i].front_param.u.ofdm.bandwidth));
        
        /* find out the fec_hp */
        bgav_dprintf("  Code Rate (HP):    %s\n",
                     find_string(fec_list, channels[i].front_param.u.ofdm.code_rate_HP));
        
        /* find out the fec_lp */
        bgav_dprintf("  Code Rate (LP):    %s\n",
                     find_string(fec_list, channels[i].front_param.u.ofdm.code_rate_LP));

        /* find out the qam */
        bgav_dprintf("  QAM:               %s\n",
                     find_string(qam_list, channels[i].front_param.u.ofdm.constellation));
        
        /* find out the transmission mode */
        bgav_dprintf("  Transmission mode: %s\n",
                     find_string(transmissionmode_list,
                                 channels[i].front_param.u.ofdm.transmission_mode));
        
        /* guard list */
        bgav_dprintf("  Guard interval:    %s\n",
                     find_string(guard_list,
                                 channels[i].front_param.u.ofdm.guard_interval));

        /* hierarchy_information */
        bgav_dprintf("  Hirarchy Info:     %s\n",
                     find_string(hierarchy_list,
                                 channels[i].front_param.u.ofdm.hierarchy_information));
        break;
      case FE_ATSC:
        bgav_dprintf("  Modulation:        %s\n",
                     find_string(atsc_list, channels[i].front_param.u.vsb.modulation));
        break;
      }
    bgav_dprintf("  Audio PID:  %d\n", channels[i].audio_pid);
    bgav_dprintf("  Video PID:  %d\n", channels[i].video_pid);
    bgav_dprintf("  Service ID: %d\n", channels[i].service_id);
    
    }
  
  }