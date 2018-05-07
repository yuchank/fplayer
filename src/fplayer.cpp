extern "C" {
  #include <libavformat/avformat.h>
  #include <libavdevice/avdevice.h>
}

#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avdevice.lib")

#include <iostream>

const char *AVMediaType2Str(AVMediaType type);
const char *AVCodecID2Str(AVCodecID type);

using namespace std;

int main(void)
{
  const char *szFilePath = "rtsp://192.168.0.9/test.mp4";
  int ret;

  AVFormatContext *pFmtCtx = NULL;

  // initialize libavformat and register all the muxers, demuxers and protocols.
  // av_register_all();     // deprecated
  avdevice_register_all();
  // do global initialization of network components.
  avformat_network_init();

  // open an input stream and read the header.
  ret = avformat_open_input(&pFmtCtx, szFilePath, NULL, NULL);
  if (ret != 0) {
    av_log(NULL, AV_LOG_ERROR, "File [%s] Open Fail (ret: %d)\n", szFilePath, ret);
    exit(-1);
  }
  av_log(NULL, AV_LOG_INFO, "File [%s] Open Success\n", szFilePath);
  av_log(NULL, AV_LOG_INFO, "Format: %s\n", pFmtCtx->iformat->name);

  // read packtes of a media file to get stream information.
  ret = avformat_find_stream_info(pFmtCtx, NULL);
  if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Fail to get Stream Information\n");
		exit(-1);
	}
	av_log(NULL, AV_LOG_INFO, "Get Stream Information Success\n");

  // get stream duration. (usec)
  if (pFmtCtx->duration > 0) {
    int tns, thh, tmm, tss;
    tns = pFmtCtx->duration / 1000000LL;  // long long int (8 Bytes)
    thh = tns / 3600;
    tmm = (tns % 3600) / 60;
    tss = (tns % 60);

    if (tns > 0) {
      av_log(NULL, AV_LOG_INFO, "Duration: %2d:%02d:%02d\n", thh, tmm, tss);
    }
  }
  av_log(NULL, AV_LOG_INFO, "Number of Stream: %d\n", pFmtCtx->nb_streams);

  // stream information
  for (int i = 0; i < pFmtCtx->nb_streams; i++) {
    AVStream *pStream = pFmtCtx->streams[i];
    // const char *szType = AVMediaType2Str(pStream->codec->codec_type);
    // const char *szCodecName = AVCodecID2Str(pStream->codec->codec_id);
    const char *szType = AVMediaType2Str(pStream->codecpar->codec_type);
    const char *szCodecName = AVCodecID2Str(pStream->codecpar->codec_id);
    
    av_log(NULL, AV_LOG_INFO, "    > Stream[%d]: %s: %s ", i, szType, szCodecName);
    // if (pStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
    if (pStream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			// av_log(NULL, AV_LOG_INFO, "%dx%d (%.2f fps)", pStream->codec->width, pStream->codec->height, av_q2d(pStream->r_frame_rate));
			av_log(NULL, AV_LOG_INFO, "%dx%d (%.2f fps)", pStream->codecpar->width, pStream->codecpar->height, av_q2d(pStream->r_frame_rate));
		}
		// else if (pStream->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
		else if (pStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			// av_log(NULL, AV_LOG_INFO, "%d Hz", pStream->codec->sample_rate);
			av_log(NULL, AV_LOG_INFO, "%d Hz", pStream->codecpar->sample_rate);
		}
		av_log(NULL, AV_LOG_INFO, "\n");
  }

  // find video stream
  int nVSI = -1;
  int nASI = -1;

  // method 1
  for (int i = 0; i < pFmtCtx->nb_streams; i++) {
    // if (nVSI < 0 && pFmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
    if (nVSI < 0 && pFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      nVSI = i;
    }
    // else if (nASI < 0 && pFmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
    else if (nASI < 0 && pFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      nASI = i;
    }
  }

  // method 2
  // nVSI = av_find_best_stream(pFmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  // nASI = av_find_best_stream(pFmtCtx, AVMEDIA_TYPE_AUDIO, -1, nVSI, NULL, 0);

  if (nVSI < 0 && nASI < 0) {
    av_log(NULL, AV_LOG_ERROR, "No Video & Audio Streams were found");
    exit(-1);
  }

  // AVCodecContext *pVCtx = pFmtCtx->streams[nVSI]->codec;
  // AVCodecContext *pACtx = pFmtCtx->streams[nASI]->codec;

  // find video decoder
  // AVCodec *pViedoCodec = avcodec_find_decoder(pFmtCtx->streams[nVSI]->codec->codec_id);
  AVCodec *pViedoCodec = avcodec_find_decoder(pFmtCtx->streams[nVSI]->codecpar->codec_id);
  if (pViedoCodec == NULL) {
    av_log(NULL, AV_LOG_ERROR, "No Video decoder was found");
    exit(-1);
  }

  AVCodecContext *pVCtx = avcodec_alloc_context3(pViedoCodec);

  // initialize codec context as decoder
  // if (avcodec_open2(pFmtCtx->streams[nVSI]->codec, pViedoCodec, NULL) < 0) {
  if (avcodec_open2(pVCtx, pViedoCodec, NULL) < 0) {
    av_log(NULL, AV_LOG_ERROR, "Failed to initialize video decoder");
    exit(-1);
  }

  // find video decoder
  // AVCodec *pAudioCodec = avcodec_find_decoder(pFmtCtx->streams[nASI]->codec->codec_id);
  AVCodec *pAudioCodec = avcodec_find_decoder(pFmtCtx->streams[nASI]->codecpar->codec_id);
  if (pAudioCodec == NULL) {
    av_log(NULL, AV_LOG_ERROR, "No Audio decoder was found");
    exit(-1);
  }
  
  AVCodecContext *pACtx = avcodec_alloc_context3(pAudioCodec);

  // initialize codec context as decoder
  // if (avcodec_open2(pFmtCtx->streams[nASI]->codec, pAudioCodec, NULL) < 0) {
  if (avcodec_open2(pACtx, pAudioCodec, NULL) < 0) {
    av_log(NULL, AV_LOG_ERROR, "Failed to initialize audio decoder");
    exit(-1);
  }

  AVPacket pkt;
  AVFrame *pVFrame, *pAFrame;
  int bGotPicture = 0;  // flag for video decoding
  int bGotSound = 0;    // flag for audio decoding

  int bPrint = 0; 

  pVFrame = av_frame_alloc();
  pAFrame = av_frame_alloc();

  while (av_read_frame(pFmtCtx, &pkt) >= 0) {
    // decoding
    if (pkt.stream_index == nVSI) {
      // if (avcodec_decode_video2(pVCtx, pVFrame, &bGotPicture, &pkt) >= 0) {
      if (avcodec_receive_frame(pVCtx, pVFrame) >= 0) {
        if (bGotPicture) {
          // ready to render image
          av_log(NULL, AV_LOG_INFO, "Got Picture\n");
          // if (!bPrint) {
          //   write_ascii_frame("output.txt", pVFrame);
          //   bPrint = 1;
          // }
        }
      }
      else {
        av_log(NULL, AV_LOG_ERROR, "video decoding error\n");
      }
    }
    else if (pkt.stream_index == nASI) {
      // if (avcodec_decode_audio4(pACtx, pAFrame, &bGotSound, &pkt) >= 0) {
      if (avcodec_receive_frame(pACtx, pAFrame) >= 0) {
        if (bGotSound) {
          // ready to render sound
          av_log(NULL, AV_LOG_INFO, "Got Picture\n");
        }
      }
      else {
        av_log(NULL, AV_LOG_ERROR, "audio decoding error\n");
      }
    }
    
    // free the packet that was allocated by av_read_frame
    av_packet_unref(&pkt);
  }

  av_free(pVFrame);
  av_free(pAFrame);

  // close an opened input AVFormatContext.
  avformat_close_input(&pFmtCtx);
  // undo the initialization done by avformat_network_init.
  avformat_network_deinit();

  return 0;
}

const char *AVMediaType2Str(AVMediaType type)
{
  switch (type) {
    case AVMEDIA_TYPE_VIDEO:
      return "Video";
    case AVMEDIA_TYPE_AUDIO:
      return "Audio";
    case AVMEDIA_TYPE_SUBTITLE:
      return "Subtitle";
    case AVMEDIA_TYPE_ATTACHMENT:
      return "Attachment";    
  }
  return "Unknown";
}

const char *AVCodecID2Str(AVCodecID id)
{
	string str = to_string(id);
  const char *c = str.c_str();
	return c;
}

void write_ascii_frame(const char *szFileName, const AVFrame *frame)
{
  uint8_t *p0, *p;
	const char arrAsciis[] = " .-+#";

	FILE* fp = fopen(szFileName, "w");
	if (fp) {
		/* Trivial ASCII grayscale display. */
		p0 = frame->data[0];		
		for (int y = 0; y < frame->height; y++) {
			p = p0;
			for (int x = 0; x < frame->width; x++) {
				putc( arrAsciis[*(p++) / 52], fp );
      }
			putc( '\n', fp );
			p0 += frame->linesize[0];
		}
		fflush(fp);
		fclose(fp);
	}
}