/*****************************************************************
 
  codecs.c
 
  Copyright (c) 2003-2004 by Burkhard Plaum - plaum@ipf.uni-stuttgart.de
 
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
#include <stdlib.h>
#include <config.h>
#include <codecs.h>
#include <pthread.h>

#include <avdec_private.h>
#include <utils.h>

static void codecs_lock();
static void codecs_unlock();

static void codecs_uninit();

#ifdef HAVE_REALDLL
static const char * env_name_real = "GMERLIN_AVDEC_CODEC_PATH_REAL";

void bgav_set_dll_path_real(const char * path)
  {
  codecs_lock();
  if(bgav_dll_path_real)
    {
    free(bgav_dll_path_real);
    }
  bgav_dll_path_real = bgav_strndup(path, NULL);
  codecs_uninit();
  codecs_unlock();
  }
#else
void bgav_set_dll_path_real(const char * path) { }
#endif

#ifdef HAVE_XADLL

static const char * env_name_xanim =  "GMERLIN_AVDEC_CODEC_PATH_XANIM";

void bgav_set_dll_path_xanim(const char * path)
  {
  codecs_lock();
  if(bgav_dll_path_xanim)
    {
    free(bgav_dll_path_xanim);
    bgav_dll_path_xanim = (char*)0;
    }
  bgav_dll_path_xanim = bgav_strndup(path, NULL);

  codecs_uninit();
  codecs_unlock();
  }
#else
void bgav_set_dll_path_xanim(const char * path) {}
#endif

#ifdef HAVE_W32DLL
static const char * env_name_win32 = "GMERLIN_AVDEC_CODEC_PATH_WIN32";

static int win_path_needs_delete = 0;

void bgav_set_dll_path_win32(const char * path)
  {
  codecs_lock();
  if(win_path_needs_delete)
    {
    free(win32_def_path);
    }
  win32_def_path = bgav_strndup(path, (char*)0);
  codecs_uninit();
  codecs_unlock();
  }
#else
void bgav_set_dll_path_win32(const char * path) {}
#endif

/* Simple codec registry */

/*
 *  Since we link all codecs statically,
 *  this stays quite simple
 */

static bgav_audio_decoder_t * audio_decoders = (bgav_audio_decoder_t*)0;
static bgav_video_decoder_t * video_decoders = (bgav_video_decoder_t*)0;
static int codecs_initialized = 0;

static int num_audio_codecs = 0;
static int num_video_codecs = 0;

static pthread_mutex_t codec_mutex;
static int mutex_initialized = 0;

static void codecs_uninit()
  {
  audio_decoders = (bgav_audio_decoder_t*)0;
  video_decoders = (bgav_video_decoder_t*)0;
  codecs_initialized = 0;
  num_audio_codecs = 0;
  num_video_codecs = 0;
  }

static void codecs_lock()
  {
  if(!mutex_initialized)
    {
    pthread_mutex_init(&(codec_mutex),(pthread_mutexattr_t *)0);
    mutex_initialized = 1;
    }
  pthread_mutex_lock(&(codec_mutex));
  }

static void codecs_unlock()
  {
  pthread_mutex_unlock(&(codec_mutex));
  }

/***************************************************************
 * This will hopefully make the destruction for dynamic loading
 * (Trick comes from a 1995 version of the ELF Howto, so it
 * should work everywhere now)
 ***************************************************************/


#if defined(__GNUC__) && defined(__ELF__)

static void codecs_free() __attribute__ ((destructor));
static void codecs_free()
  {
  if(mutex_initialized)
    pthread_mutex_destroy(&codec_mutex);
  }

#endif

void bgav_codecs_dump()
  {
  bgav_audio_decoder_t * ad;
  bgav_video_decoder_t * vd;
  int name_len = 0;
  int i, j, k;
  int len;

  bgav_codecs_init();
  
  ad = audio_decoders;
  for(i = 0; i < num_audio_codecs; i++)
    {
    len = strlen(ad->name);
    if(len > name_len)
      name_len = len;
    ad = ad->next;
    }
  vd = video_decoders;
  for(i = 0; i < num_video_codecs; i++)
    {
    len = strlen(vd->name);
    if(len > name_len)
      name_len = len;
    vd = vd->next;
    }
  
  /* Print */
  ad = audio_decoders;

  fprintf(stderr, "=========== Audio codecs ===========\n");

  for(i = 0; i < num_audio_codecs; i++)
    {
    len = strlen(ad->name);
    fprintf(stderr, ad->name);
    for(j = 0; j < name_len - len; j++)
      fprintf(stderr, " ");

    j = 0;
    while(ad->fourccs[j])
      {
      if(!j)
        fprintf(stderr, " ");
      else
        {
        for(k = 0; k <= name_len; k++)
          fprintf(stderr, " ");
        }
      bgav_dump_fourcc(ad->fourccs[j]);
      fprintf(stderr, "\n");
      j++;
      }
    ad = ad->next;
    }
  fprintf(stderr, "=========== Video codecs ===========\n");
  vd = video_decoders;
  for(i = 0; i < num_video_codecs; i++)
    {
    len = strlen(vd->name);
    fprintf(stderr, vd->name);
    for(j = 0; j < name_len - len; j++)
      fprintf(stderr, " ");
    j = 0;
    while(vd->fourccs[j])
      {
      if(!j)
        fprintf(stderr, " ");
      else
        {
        for(k = 0; k <= name_len; k++)
          fprintf(stderr, " ");
        }
      bgav_dump_fourcc(vd->fourccs[j]);
      fprintf(stderr, "\n");
      j++;
      }
    vd = vd->next;
    }
  }

/*
 *  Print some verbose error messages so users will know
 *  how to set up their systems
 */

static void print_error_nopath(const char * name, const char * env_name)
  {
  fprintf(stderr, "Didn't find path for %s\n", name);
  fprintf(stderr, "Configure it in the application or via the environment variable %s\n",
          env_name);
  }

static void print_error_nofile(const char * name, const char * env_name)
  {
  fprintf(stderr, "Didn't find some %s\n", name);
  fprintf(stderr, "Configure codec paths in the application or via the environment variable %s\n",
          env_name);
  }

void bgav_codecs_init()
  {
  codecs_lock();
  if(codecs_initialized)
    {
    codecs_unlock();
    return;
    }
//  fprintf(stderr, "bgav_codecs_init()\n");
  codecs_initialized = 1;

  bgav_init_video_decoders_qtraw();
  //  bgav_init_video_decoders_qtrle();
  
#ifdef HAVE_LIBAVCODEC
  bgav_init_audio_decoders_ffmpeg();
  bgav_init_video_decoders_ffmpeg();
#endif
#ifdef HAVE_VORBIS
  bgav_init_audio_decoders_vorbis();
#endif

#ifdef HAVE_LIBA52
  bgav_init_audio_decoders_a52();
#endif

#ifdef HAVE_MAD
  bgav_init_audio_decoders_mad();
#endif

#ifdef HAVE_LIBPNG
  bgav_init_video_decoders_png();
#endif

#ifdef HAVE_FAAD2
  bgav_init_audio_decoders_faad2();
#endif

#ifdef HAVE_FLAC
  bgav_init_audio_decoders_flac();
#endif

  
#ifdef HAVE_LIBMPEG2
  bgav_init_video_decoders_libmpeg2();
#endif
  
#ifdef HAVE_XADLL

  if(!bgav_dll_path_xanim)
    bgav_dll_path_xanim = bgav_strndup(getenv(env_name_xanim), NULL);
  
  if(!bgav_dll_path_xanim)
    print_error_nopath("Xanim dlls", env_name_xanim);
  else
    {
    if(!bgav_init_video_decoders_xadll())
      {
      print_error_nofile("Xanim dlls", env_name_xanim);
      }
    }
#endif

#ifdef HAVE_REALDLL
  if(!bgav_dll_path_real)
    bgav_dll_path_real = bgav_strndup(getenv(env_name_real), NULL);

  if(!bgav_dll_path_real)
    print_error_nopath("Real DLLs", env_name_real);
  else
    {
    if(!bgav_init_video_decoders_real())
      {
      print_error_nofile("Real video DLLs", env_name_real);
      }
    if(!bgav_init_audio_decoders_real())
      {
      print_error_nofile("Real audio DLLs", env_name_real);
      }
    }
#endif

#ifdef HAVE_W32DLL

  if(!win32_def_path)
    {
    win32_def_path = bgav_strndup(getenv(env_name_win32), NULL);
    }

  if(!win32_def_path)
    print_error_nopath("Win32 DLLs", env_name_real);
  else
    {
    if(!bgav_init_video_decoders_win32())
      {
      print_error_nofile("Win32 video DLLs", env_name_win32);
      }
    if(!bgav_init_audio_decoders_win32())
      {
      print_error_nofile("Win32 audio DLLs", env_name_win32);
      }
    if(!bgav_init_audio_decoders_qtwin32())
      {
      print_error_nofile("Win32 Quicktime audio DLLs", env_name_win32);
      }
    }
  
#endif

  
  bgav_init_audio_decoders_aiff();
  
  //  fprintf(stderr, "BGAV Codecs initialized: A: %d V: %d\n",
  //          num_audio_codecs, num_video_codecs);

  codecs_unlock();
  
  }

void bgav_audio_decoder_register(bgav_audio_decoder_t * dec)
  {
  bgav_audio_decoder_t * before;
  if(!audio_decoders)
    audio_decoders = dec;
  else
    {
    before = audio_decoders;
    while(before->next)
      before = before->next;
    before->next = dec;
    }
  dec->next = (bgav_audio_decoder_t*)0;
  num_audio_codecs++;
  }

void bgav_video_decoder_register(bgav_video_decoder_t * dec)
  {
  bgav_video_decoder_t * before;
  if(!video_decoders)
    video_decoders = dec;
  else
    {
    before = video_decoders;
    while(before->next)
      before = before->next;
    before->next = dec;
    }
  dec->next = (bgav_video_decoder_t*)0;
  num_video_codecs++;
  }

bgav_audio_decoder_t * bgav_find_audio_decoder(bgav_stream_t * s)
  {
  bgav_audio_decoder_t * cur;
  int i;
  codecs_lock();
  cur = audio_decoders;
  if(!codecs_initialized)
    bgav_codecs_init();
  
  //  fprintf(stderr, "Seeking audio codec ");
  //  bgav_dump_fourcc(s->fourcc);
  //  fprintf(stderr, "\n");
  
  while(cur)
    {
    i = 0;
    //    fprintf(stderr, "Trying %s\n", cur->name);
    while(cur->fourccs[i])
      {
      if(cur->fourccs[i] == s->fourcc)
        {
        codecs_unlock();
        return cur;
        }
      else
        i++;
      }
    cur = cur->next;
    }
  codecs_unlock();
  return (bgav_audio_decoder_t*)0;
  }

bgav_video_decoder_t * bgav_find_video_decoder(bgav_stream_t * s)
  {
  bgav_video_decoder_t * cur;
  int i;
  codecs_lock();

  if(!codecs_initialized)
    bgav_codecs_init();
  
  cur = video_decoders;

  //  fprintf(stderr, "Seeking video codec ");
  //  bgav_dump_fourcc(s->fourcc);
  //  fprintf(stderr, "\n");
  
  while(cur)
    {
    i = 0;
    while(cur->fourccs[i])
      {
      if(cur->fourccs[i] == s->fourcc)
        {
        codecs_unlock();
        return cur;
        }
      else
        i++;
      }
    cur = cur->next;
    }
  codecs_unlock();
  return (bgav_video_decoder_t*)0;
  }

