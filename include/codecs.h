#ifdef HAVE_LIBAVCODEC
void bgav_init_audio_decoders_ffmpeg();
void bgav_init_video_decoders_ffmpeg();
#endif
#ifdef HAVE_VORBIS
void bgav_init_audio_decoders_vorbis();
#endif

#ifdef HAVE_LIBA52
void bgav_init_audio_decoders_a52();
#endif

#ifdef HAVE_MAD
void bgav_init_audio_decoders_mad();
#endif

void bgav_init_video_decoders_qtraw();
void bgav_init_video_decoders_qtrle();


#ifdef HAVE_LIBPNG
void bgav_init_video_decoders_png();
#endif

#ifdef HAVE_FAAD2
void bgav_init_audio_decoders_faad2();
#endif

#ifdef HAVE_FLAC
void bgav_init_audio_decoders_flac();
#endif

#ifdef HAVE_LIBMPEG2
void bgav_init_video_decoders_libmpeg2();
#endif
#ifdef HAVE_XADLL
int bgav_init_video_decoders_xadll();
extern char * bgav_dll_path_xanim;
#endif

#ifdef HAVE_REALDLL
int bgav_init_video_decoders_real();
int bgav_init_audio_decoders_real();
extern char * bgav_dll_path_real;
#endif

#ifdef HAVE_W32DLL
int bgav_init_video_decoders_win32();
int bgav_init_audio_decoders_win32();
int bgav_init_audio_decoders_qtwin32();
extern char * win32_def_path;
#endif

void bgav_init_audio_decoders_aiff();