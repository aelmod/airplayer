#include <iostream>
#include <windows.h>

#include <cstdio>
#include <thread>

using namespace std;
extern "C"
{
// ffmpeg
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"

// SDL
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
};

void player(int *payload_size, unsigned char *payload);

int *trueSize;

#ifdef __cplusplus
extern "C"
#endif
int main(int argc, char *argv[])
{
  HWND hWnd = FindWindow(0, TEXT("C:\\Users\\aelmod\\CLionProjects\\RPiPlay\\cmake-build-release\\rpiplay.exe"));
  if (hWnd == 0) {
    std::cerr << "Cannot find window." << std::endl;
  } else {
    DWORD pId;
    GetWindowThreadProcessId(hWnd, &pId);
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pId);

    int size;
    unsigned char *data = (unsigned char *) malloc(200000);
    trueSize = &size;

    SIZE_T size_bytes_read = 0, data_bytes_read = 0;

    std::thread([&hProc, &size_bytes_read, &data_bytes_read, &size, &data, pId]() {
        if (hProc) {
          while (1) {
            if (ReadProcessMemory(hProc, (LPVOID) 0x7515a8, &size, 4, &size_bytes_read)
                || GetLastError() == ERROR_PARTIAL_COPY) {
              if (size_bytes_read == 0) std::cerr << "Cannot read size_bytes_read" << std::endl;
            }

            if (ReadProcessMemory(hProc, (LPVOID) 0x7d0048, data, size, &data_bytes_read)
                || GetLastError() == ERROR_PARTIAL_COPY) {
              if (data_bytes_read == 0) std::cerr << "Cannot read data_bytes_read" << std::endl;
            }
          }

        } else {
          std::cerr << "Couldn't open process " << pId << ": " << GetLastError() << std::endl;
        }
    }).detach();

    player(&size, data);

  }
  std::getchar();
}

int read_buffer(void *opaque, uint8_t *buf, int buf_size)
{
  int true_size;
  if (true_size = *trueSize) {
    return true_size;
  } else {
    return -1;
  }
}

void player(int *payload_size, unsigned char *payload)
{
//  const char *filename = argv[1];
  ///////////////////////////////////////////////////////////////////////////////
  // ffmpeg
  // Register all formats and codecs
  av_register_all();

  // Open video file
  AVPacket avpkt;
  int err, frame_decoded = 0;
  AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  AVCodecContext *pCodecCtx = avcodec_alloc_context3(codec);
  pCodecCtx->width = 828;
  pCodecCtx->height = 1792;
  avcodec_open2(pCodecCtx, codec, NULL);

  pCodecCtx->extradata_size = *payload_size;
//  av_malloc(pCodecCtx->extradata_size);
//  memcpy(pCodecCtx->extradata, payload, pCodecCtx->extradata_size);
  pCodecCtx->extradata = payload;

  AVFrame *frame;
  frame = av_frame_alloc();
  if (!frame) {
    printf("Could not allocate video frame\n");
  }

  int videoStream = 0;
  // Get a pointer to the codec context for the video stream
//  AVCodecContext *pCodecCtx = pFormatCtx->streams[videoStream]->codec;

  // Find the decoder for the video stream
  AVCodec *pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
  if (pCodec == NULL) {
    fprintf(stderr, "Unsupported codec!\n");
    return; // Codec not found
  }

  // Open codec
  AVDictionary *optionsDict = NULL;
  if (avcodec_open2(pCodecCtx, pCodec, &optionsDict) < 0) {
    return; // Could not open codec
  }

  //Source color format
  AVPixelFormat src_fix_fmt = pCodecCtx->pix_fmt; //AV_PIX_FMT_YUV420P
  //Objective color format
  AVPixelFormat dst_fix_fmt = AV_PIX_FMT_BGR24;
  // Allocate video frame
  AVFrame *pFrame = av_frame_alloc();
  AVFrame *pFrameYUV = av_frame_alloc();
  if (pFrameYUV == NULL) {
    return;
  }

  struct SwsContext *sws_ctx = sws_getContext(
          pCodecCtx->width,
          pCodecCtx->height,
          AV_PIX_FMT_YUV420P,
          pCodecCtx->width,
          pCodecCtx->height,
          dst_fix_fmt,
          SWS_BILINEAR,
          NULL,
          NULL,
          NULL);

  int numBytes = avpicture_get_size(dst_fix_fmt, pCodecCtx->width, pCodecCtx->height);
  uint8_t *buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));

  avpicture_fill((AVPicture *) pFrameYUV, buffer, dst_fix_fmt, pCodecCtx->width, pCodecCtx->height);

  // Read frames and save first five frames to disk
  SDL_Rect sdlRect;
  sdlRect.x = 0;
  sdlRect.y = 0;
  sdlRect.w = pCodecCtx->width;
  sdlRect.h = pCodecCtx->height;

  //////////////////////////////////////////////////////
  // SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
    exit(1);
  }

  SDL_Window *sdlWindow = SDL_CreateWindow("Video Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                           pCodecCtx->width, pCodecCtx->height, SDL_WINDOW_OPENGL);
  if (!sdlWindow) {
    fprintf(stderr, "SDL: could not set video mode - exiting\n");
    exit(1);
  }

  SDL_Renderer *sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC |
                                                                SDL_RENDERER_TARGETTEXTURE);
  SDL_Texture *sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_BGR24, SDL_TEXTUREACCESS_STATIC,
                                              pCodecCtx->width, pCodecCtx->height);
  if (!sdlTexture) {
    return;
  }
  SDL_SetTextureBlendMode(sdlTexture, SDL_BLENDMODE_BLEND);

  AVPacket packet;
  SDL_Event event;

  AVFormatContext *pFormatCtx = avformat_alloc_context();
  unsigned char *aviobuffer = (unsigned char *) av_malloc(32768);
  AVIOContext *avio = avio_alloc_context(aviobuffer, 32768, 0, NULL, read_buffer, NULL, NULL);
  pFormatCtx->pb = avio;

  if(avformat_open_input(&pFormatCtx,NULL,NULL,NULL)!=0){
    printf("Couldn't open input stream.\n");
    return;
  }

  while (av_read_frame(pFormatCtx, &packet) >= 0) {
    // Is this a packet from the video stream?
    if (packet.stream_index == videoStream) {
      // Decode video frame
      int frameFinished;
      avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

      // Did we get a video frame?
      if (frameFinished) {
        sws_scale(sws_ctx, (uint8_t const *const *) pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
                  pFrameYUV->data, pFrameYUV->linesize);

        SDL_UpdateTexture(sdlTexture, &sdlRect, pFrameYUV->data[0], pFrameYUV->linesize[0]);
        SDL_RenderClear(sdlRenderer);
        SDL_RenderCopy(sdlRenderer, sdlTexture, &sdlRect, &sdlRect);
        SDL_RenderPresent(sdlRenderer);
      }
      //SDL_Delay(50);
    }

    // Free the packet that was allocated by av_read_frame
    av_free_packet(&packet);
    SDL_PollEvent(&event);
    switch (event.type) {
      case SDL_QUIT:
        SDL_Quit();
        exit(0);
        break;
      default:
        break;
    }
  }

  SDL_DestroyTexture(sdlTexture);

  // Free the YUV frame
  av_free(pFrame);
  av_free(pFrameYUV);

  // Close the codec
  avcodec_close(pCodecCtx);

  // Close the video file
  avformat_close_input(&pFormatCtx);

//  return;
}

//int main()
//{
//  HWND hWnd = FindWindow(0, TEXT("C:\\Users\\aelmod\\CLionProjects\\RPiPlay\\cmake-build-release\\rpiplay.exe"));
//  if (hWnd == 0) {
//    std::cerr << "Cannot find window." << std::endl;
//  } else {
//    DWORD pId;
//    GetWindowThreadProcessId(hWnd, &pId);
//    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pId);
//
//    int size;
//    unsigned char *data = (unsigned char *) malloc(200000);
//
//    SIZE_T size_bytes_read = 0, data_bytes_read = 0;
//
//    if (hProc) {
//      while (1) {
//        if (ReadProcessMemory(hProc, (LPVOID) 0x7515a8, &size, 4, &size_bytes_read)
//            || GetLastError() == ERROR_PARTIAL_COPY) {
//          if (size_bytes_read == 0) std::cerr << "Cannot read size_bytes_read" << std::endl;
//        }
//
//        if (ReadProcessMemory(hProc, (LPVOID) 0x7d0048, data, size, &data_bytes_read)
//            || GetLastError() == ERROR_PARTIAL_COPY) {
//          if (data_bytes_read == 0) std::cerr << "Cannot read data_bytes_read" << std::endl;
//        }
//      }
//
//    } else {
//      std::cerr << "Couldn't open process " << pId << ": " << GetLastError() << std::endl;
//    }
//  }
//  std::getchar();
//}

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"

void render()
{
  unsigned char *payload_decrypted;
  int payload_size;

  if (payload_decrypted != NULL && payload_size > 0) {
    AVPacket avpkt;
    int err, frame_decoded = 0;
    AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    avcodec_open2(codecCtx, codec, NULL);

    codecCtx->extradata_size = payload_size;
//                av_malloc(codecCtx->extradata_size);
//                memcpy(codecCtx->extradata, payload_decrypted, codecCtx->extradata_size);
    codecCtx->extradata = payload_decrypted;

    AVFrame *frame;
    frame = av_frame_alloc();
    if (!frame) {
      printf("Could not allocate video frame\n");
    }

// Set avpkt data and size here
//                err = avcodec_decode_video2(codecCtx, frame, &frame_decoded, &avpkt);
    err = avcodec_send_packet(codecCtx, &avpkt);

    if (err < 0) {
      fprintf(stderr, "Error sending a packet for decoding\n");
    }


    printf("err %d\n", err);


//  AVCodec *codec;
//  AVCodecContext *codecCtx = NULL;
//  AVFrame *frame;
//  AVPacket avPacket;
//
//  printf("Packet 1\n");
//
////  av_packet_from_data(avPacket, payload_decrypted, payload_size);
//
//  printf("Packet 2\n");
//  av_init_packet(&avPacket);
//

//
//  av_new_packet(avPacket, payload_size);

//  memcpy(avPacket.data, payload_decrypted, payload_size);
//  avPacket.size = payload_size;

//  frame = av_frame_alloc();
//  if(!frame){
//    printf("Could not allocate video frame\n");
////    qDebug() << "Could not allocate video frame";
////                return;
//  }

//  int got_frame = 1;

//  int len = avcodec_send_packet(codecCtx, &avPacket);
//  printf("len %s\n", payload_decrypted);
//  int len = avcodec_decode_video2(codecCtx, frame, &got_frame, avPacket);
  }
}