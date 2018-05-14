extern "C" {
  #include <libavformat/avformat.h>
  #include <libavcodec/avcodec.h>
  #include <libswscale/swscale.h>
  #include <libavutil/imgutils.h>
  #include <libswresample/swresample.h>
  #include <libavdevice/avdevice.h>
}

#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

#include <cassert>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE  19200

typedef struct _PacketQueue {
  AVPacketList *first_pkt, *last_pkt;
  int nb_packets;
  int size;         // packet->size
  SDL_mutex *mutex; // SDL is running the process as a separate thread.
  SDL_cond *cond;
} PacketQueue;

int quit = 0;

PacketQueue audioq;

SwrContext *swrCtx;

void packet_queue_init(PacketQueue *q) {
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
  AVPacketList *pktl;
  AVPacket p;
  av_packet_ref(&p, pkt);
  pktl = (AVPacketList *)av_malloc(sizeof(AVPacketList));
  pktl->pkt = p;
  pktl->next = NULL;
  SDL_LockMutex(q->mutex);
  if (!q->last_pkt) {
    q->first_pkt = pktl;
  } else {
    q->last_pkt->next = pktl;
  }
  q->last_pkt = pktl;
  q->nb_packets++;
  q->size += pktl->pkt.size;
  SDL_CondSignal(q->cond);
  SDL_UnlockMutex(q->mutex);

  return 0;
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
  AVPacketList *pktl;
  int ret;

  SDL_LockMutex(q->mutex);

  while (1) {
    if (quit) {
      ret = -1;
      break;
    }

    pktl = q->first_pkt;
    if (pktl) {
      q->first_pkt = pktl->next;
      if (!q->first_pkt) {
        q->last_pkt = NULL;
      }
      q->nb_packets--;
      q->size -= pktl->pkt.size;
      *pkt = pktl->pkt;
      av_free(pktl);
      ret = 1;
      break;
    } else if (!block) {
      ret = 0;
      break;
    } else {
      SDL_CondWait(q->cond, q->mutex);
    }
  }

  SDL_UnlockMutex(q->mutex);

  return ret;
}

int audio_decode_frame(AVCodecContext *pACtx, uint8_t *audio_buf, int buf_size) {
  static AVPacket pkt = { 0 };
  static uint8_t *audio_pkt_data = NULL;
  static int audio_pkt_size = 0;
  static AVFrame frame;

  int len1, data_size = 0;

  static uint8_t converted_data[(192000 * 3) / 2];
	static uint8_t * converted = &converted_data[0];;
  int len2;

  while (1) {
    while (audio_pkt_size > 0) {
      int got_frame = 0;
      len1 = avcodec_decode_audio4(pACtx, &frame, &got_frame, &pkt);
      if (len1 < 0) {
	      /* if error, skip frame */
	      audio_pkt_size = 0;
	      break;
      }
      audio_pkt_data += len1;
      audio_pkt_size -= len1;
      data_size = 0;
      if (got_frame) {
	      data_size = av_samples_get_buffer_size(NULL, pACtx->channels, frame.nb_samples, pACtx->sample_fmt, 1);
	      assert(data_size <= buf_size);
        int outSize = av_samples_get_buffer_size(NULL, pACtx->channels, frame.nb_samples, AV_SAMPLE_FMT_FLT, 1);
				len2 = swr_convert(swrCtx, &converted, frame.nb_samples, (const uint8_t**)&frame.data[0], frame.nb_samples);
				memcpy(audio_buf, converted_data, outSize);
				data_size = outSize;
	      // memcpy(audio_buf, frame.data[0], data_size);
      }
      if (data_size <= 0) {
	      /* No data yet, get more frames */
	      continue;
      }
      /* We have data, return it and come back for more later */
      return data_size;
    }
    if (pkt.data)
      av_packet_unref(&pkt);

    if (quit) {
      return -1;
    }

    if (packet_queue_get(&audioq, &pkt, 1) < 0) {
      return -1;
    }
    audio_pkt_data = pkt.data;
    audio_pkt_size = pkt.size;
  }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {
  AVCodecContext *pACtx = (AVCodecContext *)userdata;
  int len1, audio_size;

  static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
  static unsigned int audio_buf_size = 0;
  static unsigned int audio_buf_index = 0;

  while (len > 1) {
    if (audio_buf_index >= audio_buf_size) {
      /* We have already sent all our data; get more */
      audio_size = audio_decode_frame(pACtx, audio_buf, sizeof(audio_buf));
      if (audio_size < 0) {
	      /* If error, output silence */
	      audio_buf_size = 1024;
	      memset(audio_buf, 0, audio_buf_size);
      } else {
	      audio_buf_size = audio_size;
      }
      audio_buf_index = 0;
    }
    len1 = audio_buf_size - audio_buf_index;
    if (len1 > len)
      len1 = len;
    memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
    len -= len1;
    stream += len1;
    audio_buf_index += len1;
  }
}

int main(int argc, char *argv[])
{
  // const char *szFilePath = "rtsp://192.168.0.9/test.mp4";
  const char *szFilePath = "hist.mp4";

  AVFormatContext *pFmtCtx = NULL;

  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);

  // initialize libavformat and register all the muxers, demuxers and protocols.
  // avdevice_register_all();

  // do global initialization of network components.
  // avformat_network_init();

  // open an input stream and read the header.
  avformat_open_input(&pFmtCtx, szFilePath, NULL, NULL);

  // read packtes of a media file to get stream information.
  avformat_find_stream_info(pFmtCtx, NULL);
  
  // find video stream
  int nVSI = av_find_best_stream(pFmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  int nASI = av_find_best_stream(pFmtCtx, AVMEDIA_TYPE_AUDIO, -1, nVSI, NULL, 0);
  
  // find decoder
  AVCodec *pVideoCodec = avcodec_find_decoder(pFmtCtx->streams[nVSI]->codecpar->codec_id);
  AVCodec *pAudioCodec = avcodec_find_decoder(pFmtCtx->streams[nASI]->codecpar->codec_id);
  
  // default initialization
  AVCodecContext *pVCtx = avcodec_alloc_context3(pVideoCodec);
  AVCodecContext *pACtx = avcodec_alloc_context3(pAudioCodec);

  // Fill the codec context based on the values from the supplied codec parameters
  avcodec_parameters_to_context(pVCtx, pFmtCtx->streams[nVSI]->codecpar);
  avcodec_parameters_to_context(pACtx, pFmtCtx->streams[nASI]->codecpar);

  // initialize codec context as decoder
  avcodec_open2(pVCtx, pVideoCodec, NULL);
  avcodec_open2(pACtx, pAudioCodec, NULL);

  swrCtx = swr_alloc();
	av_opt_set_channel_layout(swrCtx, "in_channel_layout", pACtx->channel_layout, 0);
	av_opt_set_channel_layout(swrCtx, "out_channel_layout", pACtx->channel_layout, 0);
	av_opt_set_int(swrCtx, "in_sample_rate", pACtx->sample_rate, 0);
	av_opt_set_int(swrCtx, "out_sample_rate", pACtx->sample_rate, 0);
	av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", pACtx->sample_fmt, 0);
	av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
	swr_init(swrCtx);

  packet_queue_init(&audioq);

  SDL_AudioSpec wanted_spec, spec;
  wanted_spec.freq = pACtx->sample_rate;
  wanted_spec.format = AUDIO_S16SYS;    // Signed 16bit endian:SYS
  wanted_spec.channels = pACtx->channels;
  wanted_spec.silence = 0;
  wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
  wanted_spec.callback = audio_callback;
  wanted_spec.userdata = pACtx;

  // open the audio device
  SDL_OpenAudio(&wanted_spec, &spec);
  // starts the audio device. it plays silence if it doesn't get data.
  SDL_PauseAudio(0);
  
  SDL_Window *window = SDL_CreateWindow("SDL_CreateTexture", 
                          SDL_WINDOWPOS_UNDEFINED, 
                          SDL_WINDOWPOS_UNDEFINED, 
                          pFmtCtx->streams[nVSI]->codecpar->width, 
                          pFmtCtx->streams[nVSI]->codecpar->height, 
                          SDL_WINDOW_RESIZABLE);
  SDL_Rect r;
  r.x = 0;
  r.y = 0;
  r.w = pFmtCtx->streams[nVSI]->codecpar->width;
  r.h = pFmtCtx->streams[nVSI]->codecpar->height;
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
  SDL_Texture *texture = SDL_CreateTexture(renderer, 
                            SDL_PIXELFORMAT_YV12, 
                            SDL_TEXTUREACCESS_STREAMING,
                            pFmtCtx->streams[nVSI]->codecpar->width, 
                            pFmtCtx->streams[nVSI]->codecpar->height);
  
  struct SwsContext *sws_ctx = sws_getContext(pFmtCtx->streams[nVSI]->codecpar->width, 
                                  pFmtCtx->streams[nVSI]->codecpar->height,
                                  pVCtx->pix_fmt,
                                  pFmtCtx->streams[nVSI]->codecpar->width, 
                                  pFmtCtx->streams[nVSI]->codecpar->height,
                                  AV_PIX_FMT_YUV420P,
                                  SWS_BILINEAR,
                                  NULL,
                                  NULL,
                                  NULL);

  AVPacket *pkt = av_packet_alloc();
  av_init_packet(pkt);
  AVFrame *pVFrame = av_frame_alloc();
  AVFrame *pAFrame = av_frame_alloc();
  AVFrame *pict = av_frame_alloc();

  int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, 
                    pFmtCtx->streams[nVSI]->codecpar->width, 
                    pFmtCtx->streams[nVSI]->codecpar->height,
                    1);
  
  uint8_t *buffer = (uint8_t *)av_malloc(num_bytes * sizeof(uint8_t));
  
  av_image_fill_arrays(pict->data, pict->linesize, buffer, 
      AV_PIX_FMT_YUV420P, 
      pFmtCtx->streams[nVSI]->codecpar->width, 
      pFmtCtx->streams[nVSI]->codecpar->height, 
      1);

  SDL_Event event;
  while (1) {
    SDL_PollEvent(&event);
    if (event.type == SDL_QUIT) {
      quit = 1;
      break;
    }
    if (av_read_frame(pFmtCtx, pkt) < 0) {
      continue;
    } else {
      if (pkt->stream_index == nVSI) {
        if (avcodec_send_packet(pVCtx, pkt) >= 0) {
          if (avcodec_receive_frame(pVCtx, pVFrame) >= 0) {
            sws_scale(sws_ctx, 
              (uint8_t const * const *)pVFrame->data,
              pVFrame->linesize, 
              0, 
              pFmtCtx->streams[nVSI]->codecpar->height,
              pict->data,
              pict->linesize);
            SDL_UpdateYUVTexture(texture, 
              &r, 
              pict->data[0], 
              pict->linesize[0],
              pict->data[1], 
              pict->linesize[1], 
              pict->data[2], 
              pict->linesize[2]);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
          }
        }
      } else if (pkt->stream_index == nASI) {
        packet_queue_put(&audioq, pkt);
        // if (avcodec_send_packet(pACtx, pkt) >= 0) {
        //   if (avcodec_receive_frame(pACtx, pAFrame) >= 0) {
            
        //   }
        // }
      } else {
        av_packet_unref(pkt);
      }
    }
  }
  
  SDL_DestroyRenderer(renderer);
  SDL_Quit();

  av_free(buffer);

  av_frame_free(&pVFrame);
  av_frame_free(&pAFrame);
  av_frame_free(&pict);

  av_packet_free(&pkt);

  avcodec_free_context(&pVCtx);
  avcodec_free_context(&pACtx);
  
  // close an opened input AVFormatContext.
  avformat_close_input(&pFmtCtx);
  
  // undo the initialization done by avformat_network_init.
  // avformat_network_deinit();

  return 0;
}
