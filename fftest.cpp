#include <string>

#include <iostream>
#include <windows.h>

#include <cstdio>
#include <thread>
#include <fstream>
#include <vector>
#include <algorithm>
#include "h264-bitstream/h264_stream.h"
#include "H264_Decoder.h"

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

void render(AVFrame *pFrame, AVPacket *pkt, void *user, AVCodecContext *pCodecCtx);
void sdlInit(AVCodecContext *pCodecCtx);

int *char_to_pointer(std::string input)
{
  return (int *) std::stoul(input, nullptr, 16);
}

#ifdef __cplusplus
extern "C"
#endif
int main(int argc, char *argv[])
{

  std::ifstream infile(
      "C:\\Users\\aelmod\\CLionProjects\\RPiPlay\\cmake-build-release\\pointers.txt"); //TODO: gowno ebanoe

  std::string sizePtrStr;
  std::string dataPtrStr;
  std::string dataTypePtrStr;
  std::string sharedPTSStr;
  std::getline(infile, sizePtrStr);
  std::getline(infile, dataPtrStr);
  std::getline(infile, dataTypePtrStr);
  std::getline(infile, sharedPTSStr);

  int *sizePtr = char_to_pointer(sizePtrStr);
  int *dataPtr = char_to_pointer(dataPtrStr);
  int *dataTypePtr = char_to_pointer(dataTypePtrStr);
  int *PTSPtr = char_to_pointer(sharedPTSStr);

  bool b = true;
  const std::string entryName = "rpiplay.exe";

  auto *pDecoder = new H264_Decoder(render, nullptr);

  HWND hWnd = FindWindow(0, TEXT("C:\\Users\\aelmod\\CLionProjects\\RPiPlay\\cmake-build-release\\rpiplay.exe"));
  if (hWnd == 0) {
    std::cerr << "Cannot find window." << std::endl;
  } else {
    DWORD pId;
    GetWindowThreadProcessId(hWnd, &pId);
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pId);

    if (hProc) {
      int size = -1;
      auto *data = (uint8_t *) malloc(200000);
      int frameType;
      uint64_t PTS;

      SIZE_T size_bytes_read = 0, data_bytes_read = 0, frame_type_bytes_read = 0, PTS_bytes_read = 0;

      h264_stream_t *h = h264_new();

      if (!h) std::cerr << "PEZDA!" << std::endl;

      pDecoder->load(60);

      bool secondFrame = true;

      int modifiedDataSize = 0;
      uint8_t *modified_data = nullptr;

      while (b) {
        if (ReadProcessMemory(hProc, (LPVOID) sizePtr, &size, 4, &size_bytes_read)
            || GetLastError() == ERROR_PARTIAL_COPY) {
          if (size_bytes_read == 0) std::cerr << "Cannot read size_bytes_read" << std::endl;
        }

        if (size > 0) {
          if (ReadProcessMemory(hProc, (LPVOID) dataPtr, data, size, &data_bytes_read)
              || GetLastError() == ERROR_PARTIAL_COPY) {
            if (data_bytes_read == 0) std::cerr << "Cannot read data_bytes_read" << std::endl;
          }
        }

        if (ReadProcessMemory(hProc, (LPVOID) dataTypePtr, &frameType, 4, &frame_type_bytes_read)
            || GetLastError() == ERROR_PARTIAL_COPY) {
          if (frame_type_bytes_read == 0) std::cerr << "Cannot read frame_type_bytes_read" << std::endl;
        }

        if (ReadProcessMemory(hProc, (LPVOID) PTSPtr, &PTS, sizeof(uint64_t), &PTS_bytes_read)
            || GetLastError() == ERROR_PARTIAL_COPY) {
          if (PTS_bytes_read == 0) std::cerr << "Cannot read PTS_bytes_read" << std::endl;
        }
        if (frameType == -1) continue;

        if (size > 0) {
          if (frameType == 0) {
            modified_data = new uint8_t[size * 2];

            int sps_start, sps_end;

            int sps_size = find_nal_unit(data, size, &sps_start, &sps_end);
            int pps_size = size - 8 - sps_size;
            if (sps_size > 0) {
              read_nal_unit(h, &data[sps_start], sps_size);
              h->sps->vui.bitstream_restriction_flag = 1;
              h->sps->vui.max_dec_frame_buffering = 4; // It seems this is the lowest value that works for iOS and macOS

              // Write the modified SPS NAL
              int new_sps_size = write_nal_unit(h, modified_data + 3, size * 2) - 1;
              modified_data[0] = 0;
              modified_data[1] = 0;
              modified_data[2] = 0;
              modified_data[3] = 1;

              // Copy the original PPS NAL
              memcpy(modified_data + new_sps_size + 4, data + 4 + sps_size, pps_size + 4);

              size = new_sps_size + pps_size + 8;
              modifiedDataSize = size;
              memcpy(modified_data, data, size);
              continue;
            }
          }
        }

        if (data && size > 0) {
          if (secondFrame) {
            int tmpSize = modifiedDataSize + size;

            auto *combined = new unsigned char[tmpSize];

            memcpy(combined, modified_data, modifiedDataSize);
            memcpy(combined + modifiedDataSize, data, size);

            bool first_frame = true;

            pDecoder->readFrame(combined, tmpSize, first_frame);
            sdlInit(pDecoder->codec_context);

            secondFrame = false;
            first_frame = false;

            continue;
          }

          bool tmp = false;
          pDecoder->readFrame(data, size, tmp);
//            int tmpSize = modifiedDataSize + size;
//
//            auto *combined = new unsigned char[tmpSize];
//
//            memcpy(combined, modified_data, modifiedDataSize);
//            memcpy(combined + modifiedDataSize, data, size);

//          pDecoder->readFrame(combined, tmpSize, tmp);
//
//          free(combined);

          Sleep(17);
        }
      }
    } else {
      std::cerr << "Couldn't open process " << pId << ": " << GetLastError() << std::endl;
    }

    std::getchar();

    b = false;
  }
  return 0;
}

AVFrame *pFrameYUV;
SDL_Rect sdlRect;
struct SwsContext *sws_ctx;
SDL_Renderer *sdlRenderer;
SDL_Texture *sdlTexture;

void sdlInit(AVCodecContext *pCodecCtx)
{
  //Source color format
  AVPixelFormat src_fix_fmt = pCodecCtx->pix_fmt; //AV_PIX_FMT_YUV420P
  //Objective color format
  AVPixelFormat dst_fix_fmt = AV_PIX_FMT_BGR24;
  // Allocate video frame
//  AVFrame *pFrame = av_frame_alloc();
  pFrameYUV = av_frame_alloc();
  if (pFrameYUV == nullptr) {
    printf("pFrameYUV == NULL");
    return;
  }

//  pCodecCtx->width = 828;
//  pCodecCtx->height = 1792;

  sws_ctx = sws_getContext(
      pCodecCtx->width,
      pCodecCtx->height,
      pCodecCtx->pix_fmt,
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

  sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC |
                                                  SDL_RENDERER_TARGETTEXTURE);
  sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_BGR24, SDL_TEXTUREACCESS_STATIC,
                                 pCodecCtx->width, pCodecCtx->height);
  if (!sdlTexture) {
    printf("!sdlTexture");
    return;
  }

  SDL_SetTextureBlendMode(sdlTexture, SDL_BLENDMODE_BLEND);
}

void render(AVFrame *pFrame, AVPacket *pkt, void *user, AVCodecContext *pCodecCtx)
{
  SDL_Event event;

  //render
  sws_scale(sws_ctx,
            (uint8_t const *const *) pFrame->data,
            pFrame->linesize,
            0,
            pCodecCtx->height,
            pFrameYUV->data,
            pFrameYUV->linesize);

  SDL_UpdateTexture(sdlTexture, &sdlRect, pFrameYUV->data[0], pFrameYUV->linesize[0]);
  SDL_RenderClear(sdlRenderer);
  SDL_RenderCopy(sdlRenderer, sdlTexture, &sdlRect, &sdlRect);
  SDL_RenderPresent(sdlRenderer);
  SDL_PollEvent(&event);

//    av_free_packet(&pkt);
//  SDL_DestroyTexture(sdlTexture);
}
