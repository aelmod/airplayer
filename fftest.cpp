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
#include "h264_data.h"
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <cstring>

using namespace boost::interprocess;

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

int *char_to_pointer(std::string input) {
  return (int *) std::stoul(input, nullptr, 16);
}

struct structure {
  int integer1;   //The compiler places this at offset 0 in the structure
  offset_ptr<int> ptr;        //The compiler places this at offset 4 in the structure
  int integer2;   //The compiler places this at offset 8 in the structure
};

#ifdef __cplusplus
extern "C"
#endif
int main(int argc, char *argv[]) {

  message_queue frames_queue
      (
          open_only,
          "frames_queue"
      );

  message_queue::size_type recvd_size;
  unsigned int priority;
  std::string serialized_string;
  serialized_string.resize(MAX_SIZE);

  h264_data frame_data;

  bool b = true;
  const std::string entryName = "rpiplay.exe";

  auto *pDecoder = new H264_Decoder(render, nullptr);

  int size = -1;
  auto *data = (uint8_t *) malloc(MAX_SIZE);
  int frameType;
  uint64_t PTS;

  SIZE_T size_bytes_read = 0, data_bytes_read = 0, frame_type_bytes_read = 0, PTS_bytes_read = 0;

  h264_stream_t *h = h264_new();

  if (!h) std::cerr << "PEZDA!" << std::endl;

  pDecoder->load(60);

  bool secondFrame = true;

  //shared_memory_object::remove("FirstFrameSharedMemory");
  managed_shared_memory segment;
  bool sharedMemoryNotExists = true;
  while (sharedMemoryNotExists) {
    try {
      managed_shared_memory segmentTry(open_only, "FirstFrameSharedMemory");
      segment = std::move(segmentTry);
    }
    catch (interprocess_exception &ex) {
      std::cout << "EXCEPTION: SHARED MEMORY DOES NOT EXISTS" << std::endl;
      sharedMemoryNotExists = true;
      continue;
    }
    sharedMemoryNotExists = false;
  }

  struct FirstFrame {
    unsigned char *data = nullptr;
    int data_len = 0;
  } firstFrame;

  while (0 == firstFrame.data_len) {
    std::cout << "Waiting for first frame data transfer..." << std::endl;
    if (0 != segment.find<std::pair<unsigned char *, int>>("FirstFrameDataPair").first) {
      firstFrame.data_len = segment.find<std::pair<unsigned char *, int>>("FirstFrameDataPair").first->second;
    }
  }

  data = firstFrame.data = segment.find<std::pair<unsigned char *, int>>("FirstFrameDataPair").first->first;
  size = firstFrame.data_len;
  std::cout << "First frame data transfer successful!" << std::endl;
  shared_memory_object::remove("FirstFrameSharedMemory");


//  boost::interprocess::shared_memory_object shm
//      (boost::interprocess::open_only, "FOO"              //name
//          , boost::interprocess::read_only
//      );
//
////  auto firstFrame = new FirstFrame();
//  boost::interprocess::mapped_region region(shm, boost::interprocess::read_only);
////  firstFrame = (FirstFrame*)region.get_address();
//
//  auto ttt = *(int*)region.get_address();
//  auto ttt2 = *((int*)region.get_address() + sizeof(int*));
//
//  auto keke = 1;




//  int *kek = res.first->first.get();
//  std::copy(res.first->first.begin(), res.first->first.end(), data);

  int modifiedDataSize = 0;
  auto *modified_data = new uint8_t[size * 2];

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
  }

  while (b) {

    frames_queue.receive(&serialized_string[0], MAX_SIZE, recvd_size, priority);
    std::cout << "Test";
    std::stringstream iss;
    iss << serialized_string;

    boost::archive::text_iarchive ia(iss);
    ia >> frame_data;

    size = frame_data.data_len;
    std::copy(frame_data.data.begin(), frame_data.data.end(), data);
    frameType = frame_data.type;
    PTS = frame_data.pts;


    if (frameType == -1) continue;

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


  std::getchar();

  b = false;

  return 0;
}

AVFrame *pFrameYUV;
SDL_Rect sdlRect;
struct SwsContext *sws_ctx;
SDL_Renderer *sdlRenderer;
SDL_Texture *sdlTexture;

void sdlInit(AVCodecContext *pCodecCtx) {
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

void render(AVFrame *pFrame, AVPacket *pkt, void *user, AVCodecContext *pCodecCtx) {
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
