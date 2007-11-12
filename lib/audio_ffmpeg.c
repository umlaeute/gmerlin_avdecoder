/*****************************************************************
 
  audio_ffmpeg.c
 
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
#include <bswap.h>

#include <stdlib.h>
#include <string.h>
#include <ffmpeg/avcodec.h>
#include <stdio.h>

#include <config.h>
#include <codecs.h>

#define LOG_DOMAIN "audio_ffmpeg"

//#define DUMP_DECODE
//#define DUMP_EXTRADATA

/* Different decoding functions */

#if LIBAVCODEC_BUILD >= 3349760
#define DECODE_FUNC avcodec_decode_audio2
#else
#define DECODE_FUNC avcodec_decode_audio
#endif

/* Map of ffmpeg codecs to fourccs (from ffmpeg's avienc.c) */

typedef struct
  {
  const char * decoder_name;
  const char * format_name;
  enum CodecID ffmpeg_id;
  uint32_t * fourccs;
  int codec_tag;
  } codec_info_t;

static codec_info_t codec_infos[] =
  {
    /*     CODEC_ID_PCM_S16LE= 0x10000, */
    /*     CODEC_ID_PCM_S16BE, */
    /*     CODEC_ID_PCM_U16LE, */
    /*     CODEC_ID_PCM_U16BE, */
    /*     CODEC_ID_PCM_S8, */
    /*     CODEC_ID_PCM_U8, */
    /*     CODEC_ID_PCM_MULAW, */
    /*     CODEC_ID_PCM_ALAW, */
    /*     CODEC_ID_PCM_S32LE, */
    /*     CODEC_ID_PCM_S32BE, */
    /*     CODEC_ID_PCM_U32LE, */
    /*     CODEC_ID_PCM_U32BE, */
    /*     CODEC_ID_PCM_S24LE, */
    /*     CODEC_ID_PCM_S24BE, */
    /*     CODEC_ID_PCM_U24LE, */
    /*     CODEC_ID_PCM_U24BE, */
    /*     CODEC_ID_PCM_S24DAUD, */
    { "FFmpeg D-Cinema decoder", "D-Cinema", CODEC_ID_PCM_S24DAUD,
      (uint32_t[]){ BGAV_MK_FOURCC('d','a','u','d'),
               0x00 },
      -1 },
    /*     CODEC_ID_PCM_ZORK, */

    /*     CODEC_ID_ADPCM_IMA_QT= 0x11000, */
    { "FFmpeg ima4 decoder", "ima4", CODEC_ID_ADPCM_IMA_QT,
      (uint32_t[]){ BGAV_MK_FOURCC('i', 'm', 'a', '4'), 0x00 },
      -1 },
    /*     CODEC_ID_ADPCM_IMA_WAV, */

    { "FFmpeg WAV ADPCM decoder", "WAV IMA ADPCM", CODEC_ID_ADPCM_IMA_WAV,
      (uint32_t[]){ BGAV_WAVID_2_FOURCC(0x11),
                    BGAV_MK_FOURCC('m', 's', 0x00, 0x11), 0x00 },
      -1 },
    /*     CODEC_ID_ADPCM_IMA_DK3, */
    { "FFmpeg IMA DK3 decoder", "IMA DK3", CODEC_ID_ADPCM_IMA_DK3,
      (uint32_t[]){ BGAV_WAVID_2_FOURCC(0x62), 0x00 },
      -1 },  /* rogue format number */
    /*     CODEC_ID_ADPCM_IMA_DK4, */
    { "FFmpeg IMA DK4 decoder", "IMA DK4", CODEC_ID_ADPCM_IMA_DK4,
      (uint32_t[]){ BGAV_WAVID_2_FOURCC(0x61), 0x00 },
      -1 },  /* rogue format number */
    /*     CODEC_ID_ADPCM_IMA_WS, */
    { "FFmpeg Westwood ADPCM decoder", "Westwood ADPCM", CODEC_ID_ADPCM_IMA_WS,
      (uint32_t[]){ BGAV_MK_FOURCC('w','s','p','c'), 0x00 },
      -1 },
    /*     CODEC_ID_ADPCM_IMA_SMJPEG, */
    { "FFmpeg SMJPEG audio decoder", "SMJPEG audio", CODEC_ID_ADPCM_IMA_SMJPEG,
      (uint32_t[]){ BGAV_MK_FOURCC('S','M','J','A'), 0x00 },
      -1 },
    /*     CODEC_ID_ADPCM_MS, */
    { "FFmpeg MS ADPCM decoder", "MS ADPCM", CODEC_ID_ADPCM_MS,
      (uint32_t[]){ BGAV_WAVID_2_FOURCC(0x02),
                    BGAV_MK_FOURCC('m', 's', 0x00, 0x02), 0x00 },
      -1 },
    /*     CODEC_ID_ADPCM_4XM, */
    { "FFmpeg 4xm audio decoder", "4XM ADPCM", CODEC_ID_ADPCM_4XM,
      (uint32_t[]){ BGAV_MK_FOURCC('4', 'X', 'M', 'A'), 0x00 },
      -1 },
    /*     CODEC_ID_ADPCM_XA, */
    { "FFmpeg Playstation ADPCM decoder", "Playstation ADPCM", CODEC_ID_ADPCM_XA,
      (uint32_t[]){ BGAV_MK_FOURCC('A','D','X','A'),
               0x00 },
      -1 },
    /*     CODEC_ID_ADPCM_ADX, */
    
    /*     CODEC_ID_ADPCM_EA, */
    { "FFmpeg Electronicarts ADPCM decoder", "Electronicarts ADPCM",
      CODEC_ID_ADPCM_EA,
      (uint32_t[]){ BGAV_MK_FOURCC('w','v','e','a'),
                    0x00 },
      -1 },
    /*     CODEC_ID_ADPCM_G726, */
    { "FFmpeg G726 decoder", "G726 ADPCM", CODEC_ID_ADPCM_G726,
      (uint32_t[]){ BGAV_WAVID_2_FOURCC(0x0045),
                    0x00 },
      -1 },
    /*     CODEC_ID_ADPCM_CT, */
    { "FFmpeg Creative ADPCM decoder", "Creative ADPCM", CODEC_ID_ADPCM_CT,
      (uint32_t[]){ BGAV_WAVID_2_FOURCC(0x200),
                    0x00 },
      -1 },
    /*     CODEC_ID_ADPCM_SWF, */
#if 1 // Sounds disgusting (with ffplay as well). zelda.flv
    { "FFmpeg Flash ADPCM decoder", "Flash ADPCM", CODEC_ID_ADPCM_SWF,
      (uint32_t[]){ BGAV_MK_FOURCC('F', 'L', 'A', '1'), 0x00 },
      -1 },
#endif
    /*     CODEC_ID_ADPCM_YAMAHA, */
    { "FFmpeg SMAF audio decoder", "SMAF", CODEC_ID_ADPCM_YAMAHA,
      (uint32_t[]){ BGAV_MK_FOURCC('S', 'M', 'A', 'F'),
               0x00 },
      -1 },
    /*     CODEC_ID_ADPCM_SBPRO_4, */
    { "FFmpeg Soundblaster Pro ADPCM 4 decoder", "Soundblaster Pro ADPCM 4",
      CODEC_ID_ADPCM_SBPRO_4,
      (uint32_t[]){ BGAV_MK_FOURCC('S', 'B', 'P', '4'), 0x00 },
      -1 },
    /*     CODEC_ID_ADPCM_SBPRO_3, */
    { "FFmpeg Soundblaster Pro ADPCM 3 decoder", "Soundblaster Pro ADPCM 3",
      CODEC_ID_ADPCM_SBPRO_3,
      (uint32_t[]){ BGAV_MK_FOURCC('S', 'B', 'P', '3'), 0x00 },
      -1 },
    /*     CODEC_ID_ADPCM_SBPRO_2, */
    { "FFmpeg Soundblaster Pro ADPCM 2 decoder", "Soundblaster Pro ADPCM 2",
      CODEC_ID_ADPCM_SBPRO_2,
      (uint32_t[]){ BGAV_MK_FOURCC('S', 'B', 'P', '2'), 0x00 },
      -1 },
    /*     CODEC_ID_ADPCM_THP, */
#if LIBAVCODEC_BUILD >= 3352580
    { "FFmpeg THP Audio decoder", "THP Audio", CODEC_ID_ADPCM_THP,
      (uint32_t[]){ BGAV_MK_FOURCC('T', 'H', 'P', 'A'),
               0x00 } },
#endif
    /*     CODEC_ID_ADPCM_IMA_AMV, */
    /*     CODEC_ID_ADPCM_EA_R1, */
    /*     CODEC_ID_ADPCM_EA_R3, */
    /*     CODEC_ID_ADPCM_EA_R2, */
    /*     CODEC_ID_ADPCM_IMA_EA_SEAD, */
    /*     CODEC_ID_ADPCM_IMA_EA_EACS, */
    /*     CODEC_ID_ADPCM_EA_XAS, */
    /*     CODEC_ID_AMR_NB= 0x12000, */
    /*     CODEC_ID_AMR_WB, */
    /*     CODEC_ID_RA_144= 0x13000, */
    { "FFmpeg ra14.4 decoder", "Real audio 14.4", CODEC_ID_RA_144,
      (uint32_t[]){ BGAV_MK_FOURCC('1', '4', '_', '4'),
               BGAV_MK_FOURCC('l', 'p', 'c', 'J'),
                    0x00 },
      -1 },
    /*     CODEC_ID_RA_288, */
    { "FFmpeg ra28.8 decoder", "Real audio 28.8", CODEC_ID_RA_288,
      (uint32_t[]){ BGAV_MK_FOURCC('2', '8', '_', '8'), 0x00 },
      -1 },

    /*     CODEC_ID_ROQ_DPCM= 0x14000, */
    { "FFmpeg ID Roq Audio decoder", "ID Roq Audio", CODEC_ID_ROQ_DPCM,
      (uint32_t[]){ BGAV_MK_FOURCC('R','O','Q','A'),
               0x00 },
      -1 },
    /*     CODEC_ID_INTERPLAY_DPCM, */
    { "FFmpeg Interplay DPCM decoder", "Interplay DPCM", CODEC_ID_INTERPLAY_DPCM,
      (uint32_t[]){ BGAV_MK_FOURCC('I','P','D','C'),
               0x00 },
      1 },
    
    /*     CODEC_ID_XAN_DPCM, */
    /*     CODEC_ID_SOL_DPCM, */
    { "FFmpeg Old SOL decoder", "SOL (old)", CODEC_ID_SOL_DPCM,
      (uint32_t[]){ BGAV_MK_FOURCC('S','O','L','1'),
               0x00 },
      1 },

    { "FFmpeg SOL decoder (8 bit)", "SOL 8 bit", CODEC_ID_SOL_DPCM,
      (uint32_t[]){ BGAV_MK_FOURCC('S','O','L','2'),
               0x00 },
      2 },

    { "FFmpeg SOL decoder (16 bit)", "SOL 16 bit", CODEC_ID_SOL_DPCM,
      (uint32_t[]){ BGAV_MK_FOURCC('S','O','L','3'),
                    0x00 },
      3 },

    /*     CODEC_ID_MP2= 0x15000, */
#if 0
    { "FFmpeg mp2 decoder", "MPEG audio Layer 1/2/3", CODEC_ID_MP2,
      (uint32_t[]){ BGAV_WAVID_2_FOURCC(0x50), 0x00 },
      -1 },
#endif
    /*     CODEC_ID_MP3, /\* preferred ID for decoding MPEG audio layer 1, 2 or 3 *\/ */
#if 0    
    { "FFmpeg mp3 decoder", "MPEG audio Layer 1/2/3", CODEC_ID_MP3,
      (uint32_t[]){ BGAV_WAVID_2_FOURCC(0x55),
               BGAV_MK_FOURCC('.', 'm', 'p', '3'),
               BGAV_MK_FOURCC('m', 's', 0x00, 0x55),
               0x00 },
      -1 },
    
#endif
    /*     CODEC_ID_AAC, */
    /*     CODEC_ID_AC3, */
#if 0    
    { "FFmpeg ac3 decoder", "AC3", CODEC_ID_AC3,
      (uint32_t[]){ BGAV_WAVID_2_FOURCC(0x2000),
                    BGAV_MK_FOURCC('.', 'a', 'c', '3'),
                    0x00 },
      -1 },
#endif
    /*     CODEC_ID_DTS, */
    /*     CODEC_ID_VORBIS, */
    /*     CODEC_ID_DVAUDIO, */
    /*     CODEC_ID_WMAV1, */
    { "FFmpeg WMA1 decoder", "Window Media Audio 1", CODEC_ID_WMAV1,
      (uint32_t[]){ BGAV_WAVID_2_FOURCC(0x160), 0x00 },
      -1 },
    /*     CODEC_ID_WMAV2, */
    { "FFmpeg WMA2 decoder", "Window Media Audio 2", CODEC_ID_WMAV2,
      (uint32_t[]){ BGAV_WAVID_2_FOURCC(0x161), 0x00 },
      -1 },
    /*     CODEC_ID_MACE3, */
    { "FFmpeg MACE3 decoder", "Apple MACE 3", CODEC_ID_MACE3,
      (uint32_t[]){ BGAV_MK_FOURCC('M', 'A', 'C', '3'), 0x00 },
      -1 },
    /*     CODEC_ID_MACE6, */
    { "FFmpeg MACE6 decoder", "Apple MACE 6", CODEC_ID_MACE6,
      (uint32_t[]){ BGAV_MK_FOURCC('M', 'A', 'C', '6'), 0x00 },
      -1 },
    /*     CODEC_ID_VMDAUDIO, */
    { "FFmpeg Sierra VMD audio decoder", "Sierra VMD audio",
      CODEC_ID_VMDAUDIO,
      (uint32_t[]){ BGAV_MK_FOURCC('V', 'M', 'D', 'A'),
                    0x00 } },
    /*     CODEC_ID_SONIC, */
    { "FFmpeg Sonic decoder", "Sonic", CODEC_ID_SONIC,
      (uint32_t[]){ BGAV_WAVID_2_FOURCC(0x2048), 0x00 },
      -1 },
    
    /*     CODEC_ID_SONIC_LS, */
    /*     CODEC_ID_FLAC, */
    /*     CODEC_ID_MP3ADU, */
#if 1 // Sounds disgusting
    { "FFmpeg mp3 ADU decoder", "MP3 ADU", CODEC_ID_MP3ADU,
      (uint32_t[]){ BGAV_MK_FOURCC('r', 'm', 'p', '3'),
                    0x00 },
      -1 },
#endif
    /*     CODEC_ID_MP3ON4, */
    { "FFmpeg mp3on4 decoder", "MP3on4", CODEC_ID_MP3ON4,
      (uint32_t[]){ BGAV_MK_FOURCC('m', '4', 'a', 29),
                    0x00 },
      -1 },
    /*     CODEC_ID_SHORTEN, */
    { "FFmpeg Shorten decoder", "Shorten", CODEC_ID_SHORTEN,
      (uint32_t[]){ BGAV_MK_FOURCC('.','s','h','n'),
               0x00 },
      -1 },
    /*     CODEC_ID_ALAC, */
    { "FFmpeg alac decoder", "alac", CODEC_ID_ALAC,
      (uint32_t[]){ BGAV_MK_FOURCC('a', 'l', 'a', 'c'),
                    0x00 },
      -1 },
    /*     CODEC_ID_WESTWOOD_SND1, */
    { "FFmpeg Westwood SND1 decoder", "Westwood SND1", CODEC_ID_WESTWOOD_SND1,
      (uint32_t[]){ BGAV_MK_FOURCC('w','s','p','1'), 0x00 },
      -1 },
    /*     CODEC_ID_GSM, /\* as in Berlin toast format *\/ */
    /*     CODEC_ID_QDM2, */
#ifndef HAVE_W32DLL
    { "FFmpeg QDM2 decoder", "QDM2", CODEC_ID_QDM2,
      (uint32_t[]){ BGAV_MK_FOURCC('Q', 'D', 'M', '2'),
                    0x00 },
      -1 },
#endif
    /*     CODEC_ID_COOK, */
    { "FFmpeg Real cook decoder", "Real cook", CODEC_ID_COOK,
      (uint32_t[]){ BGAV_MK_FOURCC('c', 'o', 'o', 'k'),
                    0x00 },
      -1 },
    /*     CODEC_ID_TRUESPEECH, */
    { "FFmpeg Truespeech audio decoder", "Truespeech", CODEC_ID_TRUESPEECH,
      (uint32_t[]){ BGAV_WAVID_2_FOURCC(0x0022),
               0x00 },
      -1 },
    /*     CODEC_ID_TTA, */
    { "FFmpeg True audio decoder", "True audio", CODEC_ID_TTA,
      (uint32_t[]){ BGAV_MK_FOURCC('T', 'T', 'A', '1'),
                    0x00 },
      -1 },
    /*     CODEC_ID_SMACKAUDIO, */
    { "FFmpeg Smacker Audio decoder", "Smacker Audio", CODEC_ID_SMACKAUDIO,
      (uint32_t[]){ BGAV_MK_FOURCC('S','M','K','A'),
               0x00 },
      -1 },
    /*     CODEC_ID_QCELP, */
    /*     CODEC_ID_WAVPACK, */
#if LIBAVCODEC_BUILD >= ((51<<16)+(16<<8)+0)
    { "FFmpeg Wavpack decoder", "Wavpack", CODEC_ID_WAVPACK,
      (uint32_t[]){ BGAV_MK_FOURCC('w', 'v', 'p', 'k'),
                    0x00 },
      -1 },
#endif
    /*     CODEC_ID_DSICINAUDIO, */
#if LIBAVCODEC_BUILD >= ((51<<16)+(18<<8)+0)
    { "FFmpeg Delphine CIN audio decoder", "Delphine CIN Audio",
      CODEC_ID_DSICINAUDIO,
      (uint32_t[]){ BGAV_MK_FOURCC('d', 'c', 'i', 'n'),
               0x00 },
      -1 },
#endif
    /*     CODEC_ID_IMC, */
#if LIBAVCODEC_BUILD >= ((51<<16)+(23<<8)+0)
    { "FFmpeg Intel Music decoder", "Intel Music coder", CODEC_ID_IMC,
      (uint32_t[]){ BGAV_WAVID_2_FOURCC(0x0401),
               0x00 },
      -1 },
#endif
    /*     CODEC_ID_MUSEPACK7, */
    /*     CODEC_ID_MLP, */
    /*     CODEC_ID_GSM_MS, /\* as found in WAV *\/ */
    /*     CODEC_ID_ATRAC3, */
#if LIBAVCODEC_BUILD >= ((51<<16)+(40<<8)+4)
    { "FFmpeg ATRAC3 decoder", "ATRAC3", CODEC_ID_ATRAC3,
      (uint32_t[]){ BGAV_MK_FOURCC('a', 't', 'r', 'c'),
                    BGAV_WAVID_2_FOURCC(0x0270),
                    0x00  } },
#endif
    /*     CODEC_ID_VOXWARE, */
    /*     CODEC_ID_APE, */
#if LIBAVCODEC_BUILD >= ((51<<16)+(44<<8)+0)
    { "FFmpeg Monkey's Audio decoder", "Monkey's Audio", CODEC_ID_APE,
      (uint32_t[]){ BGAV_MK_FOURCC('.', 'a', 'p', 'e'),
                    0x00  } },
#endif
    /*     CODEC_ID_NELLYMOSER, */
#if LIBAVCODEC_BUILD >= ((51<<16)+(46<<8)+0)
    { "FFmpeg Nellymoser decoder", "Nellymoser", CODEC_ID_NELLYMOSER,
      (uint32_t[]){ BGAV_MK_FOURCC('N', 'E', 'L', 'L'),
                    0x00 },
      -1 },
#endif
    /*     CODEC_ID_MUSEPACK8, */
  };


#define NUM_CODECS sizeof(codec_infos)/sizeof(codec_infos[0])

static int real_num_codecs;

static struct
  {
  codec_info_t * info;
  bgav_audio_decoder_t decoder;
  } codecs[NUM_CODECS];

typedef struct
  {
  AVCodecContext * ctx;
  codec_info_t * info;
  
  gavl_audio_frame_t * frame;
  int last_frame_size;
  int frame_alloc;
  
  uint8_t * packet_buffer;
  uint8_t * packet_buffer_ptr;
  int packet_buffer_alloc;
  int bytes_in_packet_buffer;

  /* ffmpeg changes the extradata sometimes,
     so we save them locally here */
  uint8_t * ext_data;
  } ffmpeg_audio_priv;

static codec_info_t * lookup_codec(bgav_stream_t * s)
  {
  int i;
  
  for(i = 0; i < real_num_codecs; i++)
    {
    if(s->data.audio.decoder->decoder == &(codecs[i].decoder))
      return codecs[i].info;
    }
  return (codec_info_t*)0;
  }

static int decode_frame(bgav_stream_t * s)
  {
  int frame_size;
  int bytes_used;

  bgav_packet_t * p;
  ffmpeg_audio_priv * priv;
  priv= (ffmpeg_audio_priv*)(s->data.audio.decoder->priv);

  /* Read data if necessary */
  while(!priv->bytes_in_packet_buffer ||
     (s->data.audio.block_align &&
      (priv->bytes_in_packet_buffer < s->data.audio.block_align)))
    {
    /* Get packet */
    p = bgav_demuxer_get_packet_read(s->demuxer, s);
    if(!p)
      {
      return 0;
      }
    /* Move data to the beginning */
    if(priv->bytes_in_packet_buffer && (priv->packet_buffer_ptr > priv->packet_buffer))
      memmove(priv->packet_buffer, priv->packet_buffer_ptr,
              priv->bytes_in_packet_buffer);
    /* Realloc */
    if(p->data_size + priv->bytes_in_packet_buffer +
       FF_INPUT_BUFFER_PADDING_SIZE > priv->packet_buffer_alloc)
      {
      priv->packet_buffer_alloc = p->data_size +
        priv->bytes_in_packet_buffer +
        FF_INPUT_BUFFER_PADDING_SIZE + 32;
      priv->packet_buffer = realloc(priv->packet_buffer, priv->packet_buffer_alloc);
      }
    
    memcpy(priv->packet_buffer + priv->bytes_in_packet_buffer, p->data, p->data_size);
    
    priv->bytes_in_packet_buffer += p->data_size;
    priv->packet_buffer_ptr = priv->packet_buffer;
    
    memset(&(priv->packet_buffer[priv->bytes_in_packet_buffer]),
           0, FF_INPUT_BUFFER_PADDING_SIZE);
    bgav_demuxer_done_packet_read(s->demuxer, p);
    }
#if LIBAVCODEC_BUILD >= 3349760
  frame_size = priv->frame_alloc;
#else
  frame_size = 0;
#endif
  
#ifdef DUMP_DECODE
  bgav_dprintf("decode_audio Size: %d, Frame size: %d\n",
               priv->bytes_in_packet_buffer + FF_INPUT_BUFFER_PADDING_SIZE, frame_size);
  bgav_hexdump(priv->packet_buffer_ptr, 16, 16);
#endif
  
  if(priv->frame)
    {
    bytes_used = DECODE_FUNC(priv->ctx,
                             priv->frame->samples.s_16,
                             &frame_size,
                             priv->packet_buffer_ptr,
                             priv->bytes_in_packet_buffer);
    }
  else
    {
    int16_t * tmp_buf = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE*4);
    bytes_used = DECODE_FUNC(priv->ctx,
                             tmp_buf,
                             &frame_size,
                             priv->packet_buffer_ptr,
                             priv->bytes_in_packet_buffer);
    s->data.audio.format.num_channels = priv->ctx->channels;
    s->data.audio.format.samplerate   = priv->ctx->sample_rate;

    gavl_set_channel_setup(&(s->data.audio.format));
    s->data.audio.format.samples_per_frame = 2*AVCODEC_MAX_AUDIO_FRAME_SIZE;
    priv->frame = gavl_audio_frame_create(&(s->data.audio.format));
    priv->frame_alloc =
      gavl_bytes_per_sample(s->data.audio.format.sample_format) *
      s->data.audio.format.num_channels *
      s->data.audio.format.samples_per_frame;
    s->data.audio.format.samples_per_frame = 1024;

    memcpy(priv->frame->samples.s_16, tmp_buf, frame_size);
    free(tmp_buf);
    }
  
#ifdef DUMP_DECODE
  bgav_dprintf("Used %d bytes (frame size: %d)\n", bytes_used, frame_size);
#endif
  
  if(bytes_used < 0)
    {
    frame_size = -1;
    }

  /* Check for error */
    
  if(frame_size < 0)
    {
    //      if(f)
    //        f->valid_samples = samples_decoded;
    //      return samples_decoded;
    }
  /* Advance packet buffer */


  if(bytes_used > 0)
    {
    priv->packet_buffer_ptr += bytes_used;
    priv->bytes_in_packet_buffer -= bytes_used;
    if(priv->bytes_in_packet_buffer < 0)
      priv->bytes_in_packet_buffer = 0;
    }
  else
    {
    //    priv->bytes_in_packet_buffer = 0;
    }
  
  /* No Samples decoded, get next packet */

  if(frame_size < 0)
    return 1;

  /* Sometimes, frame_size is terribly wrong */

  if(frame_size > AVCODEC_MAX_AUDIO_FRAME_SIZE*2)
    {
    frame_size = AVCODEC_MAX_AUDIO_FRAME_SIZE*2;
    }
  
  frame_size /= (2 * s->data.audio.format.num_channels);
  priv->last_frame_size = frame_size;
  priv->frame->valid_samples = frame_size;

  return 1;
  }


static int init(bgav_stream_t * s)
  {
  AVCodec * codec;
  ffmpeg_audio_priv * priv;
  priv = calloc(1, sizeof(*priv));
  priv->info = lookup_codec(s);
  codec = avcodec_find_decoder(priv->info->ffmpeg_id);
  priv->ctx = avcodec_alloc_context();
  

  //  priv->ctx->width = s->format.frame_width;
  //  priv->ctx->height = s->format.frame_height;

  if(s->ext_size)
    {
    priv->ext_data = calloc(1, s->ext_size +
                            FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(priv->ext_data, s->ext_data, s->ext_size);
    
    priv->ctx->extradata = priv->ext_data;
    priv->ctx->extradata_size = s->ext_size;
    }
  
#ifdef DUMP_EXTRADATA
  bgav_dprintf("Adding extradata %d bytes\n", priv->ctx->extradata_size);
  bgav_hexdump(priv->ctx->extradata, priv->ctx->extradata_size, 16);
#endif    
  priv->ctx->channels        = s->data.audio.format.num_channels;
  priv->ctx->sample_rate     = s->data.audio.format.samplerate;
  priv->ctx->block_align     = s->data.audio.block_align;
  priv->ctx->bit_rate        = s->codec_bitrate;
  priv->ctx->bits_per_sample = s->data.audio.bits_per_sample;

  if(priv->info->codec_tag != -1)
    priv->ctx->codec_tag = priv->info->codec_tag;
  else
    priv->ctx->codec_tag = bswap_32(s->fourcc);

  priv->ctx->codec_id  = codec->id;
  
  /* Some codecs need extra stuff */
    
  /* Open codec */
  
  if(avcodec_open(priv->ctx, codec) != 0)
    return 0;
  s->data.audio.decoder->priv = priv;

  /* Set missing format values */
  
  s->data.audio.format.interleave_mode = GAVL_INTERLEAVE_ALL;
  s->data.audio.format.sample_format = GAVL_SAMPLE_S16;

  /* Format already known */
  if(s->data.audio.format.num_channels && s->data.audio.format.samplerate)
    {
    gavl_set_channel_setup(&(s->data.audio.format));
    s->data.audio.format.samples_per_frame = 2*AVCODEC_MAX_AUDIO_FRAME_SIZE;
    priv->frame = gavl_audio_frame_create(&(s->data.audio.format));
    priv->frame_alloc =
      gavl_bytes_per_sample(s->data.audio.format.sample_format) *
      s->data.audio.format.num_channels *
      s->data.audio.format.samples_per_frame;
    s->data.audio.format.samples_per_frame = 1024;
    }
  else /* Let ffmpeg find out the format */
    {
    if(!decode_frame(s))
      return 0;
    }
    
  s->description = bgav_sprintf(priv->info->format_name);
  return 1;
  }


static int decode(bgav_stream_t * s, gavl_audio_frame_t * f, int num_samples)
  {
  ffmpeg_audio_priv * priv;
  int samples_decoded = 0;
  int samples_copied;
  priv= (ffmpeg_audio_priv*)(s->data.audio.decoder->priv);

  while(samples_decoded < num_samples)
    {
    if(!priv->frame->valid_samples)
      {
      if(!decode_frame(s))
        {
        if(f)
          f->valid_samples = samples_decoded;
        return samples_decoded;
        }
      }
    samples_copied = gavl_audio_frame_copy(&(s->data.audio.format),
                                           f,
                                           priv->frame,
                                           samples_decoded, /* out_pos */
                                           priv->last_frame_size - priv->frame->valid_samples,  /* in_pos */
                                           num_samples - samples_decoded, /* out_size, */
                                           priv->frame->valid_samples /* in_size */);
    priv->frame->valid_samples -= samples_copied;
    samples_decoded += samples_copied;
    }
  if(f)
    f->valid_samples = samples_decoded;
  return samples_decoded;
  }

static void resync_ffmpeg(bgav_stream_t * s)
  {
  ffmpeg_audio_priv * priv;
  priv = (ffmpeg_audio_priv*)(s->data.audio.decoder->priv);
  avcodec_flush_buffers(priv->ctx);
  priv->frame->valid_samples = 0;
  priv->bytes_in_packet_buffer = 0;
  }

static void close_ffmpeg(bgav_stream_t * s)
  {
  ffmpeg_audio_priv * priv;
  priv= (ffmpeg_audio_priv*)(s->data.audio.decoder->priv);

  if(!priv)
    return;
  
  if(priv->ext_data)
    free(priv->ext_data);
  
  if(priv->frame)
    gavl_audio_frame_destroy(priv->frame);
  
  if(priv->packet_buffer)
    free(priv->packet_buffer);

  if(priv->ctx)
    {
    avcodec_close(priv->ctx);
    free(priv->ctx);
    }
  free(priv);
  }

void
bgav_init_audio_decoders_ffmpeg(bgav_options_t * opt)
  {
  int i;
  real_num_codecs = 0;
  avcodec_init();
  avcodec_register_all();

  for(i = 0; i < NUM_CODECS; i++)
    {
    if(avcodec_find_decoder(codec_infos[i].ffmpeg_id))
      {
      codecs[real_num_codecs].info = &(codec_infos[i]);
      codecs[real_num_codecs].decoder.name =
        codecs[real_num_codecs].info->decoder_name;
      codecs[real_num_codecs].decoder.fourccs =
        codecs[real_num_codecs].info->fourccs;
      codecs[real_num_codecs].decoder.init = init;
      codecs[real_num_codecs].decoder.decode = decode;
      codecs[real_num_codecs].decoder.close = close_ffmpeg;
      codecs[real_num_codecs].decoder.resync = resync_ffmpeg;
      bgav_audio_decoder_register(&codecs[real_num_codecs].decoder);
      
      real_num_codecs++;
      }
    else
      bgav_log(opt, BGAV_LOG_WARNING, LOG_DOMAIN, "Codec not found: %s",
               codec_infos[i].decoder_name);
    }
  }

