#pragma once

#include <cstdint>
#include <boost/serialization/string.hpp>
#include <boost/serialization/serialization.hpp>
#include <utility>

#define MAX_SIZE 200000

struct h264_data {
  h264_data(std::string data="", int dataLen = -1, uint64_t pts = -1, int type =-1)
      : data(std::move(data)), data_len(dataLen), pts(pts), type(type) {}

  std::string data;
  int data_len;
  uint64_t pts;
  int type;

private:
  friend class boost::serialization::access;

  template<class Archive>
  void serialize(Archive &ar, const unsigned int version) {
    ar & data;
    ar & data_len;
    ar & pts;
    ar & type;
  }
};