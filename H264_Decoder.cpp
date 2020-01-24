#include "H264_Decoder.h"
#include "tinylib.h"

H264_Decoder::H264_Decoder(h264_decoder_callback frameCallback, void *user)
    : codec(NULL), codec_context(NULL), parser(NULL), frame(0), cb_frame(frameCallback), cb_user(user),
      frame_timeout(0), frame_delay(0)
{
  avcodec_register_all();
}

H264_Decoder::~H264_Decoder()
{

  if (parser) {
    av_parser_close(parser);
    parser = NULL;
  }

  if (codec_context) {
    avcodec_close(codec_context);
    av_free(codec_context);
    codec_context = NULL;
  }

  if (picture) {
    av_free(picture);
    picture = NULL;
  }

  cb_frame = NULL;
  cb_user = NULL;
  frame = 0;
  frame_timeout = 0;
}

bool H264_Decoder::load(float fps)
{
  codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!codec) {
    printf("Error: cannot find the h264 codec");
    return false;
  }

  codec_context = avcodec_alloc_context3(codec);

  if (codec->capabilities & AV_CODEC_CAP_TRUNCATED) {
    codec_context->flags |= AV_CODEC_FLAG_TRUNCATED;
  }

  if (avcodec_open2(codec_context, codec, NULL) < 0) {
    printf("Error: could not open codec.\n");
    return false;
  }

  picture = av_frame_alloc();
  parser = av_parser_init(AV_CODEC_ID_H264);

  if (!parser) {
    printf("Erorr: cannot create H264 parser.\n");
    return false;
  }

  if (fps > 0.0001f) {
    frame_delay = (1.0f / fps) * 1000ull * 1000ull * 1000ull;
    frame_timeout = rx_hrtime() + frame_delay;
  }

  // kickoff reading...
//  readBuffer();

  return true;
}

bool inited = false;

bool H264_Decoder::readFrame(unsigned char *data, int &size, bool &first_frame)
{
  rawData = data;
  rawDataSize = size;

  if (!inited) {
    readBuffer();
    inited = true;
  }

  uint64_t now = rx_hrtime();
  if (now < frame_timeout) {
    return false;
  }

  bool needs_more = false;

  while (!update(needs_more, first_frame)) {
    if (needs_more) {
      readBuffer();
    }
  }

  // it may take some 'reads' before we can set the fps
  if (frame_timeout == 0 && frame_delay == 0) {
    double fps = av_q2d(codec_context->time_base);
    if (fps > 0.0) {
      frame_delay = fps * 1000ull * 1000ull * 1000ull;
    }
  }

  if (frame_delay > 0) {
    frame_timeout = rx_hrtime() + frame_delay;
  }

  return true;
}

void H264_Decoder::decodeFrame(uint8_t *data, int size, bool &first_frame)
{

  AVPacket pkt;
  int got_picture = 0;
  int len = 0;

  av_init_packet(&pkt);

  pkt.data = data;
  pkt.size = size;

  len = avcodec_decode_video2(codec_context, picture, &got_picture, &pkt);
  if (len < 0) {
    printf("Error while decoding a frame.\n");
  }

  if (got_picture == 0) {
    return;
  }

  ++frame;

  if (cb_frame && !first_frame) {
    cb_frame(picture, &pkt, cb_user, codec_context);
  }
}

int H264_Decoder::readBuffer()
{
  int bytes_read = 0;

  if (rawDataSize < 0) return 0;

  if (rawDataSize <= H264_INBUF_SIZE) {
    bytes_read = rawDataSize;
    memcpy(inbuf, rawData, rawDataSize);
  } else {
    if (rawDataSize > H264_INBUF_SIZE)
      rawDataSize = rawDataSize - H264_INBUF_SIZE;

//    bytes_read = H264_INBUF_SIZE;
    memcpy(inbuf, rawData + H264_INBUF_SIZE, rawDataSize);
  }

  if (bytes_read) {
    std::copy(inbuf, inbuf + bytes_read, std::back_inserter(buffer));
  }

  return bytes_read;
}

bool H264_Decoder::update(bool &needsMoreBytes, bool &first_frame)
{

  needsMoreBytes = false;

  if (buffer.size() == 0) {
    needsMoreBytes = true;
    return false;
  }

  uint8_t *data = NULL;
  int size = 0;
  int len = av_parser_parse2(parser, codec_context, &data, &size,
                             &buffer[0], buffer.size(), 0, 0, AV_NOPTS_VALUE);

  if (size == 0 && len >= 0) {
    needsMoreBytes = true;
    return false;
  }

  if (len) {
    decodeFrame(&buffer[0], size, first_frame);
    buffer.erase(buffer.begin(), buffer.begin() + len);
    return true;
  }

  return false;
}
