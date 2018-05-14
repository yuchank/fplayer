#define __STDC_CONSTANT_MACROS

#include <stdint.h>
#include <inttypes.h>
#include <windows.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavutil/imgutils.h>
#include <libavutil/avstring.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#define SDL_MAIN_HANDLED

#include <SDL.h>
#include <SDL_thread.h>
#include <SDL_syswm.h>
#include <SDL_render.h>
#include <SDL_audio.h>
#ifdef __cplusplus
}
#endif

#define FF_ALLOC_EVENT   (SDL_USEREVENT)
#define FF_REFRESH_EVENT (SDL_USEREVENT + 1)
#define FF_QUIT_EVENT (SDL_USEREVENT + 2)

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)

#define VIDEO_PICTURE_QUEUE_SIZE (1)

#define SDL_AUDIO_BUFFER_SIZE 1024

#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000

typedef struct _PacketQueue
{
	AVPacketList *first, *last;
	int nb_packets, size;
	CRITICAL_SECTION cs;
	CONDITION_VARIABLE cv;

} PacketQueue;

typedef struct _VideoPicture
{
	SDL_Texture * texture;
	int width, height;
	int allocated;
	double pts;
} VideoPicture;

typedef struct _VideoState
{
	AVFormatContext * pFormatCtx;
	AVCodecContext * audioCtx;
	AVCodecContext * videoCtx;
	struct SwrContext * pSwrCtx;
	struct SwsContext * pSwsCtx;
	int videoStream, audioStream;
	AVStream * audioSt;
	PacketQueue audioq;
	__declspec(align(16)) uint8_t audioBuf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
	unsigned int audioBufSize, audioBufIndex;
	AVPacket audioPkt;
	AVPacket videoPkt;
	int hasAudioFrames;
	AVFrame * pAudioFrame;
	__declspec(align(16)) uint8_t audioConvertedData[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
	AVFrame * pFrameRGB;
	uint8_t * pFrameBuffer;

	AVStream * videoSt;
	PacketQueue videoq;
	
	VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
	int pictqSize, pictqRindex, pictqWindex;
	CRITICAL_SECTION pictqCs;
	CONDITION_VARIABLE pictqCv;

	HANDLE hParseThread;
	HANDLE hVideoThread;

	SDL_Window * window;
	SDL_Renderer * renderer;

	char filename[1024];
	int quit;

	double videoClock;

} VideoState;

VideoState * global_video_state;

void PacketQueueInit(PacketQueue * pq)
{
	memset(pq, 0, sizeof(PacketQueue));
	InitializeCriticalSection(&pq->cs);
	InitializeConditionVariable(&pq->cv);
}

int PacketQueuePut(PacketQueue * pq, const AVPacket * srcPkt)
{
	AVPacketList *elt;
	AVPacket pkt;
	int rv;
	if (!pq) return -1;
	rv = av_packet_ref(&pkt, srcPkt);
	if (rv) return rv;
	elt = (AVPacketList*)av_malloc(sizeof(AVPacketList));
	if (!elt) return -1;
	elt->pkt = pkt;
	elt->next = NULL;

	EnterCriticalSection(&pq->cs);

	if (!pq->last)
		pq->first = elt;
	else
		pq->last->next = elt;
	pq->last = elt;
	pq->nb_packets++;
	pq->size += elt->pkt.size;
	WakeConditionVariable(&pq->cv);

	LeaveCriticalSection(&pq->cs);
	return 0;
}

int PacketQueueGet(PacketQueue *pq, AVPacket *pkt, int block)
{
	AVPacketList * elt;
	int rv;

	if (!pq || !pkt) return -1;

	EnterCriticalSection(&pq->cs);
	for (;;)
	{
		if (global_video_state->quit)
		{
			rv = -1;
			break;
		}

		elt = pq->first;
		if (elt)
		{
			pq->first = elt->next;
			if (!pq->first)
				pq->last = NULL;
			pq->nb_packets--;
			pq->size -= elt->pkt.size;
			*pkt = elt->pkt;
			av_free(elt);
			rv = 1;
			break;
		}
		else if (!block)
		{
			rv = 0;
			break;
		}
		else
		{
			SleepConditionVariableCS(&pq->cv, &pq->cs, INFINITE);
		}
	}
	LeaveCriticalSection(&pq->cs);
	return rv;
}

int DecodeAudioFrame(VideoState * is)
{
	int len2, dataSize = 0, outSize = 0, rv, hasPacket = 0;
	int64_t len1;
	uint8_t * converted = &is->audioConvertedData[0];

	for (;;)
	{
		while (is->hasAudioFrames) {

			rv = avcodec_receive_frame(is->audioCtx, is->pAudioFrame);
			if (rv)
			{
				is->hasAudioFrames = 0;
				break;
			}

			dataSize = av_samples_get_buffer_size(NULL,
												  is->audioCtx->channels,
												  is->pAudioFrame->nb_samples,
												  is->audioCtx->sample_fmt,
												  1);

			outSize = av_samples_get_buffer_size(NULL,
												 is->audioCtx->channels,
												 is->pAudioFrame->nb_samples,
												 AV_SAMPLE_FMT_FLT,
												 1);
			len2 = swr_convert(is->pSwrCtx,
							   &converted,
							   is->pAudioFrame->nb_samples,
							   (const uint8_t**)&is->pAudioFrame->data[0],
							   is->pAudioFrame->nb_samples);
			memcpy(is->audioBuf, converted, outSize);
			dataSize = outSize;

			/* We have data, return it and come back for more later */
			return dataSize;
		}

		if (hasPacket)
		{
			av_packet_unref(&is->audioPkt);
		}

		if (is->quit)
			return -1;

		if (PacketQueueGet(&is->audioq, &is->audioPkt, 1) < 0)
			return -1;

		hasPacket = 1;

		rv = avcodec_send_packet(is->audioCtx, &is->audioPkt);
		if (rv) return rv;

		is->hasAudioFrames = 1;
	}

	return -1;
}

void AudioCallback(void *userdata, uint8_t * stream, int len)
{
	VideoState * is = (VideoState*)userdata;
	int len1, audioSize;
	
	while (len > 0)
	{
		if (is->audioBufIndex >= is->audioBufSize)
		{
			// already sent all data; get more
			audioSize = DecodeAudioFrame(is);
			if (audioSize < 0)
			{
				// error
				is->audioBufSize = SDL_AUDIO_BUFFER_SIZE;
				memset(is->audioBuf, 0, sizeof(is->audioBuf));
			}
			else
			{
				is->audioBufSize = audioSize;
			}
			is->audioBufIndex = 0;
		}
		len1 = is->audioBufSize - is->audioBufIndex;
		if (len1 > len)
			len1 = len;
		memcpy(stream, (uint8_t *)is->audioBuf + is->audioBufIndex, len1);
		len -= len1;
		stream += len1;
		is->audioBufIndex += len1;
	}
}

void AllocPicture(void* userData)
{
	VideoState * is = (VideoState*)userData;
	VideoPicture * vp;

	vp = &is->pictq[is->pictqWindex];
	if (vp->texture)
	{
		SDL_DestroyTexture(vp->texture);
	}

	vp->texture = SDL_CreateTexture(is->renderer,
		SDL_PIXELFORMAT_RGB24,
		SDL_TEXTUREACCESS_STREAMING,
		is->videoCtx->width,
		is->videoCtx->height);
	vp->width = is->videoCtx->width;
	vp->height = is->videoCtx->height;

	EnterCriticalSection(&is->pictqCs);
	vp->allocated = 1;
	WakeConditionVariable(&is->pictqCv);
	LeaveCriticalSection(&is->pictqCs);
}

int QueuePicture(VideoState* is, AVFrame* frame, double pts)
{
	VideoPicture * vp;

	EnterCriticalSection(&is->pictqCs);
	while (is->pictqSize >= VIDEO_PICTURE_QUEUE_SIZE && !is->quit)
	{
		SleepConditionVariableCS(&is->pictqCv, &is->pictqCs, INFINITE);
	}
	LeaveCriticalSection(&is->pictqCs);

	if (is->quit)
		return -1;

	vp = &is->pictq[is->pictqWindex];

	if (!vp->texture || vp->width != is->videoCtx->width || vp->height != is->videoCtx->height)
	{
		SDL_Event evt;

		vp->allocated = 0;

		evt.type = FF_ALLOC_EVENT;
		evt.user.data1 = is;
		SDL_PushEvent(&evt);

		EnterCriticalSection(&is->pictqCs);
		while (!vp->allocated && !is->quit)
		{
			SleepConditionVariableCS(&is->pictqCv, &is->pictqCs, INFINITE);
		}
		LeaveCriticalSection(&is->pictqCs);

		if (is->quit)
			return -1;
	}

	if (vp->texture)
	{
		sws_scale(is->pSwsCtx,
				  frame->data,
				  frame->linesize,
				  0,
				  is->videoCtx->height,
			      is->pFrameRGB->data,
			      is->pFrameRGB->linesize);

		SDL_UpdateTexture(vp->texture, NULL, is->pFrameRGB->data[0], is->pFrameRGB->linesize[0]);

		vp->pts = pts;
		
		if (++is->pictqWindex == VIDEO_PICTURE_QUEUE_SIZE)
		{
			is->pictqWindex = 0;
		}

		EnterCriticalSection(&is->pictqCs);
		is->pictqSize++;
		LeaveCriticalSection(&is->pictqCs);
	}

	return 0;
}

double SyncVideo(VideoState * is, AVFrame * frame, double pts)
{
	double frameDelay;

	if (pts != 0)
	{
		is->videoClock = pts;
	}
	else
	{
		pts = is->videoClock;
	}

	frameDelay = av_q2d(is->videoSt->time_base);
	frameDelay += frame->repeat_pict * (frameDelay * 0.5);
	is->videoClock += frameDelay;
	return pts;
}

unsigned __stdcall VideoThread(void* pUserData)
{
	VideoState * is = (VideoState*)pUserData;
	int rv;
	AVFrame *pFrame = NULL;
	double pts;

	pFrame = av_frame_alloc();

	while (PacketQueueGet(&is->videoq, &is->videoPkt, 1) > 0)
	{
		rv = avcodec_send_packet(is->videoCtx, &is->videoPkt);
		if (rv < 0) continue;
		
		while (!avcodec_receive_frame(is->videoCtx, pFrame))
		{
			if (is->videoPkt.dts != AV_NOPTS_VALUE)
			{
				pts = av_frame_get_best_effort_timestamp(pFrame);
			}
			else 
			{
				pts = 0;
			}

			pts *= av_q2d(is->videoSt->time_base);

			pts = SyncVideo(is, pFrame, pts);
			if (QueuePicture(is, pFrame, pts) < 0)
			{
				break;
			}
		}

		//av_packet_unref(&is->videoPkt);
	}

	av_frame_free(&pFrame);
	return 0;
}

int StreamComponentOpen(VideoState * is, int streamIndex)
{
	AVFormatContext * pFormatCtx = is->pFormatCtx;
	AVCodecContext * codecCtx;
	AVCodec * codec;
	AVCodecParameters * codecPar;
	SDL_AudioSpec wantedSpec = { 0 }, audioSpec = {0};
	int rv, rgbFrameSize;

	if (streamIndex < 0 || streamIndex >= pFormatCtx->nb_streams)
		return -1;

	codecPar = pFormatCtx->streams[streamIndex]->codecpar;

	codec = avcodec_find_decoder(codecPar->codec_id);
	if (!codec) return -1;

	codecCtx = avcodec_alloc_context3(codec);
	if (!codecCtx) return -1;

	rv = avcodec_parameters_to_context(codecCtx, codecPar);
	if (rv < 0)
	{
		avcodec_free_context(&codecCtx);
		return rv;
	}

	rv = avcodec_open2(codecCtx, codec, NULL);
	if (rv < 0)
	{
		avcodec_free_context(&codecCtx);
		return rv;
	}

	if (codecPar->codec_type == AVMEDIA_TYPE_AUDIO)
	{
		is->audioCtx = codecCtx;
		is->audioStream = streamIndex;
		is->audioBufSize = 0;
		is->audioBufIndex = 0;
		is->audioSt = pFormatCtx->streams[streamIndex];
		memset(&is->audioPkt, 0, sizeof(is->audioPkt));
		is->pAudioFrame = av_frame_alloc();
		if (!is->pAudioFrame) return -1;

		is->pSwrCtx = swr_alloc();
		if (!is->pSwrCtx) return -1;
		
		av_opt_set_channel_layout(is->pSwrCtx, "in_channel_layout", codecCtx->channel_layout, 0);
		av_opt_set_channel_layout(is->pSwrCtx, "out_channel_layout", codecCtx->channel_layout, 0);
		av_opt_set_int(is->pSwrCtx, "in_sample_rate", codecCtx->sample_rate, 0);
		av_opt_set_int(is->pSwrCtx, "out_sample_rate", codecCtx->sample_rate, 0);
		av_opt_set_sample_fmt(is->pSwrCtx, "in_sample_fmt", codecCtx->sample_fmt, 0);
		av_opt_set_sample_fmt(is->pSwrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

		rv = swr_init(is->pSwrCtx);
		if (rv < 0) return rv;

		wantedSpec.channels = codecCtx->channels;
		wantedSpec.freq = codecCtx->sample_rate;
		wantedSpec.format = AUDIO_F32;
		wantedSpec.silence = 0;
		wantedSpec.samples = SDL_AUDIO_BUFFER_SIZE;
		wantedSpec.userdata = is;
		wantedSpec.callback = AudioCallback;

		if (SDL_OpenAudio(&wantedSpec, &audioSpec) < 0) {
			fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
			return -1;
		}

		PacketQueueInit(&is->audioq);

		SDL_PauseAudio(0);
	}
	else if (codecPar->codec_type == AVMEDIA_TYPE_VIDEO)
	{
		is->videoCtx = codecCtx;
		is->videoStream = streamIndex;
		is->videoSt = pFormatCtx->streams[streamIndex];

		is->pSwsCtx = sws_getContext(codecCtx->width,
									 codecCtx->height,
									 codecCtx->pix_fmt,
									 codecCtx->width,
									 codecCtx->height,
									 AV_PIX_FMT_RGB24,
								     SWS_BILINEAR,
									 NULL,
									 NULL,
									 NULL);
		if(!is->pSwsCtx) return -1;

		is->pFrameRGB = av_frame_alloc();
		if (!is->pFrameRGB) return -1;

		rgbFrameSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codecPar->width, codecPar->height, 8);
		if (rgbFrameSize < 1) return -1;

		is->pFrameBuffer = (uint8_t*)av_malloc(rgbFrameSize);
		if (!is->pFrameBuffer) return AVERROR(ENOMEM);

		rv = av_image_fill_arrays(&is->pFrameRGB->data[0],
								  &is->pFrameRGB->linesize[0],
								  is->pFrameBuffer,
								  AV_PIX_FMT_RGB24,
								  codecPar->width,
								  codecPar->height,
								  1);
		if (rv < 0) return rv;

		PacketQueueInit(&is->videoq);

		is->hVideoThread = (HANDLE)_beginthreadex(NULL, 0, VideoThread, is, 0, NULL);

		if (!is->hVideoThread) return -1;
	}
	else 
	{
		avcodec_free_context(&codecCtx);
		return -1;
	}

	return 0;
}


unsigned __stdcall DecodeThread(void* pUserData)
{
	VideoState * is = (VideoState*)pUserData;
	AVPacket pkt;
	int rv;

	for (;;)
	{
		if (is->quit) break;

		if (is->videoq.size >= MAX_QUEUE_SIZE || is->audioq.size >= MAX_QUEUE_SIZE)
		{
			Sleep(10);
			continue;
		}

		rv = av_read_frame(is->pFormatCtx, &pkt);
		if (rv < 0) break;

		if (pkt.stream_index == is->audioStream)
		{
			PacketQueuePut(&is->audioq, &pkt);
		}
		else if (pkt.stream_index == is->videoStream)
		{
			PacketQueuePut(&is->videoq, &pkt);
		}

		av_packet_unref(&pkt);

	}

	while (!is->quit)
	{
		Sleep(100);
	}

fail:
	if (1)
	{
		SDL_Event evt;
		evt.type = FF_QUIT_EVENT;
		evt.user.data1 = is;
		SDL_PushEvent(&evt);
	}
	return 0;
}

Uint32 TimerRefreshCallback(Uint32 interval, void* param)
{
	SDL_Event evt;
	evt.type = FF_REFRESH_EVENT;
	evt.user.data1 = param;
	SDL_PushEvent(&evt);
	return 0;
}

void ScheduleRefresh(VideoState* is, int delay)
{
	SDL_AddTimer(delay, TimerRefreshCallback, is);
}

void DisplayVideo(VideoState* is)
{
	VideoPicture * vp;
	SDL_Rect rect;
	double aspectRatio;
	int w, h, x, y, windowW, windowH;

	vp = &is->pictq[is->pictqRindex];

	if (vp->texture)
	{
		if (is->videoCtx->sample_aspect_ratio.num == 0)
		{
			aspectRatio = 0;
		}
		else
		{
			aspectRatio = av_q2d(is->videoCtx->sample_aspect_ratio) *
						  is->videoCtx->width /
						  is->videoCtx->height;
		}
		if (aspectRatio <= 0.0)
		{
			aspectRatio = is->videoCtx->width / (double)is->videoCtx->height;
		}
		SDL_GetWindowSize(is->window, &windowW, &windowH);
		h = windowH;
		w = ((int)rint(h * aspectRatio)) & -3;
		if (w > windowW) {
			w = windowW;
			h = ((int)rint(w / aspectRatio)) & -3;
		}
		x = (windowW - w) / 2;
		y = (windowH - h) / 2;

		rect.x = x;
		rect.y = y;
		rect.w = w;
		rect.h = h;

		SDL_RenderClear(is->renderer);
		SDL_RenderCopy(is->renderer, vp->texture, NULL, &rect);
		SDL_RenderPresent(is->renderer);
	}

}

void VideoRefreshTimer(void* userData)
{
	VideoState * is = (VideoState*)userData;
	VideoPicture * vp;

	if (is->videoCtx)
	{
		if (is->pictqSize == 0)
		{
			ScheduleRefresh(is, 10);
		}
		else
		{
			vp = &is->pictq[is->pictqRindex];
			// timing

			ScheduleRefresh(is, 30);

			DisplayVideo(is);

			if (++is->pictqRindex == VIDEO_PICTURE_QUEUE_SIZE) {
				is->pictqRindex = 0;
			}

			EnterCriticalSection(&is->pictqCs);
			is->pictqSize--;
			WakeConditionVariable(&is->pictqCv);
			LeaveCriticalSection(&is->pictqCs);
		}
	}
	else
	{
		ScheduleRefresh(is, 100);
	}
}

int DecodeInterruptCallback(void* userData)
{
	VideoState * is = (VideoState*)userData;
	return is && is->quit;
}

int main()
{
	int rv = 0, audioStream = -1, videoStream = -1;
	unsigned int s;
	char* filename = "file:video.mp4";
	char err[1024];
	SDL_Event evt;
	VideoState * is = NULL;
	SDL_Window * window;
	SDL_Renderer * renderer;

	av_register_all();
	avcodec_register_all();
	avdevice_register_all();
	avformat_network_init();

	if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER))
	{
		fprintf(stderr, "Unable to init SDL: %s \n", SDL_GetError());
		goto cleanup;
	}

	is = (VideoState*)av_mallocz(sizeof(VideoState));
	if (!is) goto cleanup;

	global_video_state = is;

	av_strlcpy(&is->filename[0], filename, sizeof(is->filename));
	
	is->audioStream = -1;
	is->videoStream = -1;
	
	is->pFormatCtx = avformat_alloc_context();
	if (!is->pFormatCtx) goto cleanup;

	is->pFormatCtx->interrupt_callback.callback = DecodeInterruptCallback;
	is->pFormatCtx->interrupt_callback.opaque = is;

	rv = avformat_open_input(&is->pFormatCtx, &is->filename[0], NULL, NULL);
	if (rv < 0) goto cleanup;

	rv = avformat_find_stream_info(is->pFormatCtx, NULL);
	if (rv < 0) goto cleanup;

	av_dump_format(is->pFormatCtx, 0, &is->filename[0], 0);

	for (s = 0; s < is->pFormatCtx->nb_streams; ++s)
	{
		av_dump_format(is->pFormatCtx, s, &is->filename[0], FALSE);
		if (is->pFormatCtx->streams[s]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStream < 0)
		{
			audioStream = s;
		}
		else if (is->pFormatCtx->streams[s]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0)
		{
			videoStream = s;
		}
	}
	if (audioStream < 0 && videoStream < 0)
	{
		rv = -1;
		goto cleanup;
	}

	window = SDL_CreateWindow(filename,
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		640,
		480,
		0);
	if (!window) goto cleanup;
	is->window = window;

	renderer = SDL_CreateRenderer(window, -1, 0);
	if (!renderer) goto cleanup;

	is->renderer = renderer;

	is->audioStream = audioStream;
	is->videoStream = videoStream;

	if (audioStream >= 0)
	{
		rv = StreamComponentOpen(is, audioStream);
		if (rv < 0) goto cleanup;
	}
	if (videoStream >= 0)
	{
		InitializeCriticalSection(&is->pictqCs);
		InitializeConditionVariable(&is->pictqCv);
		rv = StreamComponentOpen(is, videoStream);
		if (rv < 0) goto cleanup;
	}

	is->hParseThread = (HANDLE*)_beginthreadex(NULL, 0, DecodeThread, is, 0, NULL);
	if (!is->hParseThread) goto cleanup;

	ScheduleRefresh(is, 40);

	for (;;)
	{
		SDL_WaitEvent(&evt);
		switch (evt.type)
		{
		case FF_QUIT_EVENT:
		case SDL_QUIT:
			is->quit = 1;
			WakeConditionVariable(&is->audioq.cv);
			WakeConditionVariable(&is->videoq.cv);
			WakeConditionVariable(&is->pictqCv);
			SDL_Quit();
			goto cleanup;
		case FF_ALLOC_EVENT:
			AllocPicture(evt.user.data1);
			break;
		case FF_REFRESH_EVENT:
			VideoRefreshTimer(evt.user.data1);
			break;
		default:
			break;
		}
	}
cleanup:
	if (is->hVideoThread)
	{
		WaitForSingleObject(is->hVideoThread, INFINITE);
	}
	if (is->hParseThread)
	{
		WaitForSingleObject(is->hParseThread, INFINITE);
	}
	if (is->pAudioFrame)
	{
		av_frame_free(&is->pAudioFrame);
	}
	if (is->pFrameRGB)
	{
		av_frame_free(&is->pFrameRGB);
	}
	if (is->pFrameBuffer)
	{
		av_free(is->pFrameBuffer);
	}
	if (is->videoCtx)
	{
		avcodec_free_context(&is->videoCtx);
	}
	if (is->audioCtx)
	{
		avcodec_free_context(&is->audioCtx);
	}
	if (is->pSwrCtx)
	{
		swr_free(&is->pSwrCtx);
	}
	if (is->pSwsCtx)
	{
		sws_freeContext(is->pSwsCtx);
	}
	if (is->pFormatCtx)
	{
		avformat_close_input(&is->pFormatCtx);
	}
	if (is) av_free(is);
	avformat_network_deinit();
	return rv;
}