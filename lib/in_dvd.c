/*****************************************************************
 
  in_dvd.c
 
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

#include <stdio.h>
#include <stdlib.h>
#include <avdec_private.h>

#if defined HAVE_CDIO && defined HAVE_DVDREAD

#include <cdio/cdio.h>
#include <cdio/device.h>
#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_read.h>

extern bgav_demuxer_t bgav_demuxer_mpegps;

typedef struct
  {
  dvd_reader_t * dvd_reader;
  dvd_file_t * dvd_file;

  ifo_handle_t * vmg_ifo;
  ifo_handle_t * vts_ifo;

  int current_vts;

  pgc_t * pgc;

  int current_sector, current_cell;
  
  } dvd_t;

typedef struct
  {
  int title;
  int chapter;
  int angle;
  } track_priv_t;

static void open_vts(dvd_t * dvd, int vts_index, int open_file)
  {
  if(vts_index == dvd->current_vts)
    {
    if(!dvd->dvd_file && open_file)
      dvd->dvd_file = DVDOpenFile(dvd->dvd_reader, vts_index, DVD_READ_TITLE_VOBS);
    return;
    }
  if(dvd->vts_ifo)
    ifoClose(dvd->vts_ifo);

  if(dvd->dvd_file)
    DVDCloseFile(dvd->dvd_file);
  if(open_file)
    dvd->dvd_file = DVDOpenFile(dvd->dvd_reader, vts_index, DVD_READ_TITLE_VOBS);
  
  dvd->vts_ifo = ifoOpen(dvd->dvd_reader, vts_index);
  dvd->current_vts = vts_index;
  }

static gavl_time_t convert_time(dvd_time_t * time)
  {
  gavl_time_t ret;
  int hours, minutes, seconds;

  hours   = (time->hour   >> 4) * 10 + (time->hour   & 0x0f);
  minutes = (time->minute >> 4) * 10 + (time->minute & 0x0f);
  seconds = (time->second >> 4) * 10 + (time->second & 0x0f);

  ret = seconds + 60 * (minutes + 60 * hours);
  ret *= GAVL_TIME_SCALE;
  
  switch((time->frame_u & 0xc0) >> 6)
    {
    case 1:
      ret += gavl_frames_to_time(25, 1, time->frame_u & 0x3f);
      break;
    case 3:
      ret += gavl_frames_to_time(30000, 1001, time->frame_u & 0x3f);
      break;
    }
  
  return ret;
  }

static void setup_track(bgav_input_context_t * ctx,
                        int title, int chapter, int angle)
  {
  int start_cell, end_cell;
  int ac3_id = 0, dts_id = 0, lpcm_id = 0, mpa_id = 0;

  audio_attr_t * audio_attr;
  int i;
  bgav_stream_t * s;
  bgav_track_t * new_track;
  tt_srpt_t *ttsrpt;
  pgc_t * pgc;
  vts_ptt_srpt_t *vts_ptt_srpt;
  int ttn, pgn;
  int pgc_id;
  track_priv_t * track_priv;
  dvd_t * dvd = (dvd_t*)(ctx->priv);
  ttsrpt = dvd->vmg_ifo->tt_srpt;

  new_track = bgav_track_table_append_track(ctx->tt);
  track_priv = calloc(1, sizeof(*track_priv));
  track_priv->title = title;
  track_priv->chapter = chapter;
  track_priv->angle = angle;

  new_track->priv = track_priv;

  /* Open VTS */
  open_vts(dvd, ttsrpt->title[title].title_set_nr, 0);

  /* Get the program chain for this track/chapter */
  ttn = ttsrpt->title[title].vts_ttn;
  vts_ptt_srpt = dvd->vts_ifo->vts_ptt_srpt;
  pgc_id = vts_ptt_srpt->title[ttn - 1].ptt[chapter].pgcn;
  pgc = dvd->vts_ifo->vts_pgcit->pgci_srp[pgc_id - 1].pgc;
  pgn = vts_ptt_srpt->title[ttn - 1].ptt[track_priv->chapter].pgn;
  
  start_cell = pgc->program_map[pgn - 1] - 1;
  
  /* Get name */
  if(ctx->opt->dvd_chapters_as_tracks)
    {
    if(ttsrpt->title[title].nr_of_angles > 1)
      new_track->name = bgav_sprintf("Title %02d Angle %d Chapter %02d",
                                     title+1, angle+1, chapter+1);
    else
      new_track->name = bgav_sprintf("Title %02d Chapter %02d",
                                     title+1, chapter+1);

    if(chapter < ttsrpt->title[title].nr_of_ptts-1)
      {
      /* Next chapter is in the same program chain */
      if(vts_ptt_srpt->title[ttn - 1].ptt[chapter].pgcn ==
         vts_ptt_srpt->title[ttn - 1].ptt[chapter+1].pgcn)
        {
        end_cell =
          pgc->program_map[vts_ptt_srpt->title[ttn - 1].ptt[chapter+1].pgn-1]-1;
        }
      else
        end_cell = pgc->nr_of_cells;
      }
    else
      end_cell = pgc->nr_of_cells;
    }
  else
    {
    if(ttsrpt->title[title].nr_of_angles > 1)
      new_track->name = bgav_sprintf("Title %02d Angle %d",
                                     title+1, angle+1);
    else
      new_track->name = bgav_sprintf("Title %02d", title+1);
    end_cell = pgc->nr_of_cells;
    }

  /* Get duration */
  new_track->duration = 0;
  for(i = start_cell; i < end_cell; i++)
    new_track->duration += convert_time(&(pgc->cell_playback[i].playback_time));
  
  //  fprintf(stderr, "Name: %s, start_cell: %d, end_cell: %d\n", new_track->name,
  //          start_cell, end_cell);
  
  /* Setup streams */
  s = bgav_track_add_video_stream(new_track);
  s->fourcc = BGAV_MK_FOURCC('m', 'p', 'g', 'v');
  s->stream_id = 0xE0;
  s->timescale = 90000;
  
  /* Audio streams */
  for(i = 0; i < dvd->vts_ifo->vtsi_mat->nr_of_vts_audio_streams; i++)
    {
    if(!(pgc->audio_control[i] & 0x8000))
      continue;

    s = bgav_track_add_audio_stream(new_track);
    s->timescale = 90000;
    
    audio_attr = &dvd->vts_ifo->vtsi_mat->vts_audio_attr[i];

    switch(audio_attr->audio_format)
      {
      case 0:
        //        printf("ac3 ");
        s->fourcc = BGAV_MK_FOURCC('.', 'a', 'c', '3');
        s->stream_id = 0xbd80 + ac3_id;
        ac3_id++;
        break;
      case 2:
        //        printf("mpeg1 ");
        s->fourcc = BGAV_MK_FOURCC('.', 'm', 'p', '3');
        s->stream_id = 0xc0 + mpa_id;
        mpa_id++;
        break;
      case 3:
        //        printf("mpeg2ext ");
        /* Unsupported */
        s->fourcc = BGAV_MK_FOURCC('m', 'p', 'a', 'e');
        break;
      case 4:
        //        printf("lpcm ");
        s->fourcc = BGAV_MK_FOURCC('l', 'p', 'c', 'm');
        s->stream_id = 0xbda0 + lpcm_id;
        lpcm_id++;
        break;
      case 6:
        //        printf("dts ");
        s->fourcc = BGAV_MK_FOURCC('d', 't', 's', ' ');
        s->stream_id = 0xbd8a + dts_id;
        dts_id++;
        break;
      default:
        //        printf("(please send a bug report) ");
        break;
      }
    
    }

  /* Subtitle streams */
  for(i = 0; i < dvd->vts_ifo->vtsi_mat->nr_of_vts_subp_streams; i++)
    {
    if(!(pgc->subp_control[i] & 0x80000000))
      continue;
    fprintf(stderr, "Got subtitle stream\n");
    }

  /* Duration */
  
  }

static void dump_vmg_ifo(ifo_handle_t * vmg_ifo)
  {
  fprintf(stderr, "VMG IFO:\n");
  fprintf(stderr, "  Title sets: %d\n",
          vmg_ifo->vmgi_mat->vmg_nr_of_title_sets);
  }

static int open_dvd(bgav_input_context_t * ctx, const char * url)
  {
  int i, j, k;
  dvd_t * priv;
  tt_srpt_t *ttsrpt;
  fprintf(stderr, "OPEN DVD\n");

  //  DVDInit();
  
  priv = calloc(1, sizeof(*priv));
  
  ctx->priv = priv;

  /* Try to open dvd */
  priv->dvd_reader = DVDOpen(url);
  if(!priv->dvd_reader)
    {
    fprintf(stderr, "DVDOpen failed\n");
    return 0;
    }

  /* Open Video Manager */

  priv->vmg_ifo = ifoOpen(priv->dvd_reader, 0);
  if(!priv->vmg_ifo)
    {
    fprintf(stderr, "ifoOpen failed\n");
    return 0;
    }
  dump_vmg_ifo(priv->vmg_ifo);
  
  ttsrpt = priv->vmg_ifo->tt_srpt;
  //  fprintf(stderr, "

  /* Create track table */
  ctx->tt = bgav_track_table_create(0);

  for(i = 0; i < ttsrpt->nr_of_srpts; i++)
    {
    for(j = 0; j < ttsrpt->title[i].nr_of_angles; j++)
      {
      if(ctx->opt->dvd_chapters_as_tracks)
        {
        /* Add individual chapters as tracks */
        for(k = 0; k < ttsrpt->title[i].nr_of_ptts; k++)
          {
          setup_track(ctx, i, k, j);
          }
        }
      else
        {
        /* Add entire titles as tracks */
        setup_track(ctx, i, 0, j);
        }
      }
    }
  
  /* Create demuxer */
  
  ctx->demuxer = bgav_demuxer_create(ctx->opt, &bgav_demuxer_mpegps, ctx);
  ctx->demuxer->tt = ctx->tt;

  ctx->sector_size        = 2048;
  ctx->sector_size_raw    = 2048;
  ctx->sector_header_size = 0;
  
  return 1;
  }

static int read_sector_dvd(bgav_input_context_t * ctx, uint8_t * data)
  {
  int ret;
  dvd_t * priv;
  priv = (dvd_t*)(ctx->priv);

  /* Check if we are at the end of a cell */

  if(priv->current_sector > priv->pgc->cell_playback[priv->current_cell].last_sector)
    {
    /* Cell finished */
    fprintf(stderr, "Cell finished, and now?\n");
    return 0;
    }
  
  //  return 0;
  
  //  fprintf(stderr, "DVDReadBlocks %d %p...", priv->current_sector, data);
  ret = DVDReadBlocks(priv->dvd_file, priv->current_sector, 1, data);
  //  fprintf(stderr, "return: %d\n", ret);
  if(ret <= 0)
    return 0;

  priv->current_sector++;

  //  bgav_hexdump(data, 16, 16);
  return 1;
  }

static void    close_dvd(bgav_input_context_t * ctx)
  {
  dvd_t * priv;
  //  fprintf(stderr, "CLOSE DVD\n");
  priv = (dvd_t*)(ctx->priv);
  free(priv);
  return;
  }

static void select_track_dvd(bgav_input_context_t * ctx, int track)
  {
  dvd_t * dvd;
  int ttn, pgn;
  int pgc_id;
  tt_srpt_t *ttsrpt;
  track_priv_t * track_priv;
  vts_ptt_srpt_t *vts_ptt_srpt;
  
  dvd = (dvd_t*)(ctx->priv);

  ttsrpt = dvd->vmg_ifo->tt_srpt;
  track_priv = (track_priv_t*)(ctx->tt->current_track->priv);
  
  ttn = ttsrpt->title[track_priv->title].vts_ttn;

  /* Open VTS */
  open_vts(dvd, ttsrpt->title[track_priv->title].title_set_nr, 1);
  
  vts_ptt_srpt = dvd->vts_ifo->vts_ptt_srpt;
  pgc_id = vts_ptt_srpt->title[ttn - 1].ptt[track_priv->chapter].pgcn;
  pgn    = vts_ptt_srpt->title[ttn - 1].ptt[track_priv->chapter].pgn;

  fprintf(stderr, "Select track: t: %d, c: %d, pgc_id: %d, pgn: %d\n",
          track_priv->title, track_priv->chapter, pgc_id, pgn);
  
  dvd->pgc = dvd->vts_ifo->vts_pgcit->pgci_srp[pgc_id - 1].pgc;

  dvd->current_cell   = dvd->pgc->program_map[pgn - 1] - 1;
  dvd->current_sector = dvd->pgc->cell_playback[dvd->current_cell].first_sector;

  
  }

bgav_input_t bgav_input_dvd =
  {
    name:          "dvd",
    open:          open_dvd,
    read_sector:   read_sector_dvd,
    //    seek_sector:   seek_sector_dvd,
    close:         close_dvd,
    select_track:  select_track_dvd,
  };

static char * get_device_name(CdIo_t * cdio,
                              cdio_drive_read_cap_t  read_cap,
                              cdio_drive_write_cap_t write_cap,
                              const char * device)
  {
  cdio_hwinfo_t driveid;

  if(cdio_get_hwinfo(cdio, &driveid) &&
     (driveid.psz_model[0] != '\0'))
    {
    return bgav_sprintf("%s %s", driveid.psz_vendor, driveid.psz_model);
    }
  
  if(write_cap & CDIO_DRIVE_CAP_WRITE_DVD_R)
    {
    return bgav_sprintf("DVD Writer (%s)", device);
    }
  else if(write_cap & CDIO_DRIVE_CAP_WRITE_CD_R)
    {
    return bgav_sprintf("CD Writer (%s)", device);
    }
  else if(read_cap & CDIO_DRIVE_CAP_READ_DVD_ROM)
    {
    return bgav_sprintf("DVD Drive (%s)", device);
    }
  return bgav_sprintf("CDrom Drive (%s)", device);
  }

int bgav_check_device_dvd(const char * device, char ** name)
  {
  CdIo_t * cdio;
  cdio_drive_read_cap_t  read_cap;
  cdio_drive_write_cap_t write_cap;
  cdio_drive_misc_cap_t  misc_cap;

  cdio = cdio_open (device, DRIVER_DEVICE);
  if(!cdio)
    return 0;
  
  cdio_get_drive_cap(cdio, &read_cap, &write_cap, &misc_cap);

  if(!(read_cap & CDIO_DRIVE_CAP_READ_DVD_ROM))
    {
    cdio_destroy(cdio);
    return 0;
    }
  
  /* Seems the drive is ok */

  if(name)
    *name = get_device_name(cdio, read_cap, write_cap, device);
  cdio_destroy(cdio);
  return 1;
  }

bgav_device_info_t * bgav_find_devices_dvd()
  {
  int i;
  char * device_name;
  char ** devices;
  bgav_device_info_t * ret = (bgav_device_info_t *)0;

  devices = cdio_get_devices(DRIVER_DEVICE);

  if(!devices)
    return 0;
    
  i = 0;
  while(devices[i])
    {
    //    fprintf(stderr, "Checking %s\n", devices[i]);
    device_name = (char*)0;
    if(bgav_check_device_dvd(devices[i], &device_name))
      {
      ret = bgav_device_info_append(ret,
                                  devices[i],
                                  device_name);
      if(device_name)
        free(device_name);
      }
    i++;
    }
  cdio_free_device_list(devices);
  return ret;

  }

bgav_input_context_t * bgav_input_open_dvd(const char * device,
                                           bgav_options_t * opt)
  {
  bgav_input_context_t * ret = (bgav_input_context_t *)0;
  ret = calloc(1, sizeof(*ret));
  ret->input = &bgav_input_dvd;
  ret->opt = opt;
  if(!ret->input->open(ret, device))
    {
    fprintf(stderr, "Cannot open DVD Device %s\n", device);
    goto fail;
    }
  return ret;
  fail:
  if(ret)
    free(ret);
  return (bgav_input_context_t *)0;
  }

int bgav_open_dvd(bgav_t * b, const char * device)
  {
  bgav_codecs_init();
  b->input = bgav_input_open_dvd(device, &b->opt);
  if(!b->input)
    return 0;
  if(!bgav_init(b))
    goto fail;
  return 1;
  fail:
  return 0;
  
  }


#else /* No DVD support */

int bgav_check_device_dvd(const char * device, char ** name)
  {
  fprintf(stderr, "DVD not supported (need libcdio and libdvdread)\n");
  return 0;
  }

bgav_device_info_t * bgav_find_devices_dvd()
  {
  fprintf(stderr, "DVD not supported (need libcdio and libdvdread)\n");
  return (bgav_device_info_t*)0;
  
  }

int bgav_open_dvd(bgav_t * b, const char * device)
  {
  fprintf(stderr, "DVD not supported (need libcdio and libdvdread)\n");
  return 0;
  }


#endif
