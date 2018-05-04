extern "C" {
  #include <libavformat/avformat.h>
}

#include <iostream>

#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")

using namespace std;

int main(void)
{
  av_register_all();
  av_log(NULL, AV_LOG_INFO, "Hello FFMpeg\n");
  return 0;
}