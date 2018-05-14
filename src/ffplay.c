#include "libavformat/avformat.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

typedef struct VideoState {
  SDL_Thread *read_tid;
} VideoState;

/* options specified by the user */
static AVInputFormat *file_iformat;
static const char *input_filename;

/* this thread gets the stream from the disk or the network */
static int read_thread(void *arg)
{
  VideoState *is = arg;

  return 0;
}

static VideoState *stream_open(const char *filename, AVInputFormat *iformat)
{
  VideoState *is;
  is = av_mallocz(sizeof(VideoState));
  if (!is)
    return NULL;

  is->read_tid    = SDL_CreateThread(read_thread, "read_thread", is);

  return is;
}

int main(int argc, char **argv) 
{
  int flags;
  VideoState *is;

  if (!input_filename) {

  }
  
  flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
  if (SDL_Init (flags)) {
    av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
    av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
    exit(1);
  }
  
  is = stream_open(input_filename, file_iformat);

  /* never returns */

  return 0;
}