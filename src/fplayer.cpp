extern "C" {
  #include <libavformat/avformat.h>
}

#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")

int main(void)
{
  const char *szFilePath = "OneLINE.avi";
  int ret;

  av_register_all();
  avformat_network_init();

  AVFormatContext *pFmtCtx = NULL;

  ret = avformat_open_input(&pFmtCtx, szFilePath, NULL, NULL);
  if (ret != 0) {
    av_log(NULL, AV_LOG_ERROR, "File [%s] Open Fail (ret: %d)\n", szFilePath, ret);
    exit(-1);
  }
  av_log(NULL, AV_LOG_INFO, "File [%s] Open Success\n", szFilePath);
  av_log(NULL, AV_LOG_INFO, "Format: %s\n", pFmtCtx->iformat->name);

  ret = avformat_find_stream_info(pFmtCtx, NULL);
  if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Fail to get Stream Information\n");
		exit(-1);
	}
	av_log(NULL, AV_LOG_INFO, "Get Stream Information Success\n");

  avformat_close_input(&pFmtCtx);
  avformat_network_deinit();
  return 0;
}