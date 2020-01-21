#include <string>

#ifndef INT64_C
#define INT64_C(c) (c ## LL)
#define UINT64_C(c) (c ## ULL)
#endif

//#define _M
#define _M printf( "%s(%d) : MARKER\n", __FILE__, __LINE__ )

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
};


AVFormatContext *fc = 0;
int vi = -1, waitkey = 1;

// < 0 = error
// 0 = I-Frame
// 1 = P-Frame
// 2 = B-Frame
// 3 = S-Frame
int getVopType(const void *p, int len)
{
  if (!p || 6 >= len)
    return -1;

  unsigned char *b = (unsigned char *) p;

  // Verify NAL marker
  if (b[0] || b[1] || 0x01 != b[2]) {
    b++;
    if (b[0] || b[1] || 0x01 != b[2])
      return -1;
  } // end if

  b += 3;

  // Verify VOP id
  if (0xb6 == *b) {
    b++;
    return (*b & 0xc0) >> 6;
  } // end if

  switch (*b) {
    case 0x65 :
      return 0;
    case 0x61 :
      return 1;
    case 0x01 :
      return 2;
  } // end switch

  return -1;
}

void write_frame(const void *p, int len)
{
  if (0 > vi)
    return;

  AVStream *pst = fc->streams[vi];

  // Init packet
  AVPacket pkt;
  av_init_packet(&pkt);
  pkt.flags |= (0 >= getVopType(p, len)) ? AV_PKT_FLAG_KEY : 0;
  pkt.stream_index = pst->index;
  pkt.data = (uint8_t *) p;
  pkt.size = len;

  // Wait for key frame
  if (waitkey)
    if (0 == (pkt.flags & AV_PKT_FLAG_KEY))
      return;
    else
      waitkey = 0;

  pkt.dts = AV_NOPTS_VALUE;
  pkt.pts = AV_NOPTS_VALUE;

//  av_write_frame( fc, &pkt );
  av_interleaved_write_frame(fc, &pkt);
}

void destroy()
{
  waitkey = 1;
  vi = -1;

  if (!fc)
    return;

  _M;
  av_write_trailer(fc);

  if (fc->oformat && !(fc->oformat->flags & AVFMT_NOFILE) && fc->pb)
    avio_close(fc->pb);

  // Free the stream
  _M;
  av_free(fc);

  fc = 0;
  _M;
}

int get_nal_type(void *p, int len)
{
  if (!p || 5 >= len)
    return -1;

  unsigned char *b = (unsigned char *) p;

  // Verify NAL marker
  if (b[0] || b[1] || 0x01 != b[2]) {
    b++;
    if (b[0] || b[1] || 0x01 != b[2])
      return -1;
  } // end if

  b += 3;

  return *b;
}

int create(void *p, int len)
{
//  if (0x67 != get_nal_type(p, len))
//    return -1;

  destroy();

  const char *file = "test.avi";
  AVCodecID codec_id = AV_CODEC_ID_H264;
//  AVCodecID codec_id = AV_CODEC_ID_MPEG4;
  int br = 1000000;
  int w = 828;
  int h = 1792;
  int fps = 15;

  // Create container
  _M;
  AVOutputFormat *of = av_guess_format(0, file, 0);
  fc = avformat_alloc_context();
  fc->oformat = of;
  strcpy(fc->filename, file);

  // Add video stream
  _M;
  AVStream *pst = avformat_new_stream(fc, 0);
  vi = pst->index;

  AVCodecContext *pcc = pst->codec;
  _M;
  avcodec_get_context_defaults3(pcc, NULL);
  pcc->codec_type = AVMEDIA_TYPE_VIDEO;

  pcc->codec_id = codec_id;
  pcc->bit_rate = br;
  pcc->width = w;
  pcc->height = h;
  pcc->time_base.num = 1;
  pcc->time_base.den = fps;

  // Init container
  _M;
//  av_set_parameters(fc, 0);

  if (!(fc->oformat->flags & AVFMT_NOFILE))
    avio_open(&fc->pb, fc->filename, AVIO_FLAG_WRITE);

  _M;
  avformat_write_header(fc, NULL);

  _M;
  return 1;
}

#include <iostream>
#include <windows.h>
#include <tlhelp32.h>

uintptr_t getModule(DWORD procId, const char *modName)
{
  HANDLE hModule = CreateToolhelp32Snapshot(TH32CS_SNAPALL, procId);
  MODULEENTRY32 mEntry;
  mEntry.dwSize = sizeof(mEntry);

  do {
    if (!strcmp(mEntry.szModule, modName)) {
      CloseHandle(hModule);
      return (DWORD) mEntry.hModule;
    }
  } while (Module32Next(hModule, &mEntry));
  return 0;
}

#include <cstdio>
#include <thread>
#include <fstream>
#include <vector>
#include <algorithm>
#include "h264-bitstream/h264_stream.h"

#ifdef __cplusplus
extern "C"
#endif
int main(int argc, char *argv[])
{
  av_log_set_level(AV_LOG_ERROR);
  av_register_all();

  bool b = true;

  const std::string entryName = "rpiplay.exe";

  HWND hWnd = FindWindow(0, TEXT("C:\\Users\\aelmod\\CLionProjects\\RPiPlay\\cmake-build-release\\rpiplay.exe"));
  if (hWnd == 0) {
    std::cerr << "Cannot find window." << std::endl;
  } else {
    DWORD pId;
    GetWindowThreadProcessId(hWnd, &pId);
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pId);

    if (hProc) {
//      auto entryPoint = getModule(pId, entryName.c_str());

      int size = -1;
      uint8_t *data = (uint8_t *) malloc(200000);
      int frameType;

      SIZE_T size_bytes_read = 0, data_bytes_read = 0, frame_type_bytes_read = 0;

      uint8_t *modified_data = (uint8_t *) malloc(29 * 2);
      h264_stream_t *h = h264_new();

      if (!modified_data || !h)
        std::cerr << "PEZDA!" << std::endl;

      auto start = std::chrono::steady_clock::now();
      while (b) {
//        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(10))
//          break;

        if (ReadProcessMemory(hProc, (LPVOID) (0xf91a00), &size, 4, &size_bytes_read)
            || GetLastError() == ERROR_PARTIAL_COPY) {
          if (size_bytes_read == 0) std::cerr << "Cannot read size_bytes_read" << std::endl;
        }

        if (size > 0) {
          if (ReadProcessMemory(hProc, (LPVOID) (0xe50048), data, size, &data_bytes_read)
              || GetLastError() == ERROR_PARTIAL_COPY) {
            if (data_bytes_read == 0) std::cerr << "Cannot read data_bytes_read" << std::endl;
          }
        }

        if (ReadProcessMemory(hProc, (LPVOID) (0xf91a10), &frameType, 4, &frame_type_bytes_read)
            || GetLastError() == ERROR_PARTIAL_COPY) {
          if (frame_type_bytes_read == 0) std::cerr << "Cannot read data_bytes_read" << std::endl;
        }

        if (size > 0) {
          if (frameType == 0) {
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
              data = modified_data;
            }
          }
        }

        if (data && size > 0) {
          if (!fc)
            create(data, size);

          if (fc)
            write_frame(data, size);
        }
      }
    } else {
      std::cerr << "Couldn't open process " << pId << ": " << GetLastError() << std::endl;
    }

    destroy();

    std::getchar();

    b = false;
  }
}
