extern "C" {
  #include <libavformat/avformat.h>
  #include <libavcodec/avcodec.h>
}

int main(void)
{
  // const char *szFilePath = "rtsp://192.168.0.9/test.mp4";
  const char *szFilePath = "sample.mp4";
  int ret;

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
  
  AVPacket *pkt = av_packet_alloc();
  AVFrame *pVFrame = av_frame_alloc();
  AVFrame *pAFrame = av_frame_alloc();
  
  int bGotPicture = 0;  // flag for video decoding
  int bGotSound = 0;    // flag for audio decoding

  while (av_read_frame(pFmtCtx, pkt) >= 0) {
    if (pkt->stream_index == nVSI) {
      if (avcodec_send_packet(pVCtx, pkt) >= 0) {
        if (avcodec_receive_frame(pVCtx, pVFrame) >= 0) {
          av_log(NULL, AV_LOG_INFO, "Got Picture\n");
        }
      }
    } else if (pkt->stream_index == nASI) {
      if (avcodec_send_packet(pACtx, pkt) >= 0) {
        if (avcodec_receive_frame(pACtx, pAFrame) >= 0) {
          av_log(NULL, AV_LOG_INFO, "Got Sound\n");
        }
      }
    }
    av_packet_unref(pkt);
  }
  
  av_frame_free(&pVFrame);
  av_frame_free(&pAFrame);

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
