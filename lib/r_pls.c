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

/* Shoutcast redirector (.pls) */

#include <avdec_private.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int probe_pls(bgav_input_context_t * input)
  {
  char probe_data[10];
  const char * mimetype;

  
  if(gavl_dictionary_get_src(&input->m, GAVL_META_SRC, 0, &mimetype, NULL) && mimetype &&
     (!strcmp(mimetype, "audio/x-scpls") ||
      !strcmp(mimetype, "audio/scpls")))
    return 1;
  
  if(bgav_input_get_data(input, (uint8_t*)probe_data, 10) < 10)
    return 0;
  
  if(!strncasecmp(probe_data, "[playlist]", 10))
    return 1;
  return 0;
  }

/*
 *  This routine parses a .pls file.
 *  We completely ignore all fields except File= and Title=
 */


static bgav_track_table_t * parse_pls(bgav_input_context_t * input)
  {
  char * buffer = NULL;
  uint32_t buffer_alloc = 0;
  char * pos;
  bgav_track_table_t * tt = 0;
  bgav_track_t * t;
  int have_file = 0;
  int have_title = 0;
  
  /* Get the first nonempty line */
  while(1)
    {
    if(!bgav_input_read_line(input, &buffer, &buffer_alloc, 0, NULL))
      goto fail;
    pos = buffer;
    while(isspace(*pos))
      pos++;
    if(*pos != '\0')
      break;
    }
  
  if(strncasecmp(buffer, "[playlist]", 10))
    goto fail;
  
  /* Get number of entries */

  t = NULL;


  tt = bgav_track_table_create(0);
  
  while(1)
    {
    if(!bgav_input_read_line(input, &buffer, &buffer_alloc, 0, NULL))
      break;

    if(!strncasecmp(buffer, "Title", 5))
      {
      if(!t)
        t = bgav_track_table_append_track(tt);
      
      pos = strchr(buffer, '=');
      if(pos)
        {
        pos++;
        gavl_dictionary_set_string(t->metadata, GAVL_META_LABEL, pos);
        have_title = 1;
        }
      }
    else if(!strncasecmp(buffer, "File", 4))
      {
      if(have_file)
        t = NULL;
      
      if(!t)
        t = bgav_track_table_append_track(tt);
      
      pos = strchr(buffer, '=');
      if(pos)
        {
        pos++;
        gavl_dictionary_set_string(t->metadata, GAVL_META_REFURL, pos);
        }
      have_file = 1;
      }
    if(have_title && have_file)
      t = NULL;

    if(!t)
      {
      have_title = 0;
      have_file = 0;
      }
    
    }
  fail:
  if(buffer)
    free(buffer);
  
  return tt;
  }

const bgav_redirector_t bgav_redirector_pls = 
  {
    .name =  "pls (Shoutcast)",
    .probe = probe_pls,
    .parse = parse_pls
  };
