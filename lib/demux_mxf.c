/*****************************************************************
 * gmerlin-avdecoder - a general purpose multimedia decoding library
 *
 * Copyright (c) 2001 - 2008 Members of the Gmerlin project
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

#include <string.h>
#include <stdlib.h>

#include <avdec_private.h>
#include <mxf.h>

#define LOG_DOMAIN "mxf"

#define FRAME_WRAPPED      0
#define CLIP_WRAPPED_CBR   1
#define CLIP_WRAPPED_PARSE 2 /* Unsupported for now */

// #define DUMP_MXF

static void build_edl_mxf(bgav_demuxer_context_t * ctx);

/* TODO: Find a better way */
static int probe_mxf(bgav_input_context_t * input)
  {
  char * pos;
  if(input->filename)
    {
    pos = strrchr(input->filename, '.');
    if(!pos)
      return 0;
    if(!strcasecmp(pos, ".mxf"))
      return 1;
    }
  return 0;
  }

typedef struct
  {
  int64_t pts_counter;
  int eof;
  /* Constant frame size for clip-wrapped streams, which are TRUE CBR */
  int frame_size;
  int wrap_mode;
  
  int64_t start;
  int64_t length;
  int64_t pos;
  
  int (*next_packet)(bgav_demuxer_context_t * ctx, bgav_stream_t * s);

  int track_id;
  
  } stream_priv_t;

typedef struct
  {
  mxf_file_t mxf;
  } mxf_t;

static void set_pts(bgav_stream_t * s, stream_priv_t * sp,
                    bgav_packet_t * p)
  {
  if(s->type == BGAV_STREAM_VIDEO)
    {
    if(s->data.video.frametime_mode != BGAV_FRAMETIME_CODEC)
      {
      p->pts = sp->pts_counter;
      p->duration = s->data.video.format.frame_duration;
      sp->pts_counter += p->duration;
      }
    }
  else if(s->type == BGAV_STREAM_AUDIO)
    {
    p->pts = sp->pts_counter;
    if(s->data.audio.block_align)
      p->duration = p->data_size / s->data.audio.block_align;
    sp->pts_counter += p->duration;
    p->keyframe = 1;
    }
  }

static int next_packet_clip_wrapped_const(bgav_demuxer_context_t * ctx, bgav_stream_t * s)
  {
  int bytes_to_read;
  mxf_klv_t klv;
  bgav_stream_t * tmp_stream = (bgav_stream_t *)0;
  stream_priv_t * sp;
  bgav_packet_t * p;
  mxf_t * priv;
  priv = (mxf_t *)ctx->priv;
  sp = (stream_priv_t*)(s->priv);

  /* Need the KLV packet for this stream */
  if(!sp->start)
    {
    bgav_input_seek(ctx->input, ctx->data_start, SEEK_SET);
    while(1)
      {
      if(!bgav_mxf_klv_read(ctx->input, &klv))
        return 0;

      tmp_stream = bgav_mxf_find_stream(&priv->mxf, ctx, klv.key);
      if(tmp_stream == s)
        {
        sp->start  = ctx->input->position;
        sp->pos    = ctx->input->position;
        sp->length = klv.length;
        break;
        }
      else
        bgav_input_skip(ctx->input, klv.length);
      }
    }
  /* No packets */
  if(!sp->start)
    return 0;
  /* Out of data */
  if(sp->pos >= sp->start + sp->length)
    return 0;

  if(ctx->input->position != sp->pos)
    bgav_input_seek(ctx->input, sp->pos, SEEK_SET);
  
  bytes_to_read = sp->frame_size;
  if(sp->pos + bytes_to_read >= sp->start + sp->length)
    bytes_to_read = sp->start + sp->length - sp->pos;

  p = bgav_stream_get_packet_write(s);
  p->position = ctx->input->position;
  bgav_packet_alloc(p, bytes_to_read);
  p->data_size = bgav_input_read_data(ctx->input, p->data, bytes_to_read);

  sp->pos += bytes_to_read;

  if(p->data_size < bytes_to_read)
    return 0;
  
  set_pts(s, sp, p);
  bgav_packet_done_write(p);
  return 1;
  }

static int process_packet_frame_wrapped(bgav_demuxer_context_t * ctx)
  {
  bgav_stream_t * s;
  bgav_packet_t * p;
  mxf_klv_t klv;
  int64_t position;
  stream_priv_t * sp;
  mxf_t * priv;

  priv = (mxf_t*)ctx->priv;
  position = ctx->input->position;

  if(position > ((partition_t*)(ctx->tt->cur->priv))->end_pos)
    return 0;
  
  if(!bgav_mxf_klv_read(ctx->input, &klv))
    return 0;
  s = bgav_mxf_find_stream(&priv->mxf, ctx, klv.key);
  if(!s)
    {
    bgav_input_skip(ctx->input, klv.length);
    return 1;
    }
  
  sp = (stream_priv_t*)(s->priv);

  p = bgav_stream_get_packet_write(s);
  p->position = position;
  /* check for 8 channels AES3 element */
  if(klv.key[12] == 0x06 && klv.key[13] == 0x01 && klv.key[14] == 0x10)
    {
    int i, j;
    int32_t sample;
    int64_t end_pos = ctx->input->position + klv.length;
    int num_samples;
    uint8_t * ptr;

    //    fprintf(stderr, "Got AES3 packet\n");

    /* Skip  SMPTE 331M header */
    bgav_input_skip(ctx->input, 4);

    num_samples = (end_pos - ctx->input->position) / 32; /* 8 channels*4 bytes/channel */
    
    bgav_packet_alloc(p, num_samples * s->data.audio.block_align);
    ptr = p->data;
    
    for(i = 0; i < num_samples; i++)
      {
      for(j = 0; j < s->data.audio.format.num_channels; j++)
        {
        if(!bgav_input_read_32_le(ctx->input, (uint32_t*)(&sample)))
          return 0;
        if(s->data.audio.bits_per_sample == 24)
          {
          sample = (sample >> 4) & 0xffffff;
          BGAV_24LE_2_PTR(sample, ptr);
          ptr += 3;
          p->data_size += 3;
          }
        else if(s->data.audio.bits_per_sample == 16)
          {
          sample = (sample >> 12) & 0xffff;
          BGAV_16LE_2_PTR(sample, ptr);
          ptr += 2;
          p->data_size += 2;
          }
        }
      bgav_input_skip(ctx->input, 32 - s->data.audio.format.num_channels * 4);
      }
    p->pts = sp->pts_counter;
    p->duration = num_samples;
    sp->pts_counter += num_samples;
    }
  else
    {
    bgav_packet_alloc(p, klv.length);
    if((p->data_size = bgav_input_read_data(ctx->input, p->data, klv.length)) < klv.length)
      return 0;

    set_pts(s, sp, p);
    }
  
  if(p)
    {
    fprintf(stderr, "Got packet\n");
    bgav_packet_done_write(p);
    }
  return 1;
  }

static int next_packet_frame_wrapped(bgav_demuxer_context_t * ctx, bgav_stream_t * dummy)
  {
  if(ctx->next_packet_pos)
    {
    int ret = 0;
    while(1)
      {
      if(!process_packet_frame_wrapped(ctx))
        return ret;
      else
        ret = 1;
      if(ctx->input->position >= ctx->next_packet_pos)
        return ret;
      }
    }
  else
    {
    return process_packet_frame_wrapped(ctx);
    }
  return 0;
  }


static void init_stream_common(bgav_demuxer_context_t * ctx, bgav_stream_t * s,
                               mxf_track_t * st, mxf_descriptor_t * sd,
                               uint32_t fourcc)
  {
  stream_priv_t * sp;
  mxf_t * priv;
  /* Common initialization */
  priv = (mxf_t *)ctx->priv;
  sp = calloc(1, sizeof(*priv));
  s->priv = sp;
  s->fourcc = fourcc;

  sp->track_id = st->track_id;
  
  /* Detect wrap mode */

  if(priv->mxf.index_segments &&
     priv->mxf.index_segments[0]->edit_unit_byte_count)
    sp->frame_size = priv->mxf.index_segments[0]->edit_unit_byte_count;

  /* Hack: This makes P2 audio files clip wrapped */
  if(!sd->clip_wrapped &&
     (((mxf_preface_t*)(priv->mxf.header.preface))->operational_pattern == MXF_OP_ATOM) &&
     sp->frame_size &&
     (sp->frame_size < st->max_packet_size) &&
     (st->num_packets == 1))
    sd->clip_wrapped = 1;

  if(sd->clip_wrapped)
    {
    if(sp->frame_size)
      sp->next_packet = next_packet_clip_wrapped_const;
    else
      bgav_log(ctx->opt, BGAV_LOG_ERROR, LOG_DOMAIN,
               "Clip wrapped tracks with nonconstant framesize not supported");
    }
  else
    sp->next_packet = next_packet_frame_wrapped;
    
  }

static const uint32_t pcm_fourccs[] =
  {
    BGAV_MK_FOURCC('s', 'o', 'w', 't'),
    BGAV_MK_FOURCC('t', 'w', 'o', 's'),
    BGAV_MK_FOURCC('a', 'l', 'a', 'w'),
  };

static int is_pcm(uint32_t fourcc)
  {
  int i;
  for(i = 0; i < sizeof(pcm_fourccs)/sizeof(pcm_fourccs[0]); i++)
    {
    if(fourcc == pcm_fourccs[i])
      return 1;
    }
  return 0;
  }

static void init_audio_stream(bgav_demuxer_context_t * ctx, bgav_stream_t * s,
                              mxf_track_t * st, mxf_descriptor_t * sd,
                              uint32_t fourcc)
  {
  stream_priv_t * priv;
  init_stream_common(ctx, s, st, sd, fourcc);
  priv = (stream_priv_t *)s->priv;
  if(sd->sample_rate_num % sd->sample_rate_den)
    bgav_log(ctx->opt, BGAV_LOG_WARNING, LOG_DOMAIN, "Rounding fractional audio samplerate");
  s->data.audio.format.samplerate = sd->sample_rate_num / sd->sample_rate_den;
  s->data.audio.format.num_channels = sd->channels;
  s->data.audio.bits_per_sample = sd->bits_per_sample;

  if(is_pcm(fourcc))
    {
    s->data.audio.block_align = sd->channels * ((sd->bits_per_sample+7)/8);
    /* Make a more sensible frame size for clip wrapped PCM streams */
    if(priv->frame_size == s->data.audio.block_align)
      priv->frame_size = 1024 * s->data.audio.block_align; /* 1024 samples */
    s->index_mode = INDEX_MODE_SIMPLE;
    }
  }

static void init_video_stream(bgav_demuxer_context_t * ctx, bgav_stream_t * s,
                              mxf_track_t * st, mxf_descriptor_t * sd,
                              uint32_t fourcc)
  {
  stream_priv_t * priv;
  init_stream_common(ctx, s, st, sd, fourcc);
  priv = (stream_priv_t *)s->priv;
  
  if(s->fourcc == BGAV_MK_FOURCC('m','p','g','v'))
    {
    s->data.video.frametime_mode = BGAV_FRAMETIME_CODEC;
    s->index_mode = INDEX_MODE_MPEG;
    }
  else if(s->fourcc == BGAV_MK_FOURCC('m','p','4','v'))
    {
    s->data.video.frametime_mode = BGAV_FRAMETIME_CONSTANT;
    s->index_mode = INDEX_MODE_MPEG;
    s->not_aligned = 1; 
    }
  else
    s->index_mode = INDEX_MODE_SIMPLE;
  
  s->data.video.format.timescale      = sd->sample_rate_num;
  s->data.video.format.frame_duration = sd->sample_rate_den;
  s->data.video.format.image_width    = sd->width;
  s->data.video.format.image_height   = sd->height;
  s->data.video.format.frame_width    = sd->width;
  s->data.video.format.frame_height   = sd->height;

  if(sd->ext_size)
    {
    s->ext_size = sd->ext_size;
    s->ext_data = sd->ext_data;
    }
  
  /* Todo: Aspect ratio */
  s->data.video.format.pixel_width    = 1;
  s->data.video.format.pixel_height   = 1;
  }

static mxf_descriptor_t * get_source_descriptor(mxf_file_t * file, mxf_package_t * p, mxf_track_t * st)
  {
  int i;
  mxf_descriptor_t * desc;
  mxf_descriptor_t * sub_desc;
  if(!p->descriptor)
    {
    if((((mxf_preface_t *)(file->header.preface))->operational_pattern == MXF_OP_ATOM) &&
       (file->header.num_descriptors == 1))
      {
      for(i = 0; i < file->header.num_metadata; i++)
        {
        if(file->header.metadata[i]->type == MXF_TYPE_DESCRIPTOR)
          return (mxf_descriptor_t *)(file->header.metadata[i]);
        }
      }
    return (mxf_descriptor_t *)0;
    }
  if(p->descriptor->type == MXF_TYPE_DESCRIPTOR)
    return (mxf_descriptor_t *)p->descriptor;
  else if(p->descriptor->type == MXF_TYPE_MULTIPLE_DESCRIPTOR)
    {
    desc = (mxf_descriptor_t*)(p->descriptor);
    for(i = 0; i < desc->num_subdescriptor_refs; i++)
      {
      sub_desc = (mxf_descriptor_t *)(desc->subdescriptors[i]);
      if(sub_desc && (sub_desc->linked_track_id == st->track_id))
        return sub_desc;
      } 
    }
  return (mxf_descriptor_t*)0;
  }

static void
handle_source_track_simple(bgav_demuxer_context_t * ctx, mxf_package_t * sp, mxf_track_t * t,
                           bgav_track_t * bt)
  {
  mxf_descriptor_t * sd;
  mxf_sequence_t * ss;
  mxf_source_clip_t * sc;
  mxf_t * priv;
  uint32_t fourcc;
  bgav_stream_t * s = (bgav_stream_t*)0;
  
  priv = (mxf_t*)ctx->priv;
  
  ss = (mxf_sequence_t*)(t->sequence);
  
  if(!ss)
    return;
  
  if(ss->is_timecode)
    {
    /* TODO */
    return;
    }
  else if(ss->stream_type == BGAV_STREAM_UNKNOWN)
    {
    return;
    }
  else
    {
    if(!ss->structural_components)
      return;
    
    if(ss->structural_components[0]->type != MXF_TYPE_SOURCE_CLIP)
      return;
    
    sc = (mxf_source_clip_t*)(ss->structural_components[0]);
    
    sd = get_source_descriptor(&priv->mxf, sp, t);
    if(!sd)
      {
      fprintf(stderr, "Got no source descriptor\n");
      return;
      }
    
    if(ss->stream_type == BGAV_STREAM_AUDIO)
      {
      //      fprintf(stderr, "Got audio stream %p\n", sd);
      //      bgav_mxf_descriptor_dump(0, sd);

      fourcc = bgav_mxf_get_audio_fourcc(sd);
      if(!fourcc)
        return;
      s = bgav_track_add_audio_stream(bt, ctx->opt);
      init_audio_stream(ctx, s, t, sd, fourcc);
      }
    else if(ss->stream_type == BGAV_STREAM_VIDEO)
      {
      fourcc = bgav_mxf_get_video_fourcc(sd);
      if(!fourcc)
        return;
      s = bgav_track_add_video_stream(bt, ctx->opt);
      init_video_stream(ctx, s, t, sd, fourcc);
      }

    /* Should not happen */
    if(!s)
      return;

    s->stream_id =
      t->track_number[0] << 24 |
      t->track_number[1] << 16 |
      t->track_number[2] <<  8 |
      t->track_number[3];
#if 0        
    bgav_stream_dump(s);
    if(ms->stream_type == BGAV_STREAM_VIDEO)
      bgav_video_dump(s);
    else if(ms->stream_type == BGAV_STREAM_AUDIO)
      bgav_audio_dump(s);
#endif
    }
  return;
  }

static int get_body_sid(mxf_file_t * f, mxf_package_t * p, uint32_t * ret)
  {
  int i;
  mxf_essence_container_data_t * ec;
  mxf_content_storage_t * cs;
  
  cs = (mxf_content_storage_t*)(((mxf_preface_t*)(f->header.preface))->content_storage);
  
  for(i = 0; i < cs->num_essence_container_data_refs; i++)
    {
    if(!cs->essence_containers[i])
      continue;
    ec = (mxf_essence_container_data_t*)cs->essence_containers[i];

    if(p == (mxf_package_t*)ec->linked_package)
      {
      *ret = ec->body_sid;
      return 1;
      }
    }
  return 0;
  }

static partition_t * get_body_partition(mxf_file_t * f, mxf_package_t * p)
  {
  int i;
  uint32_t body_sid;
  if(!get_body_sid(f, p, &body_sid))
    return (partition_t *)0;
  if(f->header.p.body_sid == body_sid)
    return &f->header;

  for(i = 0; i < f->num_body_partitions; i++)
    {
    if(f->body_partitions[i].p.body_sid == body_sid)
      return &f->body_partitions[i];
    }
  return (partition_t *)0;
  }

/* Simple initialization */
static int init_simple(bgav_demuxer_context_t * ctx)
  {
  mxf_t * priv;
  int i, j, num_tracks = 0;
  mxf_package_t * sp = (mxf_package_t *)0;
  int index = 0;
  
  priv = (mxf_t*)ctx->priv;
  /* We simply open the Source packages */
  
  for(i = 0; i < priv->mxf.header.num_metadata; i++)
    {
    if(priv->mxf.header.metadata[i]->type == MXF_TYPE_SOURCE_PACKAGE)
      num_tracks++;
    }

  ctx->tt = bgav_track_table_create(num_tracks);
  
  for(i = 0; i < priv->mxf.header.num_metadata; i++)
    {
    if(priv->mxf.header.metadata[i]->type == MXF_TYPE_SOURCE_PACKAGE)
      {
      sp = (mxf_package_t*)(priv->mxf.header.metadata[i]);

      ctx->tt->tracks[index].priv = get_body_partition(&priv->mxf, sp);
      
      if(!ctx->tt->tracks[index].priv)
        {
        bgav_log(ctx->opt, BGAV_LOG_ERROR, LOG_DOMAIN,
                 "Couldn't find partition for source package %d", index);
        return 0;
        }
      /* Loop over tracks */
      for(j = 0; j < sp->num_track_refs; j++)
        {
        handle_source_track_simple(ctx, sp, (mxf_track_t*)sp->tracks[j], &ctx->tt->tracks[index]);
        }
      index++;
      }
    }
  
  return 1;
  }

static int open_mxf(bgav_demuxer_context_t * ctx)
  {
  int i;
  mxf_t * priv;
  
  priv = calloc(1, sizeof(*priv));
  ctx->priv = priv;

  if(!bgav_mxf_file_read(ctx->input, &priv->mxf))
    {
    bgav_log(ctx->opt, BGAV_LOG_ERROR, LOG_DOMAIN,
             "Parsing MXF file failed, please report");
    return 0;
    }
#ifdef DUMP_MXF
  bgav_mxf_file_dump(&priv->mxf);
#endif
  
  if(priv->mxf.header.max_source_sequence_components == 1)
    {
    if(!init_simple(ctx))
      return 0;
    }
  else
    {
    bgav_log(ctx->opt, BGAV_LOG_ERROR, LOG_DOMAIN,
             "Unsupported MXF type, please report");
    return 0;
    }

  if(priv->mxf.header.max_material_sequence_components >= 1)
    build_edl_mxf(ctx);    
  
  ctx->data_start = priv->mxf.data_start;
  ctx->flags |= BGAV_DEMUXER_HAS_DATA_START;

  /* Decide index mode */
  ctx->index_mode = INDEX_MODE_MIXED;
  for(i = 0; i < ctx->tt->cur->num_audio_streams; i++)
    {
    if(ctx->tt->cur->audio_streams[i].index_mode == INDEX_MODE_NONE)
      {
      ctx->index_mode = INDEX_MODE_NONE;
      break;
      }
    }
  if(ctx->index_mode != INDEX_MODE_NONE)
    {
    for(i = 0; i < ctx->tt->cur->num_video_streams; i++)
      {
      if(ctx->tt->cur->video_streams[i].index_mode == INDEX_MODE_NONE)
        ctx->index_mode = INDEX_MODE_NONE;
      }
    }
  if(ctx->index_mode != INDEX_MODE_NONE)
    {
    for(i = 0; i < ctx->tt->cur->num_subtitle_streams; i++)
      {
      if(ctx->tt->cur->subtitle_streams[i].index_mode == INDEX_MODE_NONE)
        ctx->index_mode = INDEX_MODE_NONE;
      }
    }
  
  return 1;
  }

static bgav_stream_t * next_stream(bgav_stream_t * s, int num)
  {
  stream_priv_t * sp;
  int i;
  for(i = 0; i < num; i++)
    {
    if(s[i].action != BGAV_STREAM_MUTE)
      {
      sp = (stream_priv_t *)s[i].priv;
      if(!sp->eof)
        return s + i;
      }
    }
  return (bgav_stream_t*)0;
  }

static int next_packet_mxf(bgav_demuxer_context_t * ctx)
  {
  bgav_stream_t * s;
  stream_priv_t * sp;
  
  s = ctx->request_stream;
  if(s)
    sp = (stream_priv_t*)s->priv;
  else
    sp = (stream_priv_t*)0;
  
  while(!s || !sp->next_packet(ctx, s))
    {
    if(sp) sp->eof = 1;
    s = next_stream(ctx->tt->cur->audio_streams,ctx->tt->cur->num_audio_streams);
    if(!s)
      s = next_stream(ctx->tt->cur->video_streams,ctx->tt->cur->num_video_streams);
    if(!s)
      s = next_stream(ctx->tt->cur->subtitle_streams,ctx->tt->cur->num_subtitle_streams);
    if(!s)
      return 0;
    sp = (stream_priv_t*)s->priv;
    }
  return 1;
  }

static void seek_mxf(bgav_demuxer_context_t * ctx, int64_t time,
                    int scale)
  {
  
  }

#if 1
static int select_track_mxf(bgav_demuxer_context_t * ctx, int track)
  {
  fprintf(stderr, "Select track: %d start pos: %ld\n", track,
          ((partition_t*)(ctx->tt->cur->priv))->start_pos);
  bgav_input_seek(ctx->input, ((partition_t*)(ctx->tt->cur->priv))->start_pos, SEEK_SET);
  return 1;
  }
#endif

static void close_mxf(bgav_demuxer_context_t * ctx)
  {
  int i, j;
  mxf_t * priv;
  stream_priv_t * sp;
  priv = (mxf_t*)ctx->priv;
  bgav_mxf_file_free(&priv->mxf);

  if(ctx->tt)
    {
    for(i = 0; i < ctx->tt->num_tracks; i++)
      {
      for(j = 0; j < ctx->tt->tracks[i].num_audio_streams; j++)
        {
        sp = ctx->tt->tracks[i].audio_streams[j].priv;
        if(sp)
          free(sp);
        }
      for(j = 0; j < ctx->tt->tracks[i].num_video_streams; j++)
        {
        sp = ctx->tt->tracks[i].video_streams[j].priv;
        if(sp)
          free(sp);
        }
      /* Not supported yet but well.. */
      for(j = 0; j < ctx->tt->tracks[i].num_subtitle_streams; j++)
        {
        sp = ctx->tt->tracks[i].subtitle_streams[j].priv;
        if(sp)
          free(sp);
        }
      }
    }
  
  free(priv);
  }

static void resync_mxf(bgav_demuxer_context_t * ctx, bgav_stream_t * s)
  {
  
  }

const bgav_demuxer_t bgav_demuxer_mxf =
  {
    .probe        = probe_mxf,
    .open         = open_mxf,
    .select_track = select_track_mxf,
    .next_packet  = next_packet_mxf,
    .resync       = resync_mxf,
    .seek         = seek_mxf,
    .close        = close_mxf
  };

static int find_source_stream(bgav_stream_t * streams, int num, int track_id)
  {
  int i;
  stream_priv_t * p;
  
  for(i = 0; i < num; i++)
    {
    p = (stream_priv_t *)streams->priv;
    if(p->track_id == track_id)
      return i;
    }
  return -1;
  }

static int get_source_stream(bgav_track_table_t * tt,
                             mxf_file_t * f,
                             int * track_index, int * stream_index,
                             bgav_stream_type_t type,
                             mxf_package_t * sp, int track_id)
  {
  int i;
  mxf_content_storage_t * cs;
  int found;

  cs = (mxf_content_storage_t*)(((mxf_preface_t*)(f->header.preface))->content_storage);
  found = 0;
  *track_index = 0;
  
  for(i = 0; i < cs->num_package_refs; i++)
    {
    if(cs->packages[i]->type == MXF_TYPE_SOURCE_PACKAGE)
      {
      if(cs->packages[i] == (mxf_metadata_t*)sp)
        {
        found = 1;
        break;
        }
      else
        (*track_index)++;
      }
    }

  if(!found)
    return 0;

  switch(type)
    {
    case BGAV_STREAM_AUDIO:
      *stream_index = find_source_stream(tt->tracks[*track_index].audio_streams,
                                         tt->tracks[*track_index].num_audio_streams,
                                         track_id);
      break;
    case BGAV_STREAM_VIDEO:
      *stream_index = find_source_stream(tt->tracks[*track_index].video_streams,
                                         tt->tracks[*track_index].num_video_streams,
                                         track_id);
      break;
    case BGAV_STREAM_SUBTITLE_TEXT:
    case BGAV_STREAM_SUBTITLE_OVERLAY:
      *stream_index = find_source_stream(tt->tracks[*track_index].subtitle_streams,
                                         tt->tracks[*track_index].num_subtitle_streams,
                                         track_id);
      break;
    case BGAV_STREAM_UNKNOWN:
      break;
    }
  if(*stream_index < 0)
    return 0;
  return 1;
  }

static void handle_material_track(bgav_demuxer_context_t * ctx, mxf_package_t * p,
                                  mxf_track_t * mt, bgav_edl_track_t * et)
  {
  int i;
  mxf_sequence_t * ss;
  mxf_source_clip_t * sc;
  mxf_t * priv;
  bgav_edl_stream_t * es = (bgav_edl_stream_t*)0;
  int track_index, stream_index = 0;
  bgav_edl_segment_t * seg;
  int64_t duration = 0;
  priv = (mxf_t*)ctx->priv;
  
  ss = (mxf_sequence_t*)(mt->sequence);
  
  if(!ss)
    return;
  
  if(ss->is_timecode)
    {
    /* TODO */
    return;
    }
  else if(ss->stream_type == BGAV_STREAM_UNKNOWN)
    {
    return;
    }
  else
    {
    if(!ss->structural_components ||
       (ss->structural_components[0]->type != MXF_TYPE_SOURCE_CLIP))
      return;

    es = (bgav_edl_stream_t*)0;
    
    switch(ss->stream_type)
      {
      case BGAV_STREAM_AUDIO:
        es = bgav_edl_add_audio_stream(et);
        break;
      case BGAV_STREAM_VIDEO:
        es = bgav_edl_add_video_stream(et);
        break;
      case BGAV_STREAM_SUBTITLE_TEXT:
      case BGAV_STREAM_SUBTITLE_OVERLAY:
      case BGAV_STREAM_UNKNOWN:
        break;
      }
    if(!es)
      return;
    
    es->timescale = mt->edit_rate_num;
    
    for(i = 0; i < ss->num_structural_component_refs; i++)
      {
      sc = (mxf_source_clip_t*)(ss->structural_components[i]);
      
      /*  */
      
      if(get_source_stream(ctx->tt,
                            &priv->mxf,
                            &track_index, &stream_index,
                            ss->stream_type,
                            (mxf_package_t*)sc->source_package,
                            sc->source_track_id))
        {
        seg = bgav_edl_add_segment(es);
        seg->track        = track_index;
        seg->stream       = stream_index;
        seg->timescale    = mt->edit_rate_num;
        seg->src_time     = sc->start_position * mt->edit_rate_den;
        seg->dst_time     = duration;
        seg->dst_duration = sc->duration * mt->edit_rate_den;
        seg->speed_num    = 1;
        seg->speed_den    = 1;
        }
      
      duration += sc->duration * mt->edit_rate_den;
      }
    }
  }

static void build_edl_mxf(bgav_demuxer_context_t * ctx)
  {
  mxf_t * priv;
  int i, j;
  mxf_package_t * sp = (mxf_package_t *)0;
  bgav_edl_track_t * t;
  priv = (mxf_t*)ctx->priv;

  if(!ctx->input->filename)
    return;
    
  ctx->edl = bgav_edl_create();
  ctx->edl->url = bgav_strdup(ctx->input->filename);
  
  
  /* We simply open the Material packages */
  
  for(i = 0; i < priv->mxf.header.num_metadata; i++)
    {
    if(priv->mxf.header.metadata[i]->type == MXF_TYPE_MATERIAL_PACKAGE)
      {
      t = bgav_edl_add_track(ctx->edl);
      sp = (mxf_package_t*)(priv->mxf.header.metadata[i]);
      
      /* Loop over tracks */
      for(j = 0; j < sp->num_track_refs; j++)
        {
        handle_material_track(ctx, sp, (mxf_track_t*)sp->tracks[j], t);
        }
      }
    }
  
  }