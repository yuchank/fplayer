extern "C" {
  #include <libavformat/avformat.h>
  #include <libavcodec/avcodec.h>
  #include <libswscale/swscale.h>
  #include <libavutil/imgutils.h>
}

#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

int decode(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *pkt);

int main(int argc, char *argv[])
{
  // const char *szFilePath = "rtsp://192.168.0.9/test.mp4";
  const char *szFilePath = "sample.mp4";

  AVFormatContext *pFmtCtx = NULL;

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
  
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
  
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
      break;
    }
    if (av_read_frame(pFmtCtx, pkt) < 0) {
      continue;
    } else {
      if (pkt) {
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
          if (avcodec_send_packet(pACtx, pkt) >= 0) {
            if (avcodec_receive_frame(pACtx, pAFrame) >= 0) {
              av_log(NULL, AV_LOG_INFO, "Got Sound\n");
            }
          }
        }
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

  avcodec_close(pVCtx);
  avcodec_close(pACtx);

  avcodec_free_context(&pVCtx);
  avcodec_free_context(&pACtx);
  
  // close an opened input AVFormatContext.
  avformat_close_input(&pFmtCtx);
  
  // undo the initialization done by avformat_network_init.
  // avformat_network_deinit();

  return 0;
}
