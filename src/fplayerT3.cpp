extern "C" {
  #include <libavformat/avformat.h>
  #include <libswscale/swscale.h>
  #include <libswresample/swresample.h>
  #include <libavutil/avutil.h>
  #include <libavutil/avstring.h>
}

#include <SDL2/SDL.h>

#define FF_REFRESH_EVENT  (SDL_USEREVENT + 1)
#define FF_QUIT_EVENT     (SDL_USEREVENT + 2)

#define VIDEO_PICTURE_QUEUE_SIZE (1)

typedef struct _PacketQueue
{
} PacketQueue;

typedef struct _VideoPicture
{	
  SDL_Texture *texture;
  int width, height;
  int allocated;
} VideoPicture;

typedef struct _VideoState {
  AVFormatContext *pFormatCtx;
  AVCodecContext *audioCtx;
  AVCodecContext *videoCtx;

  struct SwrContext *pSwrCtx;
  struct SwsContext *pSwsCtx;

  int video_index, audio_index;

  AVStream *audio_stream;
  AVStream *video_stream;

  AVFrame *pFrameRGB;

  PacketQueue audioq;
  PacketQueue videoq;

  VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
  int pictq_size, pictq_rindex, pictq_windex;

  SDL_mutex *pictq_mutex;
  SDL_cond *pictq_cond;

  SDL_Thread *parse_tid;
  SDL_Thread *video_tid;

  SDL_Window *window;
  SDL_Renderer *renderer;

  char filename[1024];
  int quit;
} VideoState;

SDL_mutex *screen_mutex;

void PacketQueueInit(PacketQueue *q) {
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {  
}

void audio_callback(void *userdata, uint8_t * stream, int len) {
}

Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque) {
  SDL_Event event;
  event.type = FF_REFRESH_EVENT;
  event.user.data1 = opaque;
  SDL_PushEvent(&event);
  return 0; /* 0 means stop timer */
}

void schedule_refresh(VideoState *is, int delay) {
  SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

void video_refresh_timer(void *userdata) {
}

void alloc_picture(void *userdata) {
  VideoState *is = static_cast<VideoState *>(userdata);
  VideoPicture *vp;

  vp = &is->pictq[is->pictq_windex];
  if (vp->texture) {
    SDL_DestroyTexture(vp->texture);
  }
  SDL_LockMutex(screen_mutex);
  vp->texture = SDL_CreateTexture(is->renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, is->videoCtx->width, is->videoCtx->height);
  SDL_UnlockMutex(screen_mutex);
  vp->width = is->videoCtx->width;
  vp->height = is->videoCtx->height;
  vp->allocated = 1;
}

int queue_picture(VideoState *is, AVFrame *frame) {
  VideoPicture *vp;

  SDL_LockMutex(is->pictq_mutex);
  while (is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE && !is->quit) {
    SDL_CondWait(is->pictq_cond, is->pictq_mutex);
  }
  SDL_UnlockMutex(is->pictq_mutex);

  // windex is set to 0 initially
  vp = &is->pictq[is->pictq_windex];

  // allocate or resize the buffer
  if (!vp->texture || vp->width != is->videoCtx->width || vp->height != is->videoCtx->height) {
    vp->allocated = 0;
    alloc_picture(is);
  }

  if (vp->texture) {
    sws_scale(is->pSwsCtx, frame->data, frame->linesize, 0, is->videoCtx->height, is->pFrameRGB->data, is->pFrameRGB->linesize);
    SDL_UpdateTexture(vp->texture, NULL, is->pFrameRGB->data[0], is->pFrameRGB->linesize[0]);

    if (++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
      is->pictq_windex = 0;
    }

    SDL_LockMutex(is->pictq_mutex);
    is->pictq_size++;
    SDL_UnlockMutex(is->pictq_mutex);
  }

  return 0;
}

int video_thread(void *arg) {
  VideoState *is = static_cast<VideoState *>(arg);
  AVPacket pkt1, *packet = &pkt1;
	int rv;
	AVFrame *pFrame = NULL;

	pFrame = av_frame_alloc();

  while (packet_queue_get(&is->videoq, packet, 1) > 0)
	{
    rv = avcodec_send_packet(is->videoCtx, packet);
		if (rv < 0) {
      continue;
    }
		
		while (!avcodec_receive_frame(is->videoCtx, pFrame)) {
      if (queue_picture(is, pFrame) < 0)
			{
				break;
			}
      av_packet_unref(packet);
    }
	}

  return EXIT_SUCCESS;
}

int stream_component_open(VideoState *is, int stream_index) {
  AVFormatContext *pFormatCtx = is->pFormatCtx;
  AVCodecContext *codecCtx;
  AVCodec *codec;
  SDL_AudioSpec wanted_spec = { 0 }, spec = { 0 };

  AVCodecParameters *codecpar = pFormatCtx->streams[stream_index]->codecpar;
  codec = avcodec_find_decoder(codecpar->codec_id);
  codecCtx = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(codecCtx, codecpar);
  
  if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = is;
    SDL_OpenAudio(&wanted_spec, &spec);
  } 
  avcodec_open2(codecCtx, codec, NULL);

  if (codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
    SDL_PauseAudio(0);
  }
  else if (codecCtx->codec_type == AVMEDIA_TYPE_VIDEO) {
    is->videoCtx = codecCtx;
    is->video_index = stream_index;
    is->video_stream = pFormatCtx->streams[stream_index];
    is->pSwsCtx = sws_getContext(codecCtx->width, codecCtx->height, codecCtx->pix_fmt, codecCtx->width, codecCtx->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);

    is->window = SDL_CreateWindow(is->filename, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, codecpar->width, codecpar->height, 0);
    is->renderer = SDL_CreateRenderer(is->window, -1, 0);
    is->video_tid = SDL_CreateThread(video_thread, "video", is);
  }

  return EXIT_SUCCESS;
}

int decode_thread(void *arg) {
  VideoState *is = static_cast<VideoState *>(arg);
  AVPacket pkt1, *packet = &pkt1;

  int video_index = -1;
  int audio_index = -1;

  stream_component_open(is, audio_index);
  stream_component_open(is, video_index);

  while (1) {
    av_read_frame(is->pFormatCtx, packet);

    if (packet->stream_index == is->video_index) {
    }
    else if (packet->stream_index == is->audio_index) {
    }
    else {
    }
  }

  while (!is->quit) {
    SDL_Delay(100);
  }

fail:
  SDL_Event event;
  event.type = FF_QUIT_EVENT;
  event.user.data1 = is;
  SDL_PushEvent(&event);

  return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
  SDL_Event event;
  
  const char *filename = "file:little.mkv";
  
  VideoState *is = static_cast<VideoState *>(av_mallocz(sizeof(VideoState)));

  screen_mutex = SDL_CreateMutex();

  av_strlcpy(is->filename, filename, sizeof(is->filename));

  is->pictq_mutex = SDL_CreateMutex();
  is->pictq_cond = SDL_CreateCond();

  schedule_refresh(is, 40);

  is->parse_tid = SDL_CreateThread(decode_thread, "parse", is);

  while (1) {
    SDL_WaitEvent(&event);
    switch (event.type) {
      case FF_QUIT_EVENT:
      case SDL_QUIT:
        break;
      case FF_REFRESH_EVENT:
        video_refresh_timer(event.user.data1);
        break;
      default:
        break;
    }
  }
  
  SDL_DestroyMutex(is->pictq_mutex);
  SDL_DestroyCond(is->pictq_cond);

  av_free(is);

  return EXIT_SUCCESS;
}