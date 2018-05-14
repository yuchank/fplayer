extern "C" {
  #include <libavformat/avformat.h>
  #include <libavutil/avutil.h>
  #include <libavutil/avstring.h>
}

#include <SDL2/SDL.h>

typedef struct _VideoState {
  AVFormatContext *pFormatCtx;

  SDL_Thread *parse_tid;

  char filename[1024];
} VideoState;

VideoState *global_video_state;

int stream_component_open(VideoState *is, int stream_index) {

}

int decode_thread(void *arg) {
  VideoState *is = static_cast<VideoState *>(arg);
  AVFormatContext *pFormatCtx;
  global_video_state = is;

  avformat_open_input(&pFormatCtx, is->filename, NULL, NULL);
  is->pFormatCtx = pFormatCtx;

  avformat_find_stream_info(pFormatCtx, NULL);
  av_dump_format(pFormatCtx, 0, is->filename, 0);

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
  if (is) {
    av_free(is);
  }
  return 0;
}
