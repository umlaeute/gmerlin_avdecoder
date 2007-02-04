/*****************************************************************
 
  bgav.c
 
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

#include <avdec_private.h>

#include <stdlib.h>
#include <stdio.h>

#define LOG_DOMAIN "core"

static bgav_input_context_t * create_input(bgav_t * b)
  {
  bgav_input_context_t * ret;

  
  ret = bgav_input_create(&(b->opt));
  
  return ret;
  }

static bgav_demuxer_context_t *
create_demuxer(bgav_t * b, bgav_demuxer_t * demuxer)
  {
  bgav_demuxer_context_t * ret;


  ret = bgav_demuxer_create(&(b->opt), demuxer, b->input);
  return ret;
  }

int bgav_init(bgav_t * ret)
  {
  bgav_demuxer_t * demuxer = (bgav_demuxer_t *)0;
  bgav_redirector_t * redirector = (bgav_redirector_t*)0;
  
  bgav_subtitle_reader_context_t * subreader, * subreaders;
  
  /*
   *  If the input already has it's track table,
   *  we can stop here
   */
  if(ret->input->tt)
    {
    ret->tt = ret->input->tt;
    bgav_track_table_ref(ret->tt);

    ret->demuxer = ret->input->demuxer;

    if(ret->demuxer)
      {
      ret->demuxer->tt = ret->input->tt;
      bgav_track_table_ref(ret->demuxer->tt);
      }
    if(ret->tt->num_tracks > 1)
      return 1;
    
    }
  /*
   *  Autodetect the format and fire up the demuxer
   */
  if(!ret->input->demuxer)
    {
    /* First, we try the redirector, because we never need to
       skip bytes for them. */

    redirector = bgav_redirector_probe(ret->input);
    if(redirector)
      {
      ret->redirector = calloc(1, sizeof(*(ret->redirector)));
      ret->redirector->input = ret->input;
      ret->redirector->opt = &(ret->opt);
      
      if(!redirector->parse(ret->redirector))
        goto fail;
      else
        return 1;
      }

    /* Check for ID3V2 tags here, they can be prepended to
       many different file types */
    
    if(bgav_id3v2_probe(ret->input))
      ret->input->id3v2 = bgav_id3v2_read(ret->input);
    
    demuxer = bgav_demuxer_probe(ret->input);
    
    if(demuxer)
      {
      ret->demuxer = create_demuxer(ret, demuxer);
      if(!bgav_demuxer_start(ret->demuxer, &(ret->redirector)))
        {
        if(ret->demuxer->error_msg)
          ret->error_msg = bgav_strdup(ret->demuxer->error_msg);
        goto fail;
        }
      if(ret->redirector)
        return 1;
      }
    if(!ret->demuxer)
      goto fail;
    }
  else
    {
    ret->demuxer = ret->input->demuxer;
    }
  
  ret->tt = ret->demuxer->tt;
  bgav_track_table_ref(ret->tt);

  bgav_track_table_remove_unsupported(ret->tt);
  
  bgav_track_table_merge_metadata(ret->tt,
                                  &(ret->input->metadata));

  /* Check for subtitle file */
  
  if(ret->opt.seek_subtitles &&
     (ret->opt.seek_subtitles + ret->tt->cur->num_video_streams > 1))
    {
    subreaders = bgav_subtitle_reader_open(ret->input);
    
    subreader = subreaders;
    while(subreader)
      {
      bgav_track_attach_subtitle_reader(ret->tt->cur,
                                        &(ret->opt), subreader);
      subreader = subreader->next;
      }
    }
  
  return 1;
    
  fail:

  if(!demuxer && !redirector)
    {
    bgav_log(&ret->opt, BGAV_LOG_ERROR, LOG_DOMAIN, "Cannot detect stream type");
    }
  
  if(ret->demuxer)
    {
    bgav_demuxer_destroy(ret->demuxer);
    ret->demuxer = (bgav_demuxer_context_t*)0;
    }
  if(ret->redirector)
    {
    bgav_redirector_destroy(ret->redirector);
    ret->redirector = (bgav_redirector_context_t*)0;
    }
  return 0;
  }

int bgav_num_tracks(bgav_t * b)
  {
  return b->tt->num_tracks;
  }

bgav_t * bgav_create()
  {
  bgav_t * ret;
  ret = calloc(1, sizeof(*ret));

  bgav_options_set_defaults(&ret->opt);
  
  return ret;
  }

int bgav_open(bgav_t * ret, const char * location)
  {
  bgav_codecs_init(&ret->opt);
  ret->input = create_input(ret);
  if(!bgav_input_open(ret->input, location))
    {
    ret->error_msg = bgav_strdup(ret->input->error_msg);
    goto fail;
    }
  if(!bgav_init(ret))
    goto fail;

  ret->location = bgav_strdup(location);
  
  return 1;
  fail:

  if(ret->input)
    {
    bgav_input_close(ret->input);
    free(ret->input);
    ret->input = (bgav_input_context_t*)0;
    }
  return 0;
  }



int bgav_open_fd(bgav_t * ret, int fd, int64_t total_size, const char * mimetype)
  {
  bgav_codecs_init(&ret->opt);
  ret->input = bgav_input_open_fd(fd, total_size, mimetype);
  if(!bgav_init(ret))
    return 0;
  return 1;
  }

void bgav_close(bgav_t * b)
  {
  if(b->location)
    free(b->location);
  
  if(b->is_running)
    {
    bgav_track_stop(b->tt->cur);
    b->is_running = 0;
    }

  if(b->demuxer)
    bgav_demuxer_destroy(b->demuxer);
  if(b->redirector)
    bgav_redirector_destroy(b->redirector);

  if(b->input)
    {
    bgav_input_close(b->input);
    free(b->input);
    }
  if(b->tt)
    bgav_track_table_unref(b->tt);
  if(b->error_msg)
    free(b->error_msg);

  bgav_options_free(&b->opt);
  
  free(b);
  }


void bgav_dump(bgav_t * bgav)
  {
  bgav_track_dump(bgav, bgav->tt->cur);
  
  }

gavl_time_t bgav_get_duration(bgav_t * bgav, int track)
  {
  return bgav->tt->tracks[track].duration;
  }

const char * bgav_get_track_name(bgav_t * b, int track)
  {
  return b->tt->tracks[track].name;
  }

void bgav_stop(bgav_t * b)
  {
  }

int bgav_select_track(bgav_t * b, int track)
  {
  int was_running = 0;
  int reset_input = 0;
  int64_t data_start = -1;
  if((track < 0) || (track >= b->tt->num_tracks))
    return 0;
  
  if(b->is_running)
    {
    bgav_track_stop(b->tt->cur);
    b->is_running = 0;
    was_running = 1;
    }
  
  if(b->input->input->select_track)
    {
    /* Input switches track, recreate the demuxer */

    bgav_demuxer_stop(b->demuxer);
    bgav_track_table_select_track(b->tt, track);
    b->input->input->select_track(b->input, track);
    bgav_demuxer_start(b->demuxer, &(b->redirector));
    return 1;
    }

  if(b->demuxer)
    {
    if(b->demuxer->flags & BGAV_DEMUXER_HAS_DATA_START)
      {
      if(b->demuxer->data_start < b->input->position)
        {
        if(b->input->input->seek_byte)
          bgav_input_seek(b->input, b->demuxer->data_start, SEEK_SET);
        else
          {
          data_start = b->demuxer->data_start;
          reset_input = 1;
          //        bgav_log(&b->opt, BGAV_LOG_ERROR, LOG_DOMAIN,
          //                 "Cannot reset track when on a nonseekable source");
          //        return 0;
          }
        }
      else if(b->demuxer->data_start > b->input->position)
        bgav_input_skip(b->input, b->demuxer->data_start - b->input->position);
      }
    // Enable this for inputs, which read sector based but have
    // no track selection

    //if(b->input->input->seek_sector)
    //  bgav_input_seek_sector(b->input, 0);

    if(!reset_input)
      {
      if(b->demuxer->si && was_running)
        {
        b->demuxer->si->current_position = 0;
        if(b->input->input->seek_byte)
          bgav_input_seek(b->input, b->demuxer->si->entries[0].offset,
                          SEEK_SET);
        else
          {
          data_start = b->demuxer->si->entries[0].offset;
          reset_input = 1;
          //        bgav_log(&b->opt, BGAV_LOG_ERROR, LOG_DOMAIN,
          //                 "Cannot reset track when on a nonseekable source");
          //        return 0;
          }
        }
      }
    
    if(b->demuxer->demuxer->select_track)
      {
      /* Demuxer switches track */
      bgav_track_table_select_track(b->tt, track);
      
      if(!b->demuxer->demuxer->select_track(b->demuxer, track))
        reset_input = 1;
      }
    
    if(reset_input)
      {
      /* Reset input. This will be done by closing and reopening the input */
      bgav_log(&b->opt, BGAV_LOG_INFO, LOG_DOMAIN, "Reopening input due to track reset");

      if(data_start < 0)
        {
        if(b->tt)
          {
          bgav_track_table_unref(b->tt);
          b->tt = (bgav_track_table_t*)0;
          }
        if(b->demuxer && b->demuxer->tt)
          {
          bgav_track_table_unref(b->demuxer->tt);
          b->demuxer->tt = (bgav_track_table_t*)0;
          }
        bgav_demuxer_stop(b->demuxer);
        }
      
      if(!bgav_input_reopen(b->input))
        return 0;


      if(data_start >= 0)
        {
        bgav_input_skip(b->input, data_start);
        }
      else
        {
        bgav_demuxer_start(b->demuxer, &(b->redirector));
        
        if(b->demuxer->tt)
          {
          b->tt = b->demuxer->tt;
          bgav_track_table_ref(b->tt);
          }
        }
      }
    }
  return 1;
  }

int bgav_start(bgav_t * b)
  {
  b->is_running = 1;
  /* Create buffers */
  bgav_input_buffer(b->input);
  if(!bgav_track_start(b->tt->cur, b->demuxer))
    {
    if(b->demuxer->error_msg)
       b->error_msg = bgav_strdup(b->demuxer->error_msg);
    return 0;
    }
  return 1;
  }

int bgav_can_seek(bgav_t * b)
  {
  return !!(b->demuxer->flags & BGAV_DEMUXER_CAN_SEEK);
  }

const bgav_metadata_t * bgav_get_metadata(bgav_t*b, int track)
  {
  return &(b->tt->tracks[track].metadata);
  }

const char * bgav_get_description(bgav_t * b)
  {
  return b->demuxer->stream_description;
  }

const char * bgav_get_error(bgav_t * b)
  {
  return b->error_msg;
  }

bgav_options_t * bgav_get_options(bgav_t * b)
  {
  return &(b->opt);
  }

const char * bgav_get_disc_name(bgav_t * bgav)
  {
  if(bgav->input)
    return bgav->input->disc_name;
  return
    (const char*)0;
  }
