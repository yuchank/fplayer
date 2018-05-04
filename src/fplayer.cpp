extern "C" {
  #include <libavformat/avformat.h>
}

#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")

#include <iostream>

const char *AVMediaType2Str(AVMediaType type);
const char *AVCodecID2Str(AVCodecID type);

using namespace std;

int main(void)
{
  const char *szFilePath = "sample.mp4";
  int ret;

  // initialize libavformat and register all the muxers, demuxers and protocols.
  av_register_all();
  // do global initialization of network components.
  avformat_network_init();

  AVFormatContext *pFmtCtx = NULL;

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
    const char *szType = AVMediaType2Str(pStream->codec->codec_type);
    const char *szCodecName = AVCodecID2Str(pStream->codec->codec_id);
    
    av_log(NULL, AV_LOG_INFO, "    > Stream[%d]: %s: %s ", i, szType, szCodecName);
    if (pStream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			av_log(NULL, AV_LOG_INFO, "%dx%d (%.2f fps)", pStream->codec->width, pStream->codec->height, av_q2d(pStream->r_frame_rate));
		}
		else if (pStream->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
			av_log(NULL, AV_LOG_INFO, "%d Hz", pStream->codec->sample_rate);
		}
		av_log(NULL, AV_LOG_INFO, "\n");
  }

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