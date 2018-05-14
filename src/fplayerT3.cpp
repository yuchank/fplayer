extern "C" {
  #include <libavformat/avformat.h>
  #include <libavutil/avutil.h>
}

#include <SDL2/SDL.h>

#define FF_REFRESH_EVENT  (SDL_USEREVENT + 1)
#define FF_QUIT_EVENT     (SDL_USEREVENT + 2)

typedef struct _PacketQueue
{
} PacketQueue;

typedef struct _VideoPicture
{	
} VideoPicture;

typedef struct _VideoState {
  AVFormatContext *pFormatCtx;
  AVCodecContext *audioCtx;
  AVCodecContext *videoCtx;

  int video_index, audio_index;

  PacketQueue audioq;
  PacketQueue videoq;

  SDL_mutex *pictq_mutex;
  SDL_cond *pictq_cond;

  SDL_Thread *parse_tid;
  SDL_Thread *video_tid;

  int quit;
} VideoState;

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

int queue_picture(VideoState *is, AVFrame *frame) {

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
  
  VideoState *is = static_cast<VideoState *>(av_mallocz(sizeof(VideoState)));

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