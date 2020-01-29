cmake_minimum_required(VERSION 3.15)
project(airplayer)

set(CMAKE_CXX_STANDARD 17)
set(Boost_COMPILER mgw81)
find_package(Boost REQUIRED COMPONENTS system serialization)
include_directories(${Boost_INCLUDE_DIRS})

include_directories("D:/Development/Lib/ffmpeg-4.2.1-win32-dev/include")
link_directories("D:/Development/Lib/ffmpeg-4.2.1-win32-dev/lib")

include_directories("D:/Development/Lib/SDL2-2.0.10/i686-w64-mingw32/include")
link_directories("D:/Development/Lib/SDL2-2.0.10/i686-w64-mingw32/lib")

include_directories("D:/Development/Lib/boost/include")
link_directories("D:/Development/Lib/boost/lib")

add_subdirectory(h264-bitstream)
include_directories(${Boost_INCLUDE_DIRS})

add_executable(airplayer #[[main.cpp]] fftest.cpp H264_Decoder.cpp H264_Decoder.h tinylib.h #[[mainkek.cpp]])
target_link_libraries(airplayer avcodec avutil avformat swscale h264-bitstream mingw32 SDL2main SDL2 ${Boost_LIBRARIES})

#add_executable(player player.cpp)
#target_link_libraries(player avcodec avutil avformat swscale SDL2main SDL2)