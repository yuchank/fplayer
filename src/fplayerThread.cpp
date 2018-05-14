extern "C" {
  #include <libavformat/avformat.h>
  #include <libswresample/swresample.h>
  #include <libswscale/swscale.h>
  #include <libavutil/avutil.h>
  #include <libavutil/avstring.h>
  #include <libavutil/opt.h>
  #include <libavutil/imgutils.h>
}

#include <SDL2/SDL.h>

#define SDL_AUDIO_BUFFER_SIZE 1024

typedef struct _PacketQueue {
  
} PacketQueue;

typedef struct _VideoState {
  AVFormatContext *pFormatCtx;

  // audio
  int audio_index;
  AVStream *audio_stream;
  AVCodecContext *audio_ctx;
  PacketQueue audioq;
  unsigned int audio_buf_size;
  unsigned int audio_buf_index;
  AVFrame audio_frame;
  AVPacket audio_pkt;
  struct SwrContext *pSwrCtx;

  // video
  int video_index;
  AVStream *video_stream;
  AVCodecContext *video_ctx;
  PacketQueue videoq;
  struct SwsContext *pSwsCtx;
  AVFrame *pFrameRGB;
  uint8_t *pFrameBuffer;

  // system
  SDL_Thread *parse_tid;
  SDL_Thread *video_tid;

  char filename[1024];
} VideoState;

VideoState *global_video_state;

int packet_queue_init(PacketQueue *q) {

}

void audio_callback(void *userdata, Uint8 *stream, int len) {

}

int video_thread(void *arg) {

}

int stream_component_open(VideoState *is, int stream_index) {
  AVFormatContext *pFormatCtx = is->pFormatCtx;
  SDL_AudioSpec wanted_spec, spec;
  AVCodecParameters *codecPar = pFormatCtx->streams[stream_index]->codecpar;

  AVCodec *pCodec = avcodec_find_decoder(pFormatCtx->streams[stream_index]->codecpar->codec_id);
  AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
  avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[stream_index]->codecpar);
  avcodec_open2(pCodecCtx, pCodec, NULL);

  if (pCodecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
    wanted_spec.freq = pCodecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;    // AUDIO_F32
    wanted_spec.channels = pCodecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = is;

		is->pSwrCtx = swr_alloc();
		
		av_opt_set_channel_layout(is->pSwrCtx, "in_channel_layout", pCodecCtx->channel_layout, 0);
		av_opt_set_channel_layout(is->pSwrCtx, "out_channel_layout", pCodecCtx->channel_layout, 0);
		av_opt_set_int(is->pSwrCtx, "in_sample_rate", pCodecCtx->sample_rate, 0);
		av_opt_set_int(is->pSwrCtx, "out_sample_rate", pCodecCtx->sample_rate, 0);
		av_opt_set_sample_fmt(is->pSwrCtx, "in_sample_fmt", pCodecCtx->sample_fmt, 0);
		av_opt_set_sample_fmt(is->pSwrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

		swr_init(is->pSwrCtx);

		SDL_OpenAudio(&wanted_spec, &spec);
    
    is->audio_stream = pFormatCtx->streams[stream_index];
    is->audio_ctx = pCodecCtx;
    is->audio_buf_size = 0;
    is->audio_buf_index = 0;
    memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
    packet_queue_init(&is->audioq);

    SDL_PauseAudio(0);
  }
  else if (pCodecCtx->codec_type == AVMEDIA_TYPE_VIDEO) {
    is->video_stream = pFormatCtx->streams[stream_index];
    is->video_ctx = pCodecCtx;
    is->pSwsCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
		is->pFrameRGB = av_frame_alloc();
		int rgbFrameSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codecPar->width, codecPar->height, 8);
		is->pFrameBuffer = static_cast<uint8_t *>(av_malloc(rgbFrameSize));
		av_image_fill_arrays(&is->pFrameRGB->data[0], &is->pFrameRGB->linesize[0], is->pFrameBuffer, AV_PIX_FMT_RGB24, codecPar->width, codecPar->height, 1);
    packet_queue_init(&is->videoq);
    is->video_tid = SDL_CreateThread(video_thread, "video_thread", is);
  }
  else {
    avcodec_free_context(&pCodecCtx);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int decode_thread(void *arg) {
  VideoState *is = static_cast<VideoState *>(arg);
  AVFormatContext *pFormatCtx;

  global_video_state = is;

  avformat_open_input(&pFormatCtx, is->filename, NULL, NULL);
  is->pFormatCtx = pFormatCtx;

  avformat_find_stream_info(pFormatCtx, NULL);
  av_dump_format(pFormatCtx, 0, is->filename, 0);

  is->video_index = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  is->audio_index = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, is->video_index, NULL, 0);

  stream_component_open(is, is->video_index);
  stream_component_open(is, is->audio_index);

  while (1) {

  }
}

int main(int argc, char *argv[])
{
  const char *szFilePath = "land.mkv";

  VideoState *is = static_cast<VideoState *>(av_mallocz(sizeof(VideoState)));
  SDL_Event event;

  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);

  av_strlcpy(is->filename, szFilePath, sizeof(is->filename));

  SDL_Window *window = 	window = SDL_CreateWindow(szFilePath, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_RESIZABLE);

  is->parse_tid = SDL_CreateThread(decode_thread, "decode_thread", is);

  while (1) {
    SDL_WaitEvent(&event);
    switch(event.type) {
      case SDL_QUIT:
        SDL_Quit();
        goto cleanup;
      default:
        break;
    }
  }
cleanup:
  if (is->pFrameBuffer) {
    av_free(is->pFrameBuffer);
  }
  if (is->pFrameRGB) {
    av_frame_free(&is->pFrameRGB);
  }
  if (is->pSwsCtx) {
    sws_freeContext(is->pSwsCtx);
  }
  if (is->pSwrCtx) {
    swr_free(&is->pSwrCtx);
  }
  if (is->video_ctx) {
    avcodec_free_context(&is->video_ctx);
  }
  if (is->audio_ctx) {
    avcodec_free_context(&is->audio_ctx);
  }
  if (is->pFormatCtx) {
    avformat_close_input(&is->pFormatCtx);
  }
  if (is) {
    av_free(is);
  }
  return 0;
}
