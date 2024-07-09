cmake_minimum_required(VERSION 3.28)
project(sffplay)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)


set(FFMPEG_DIR /usr/local)
# 头文件搜索路径
include_directories(${FFMPEG_DIR}/include/)
# SDL2 头文件路径
include_directories(${FFMPEG_DIR}/include/SDL2)

# 动态链接库或静态链接库的搜索路径
link_directories(${FFMPEG_DIR}/lib/)


add_executable(sffplay
#        showImageWithSDL.cpp
#       simplest_ffmpeg_player.cpp
        sffplay.cpp
#        sffplay_yuv.cpp
#        showYUV420pWithSDL.cpp
)

#链接库
target_link_libraries(sffplay
        # FFmpeg 库
        avcodec
        avdevice
        avfilter
        avformat
        avutil
        swresample
        swscale
        # sdl2
        SDL2
        SDL2_image
        SDL2_test
        SDL2_ttf
        SDL2main
)




