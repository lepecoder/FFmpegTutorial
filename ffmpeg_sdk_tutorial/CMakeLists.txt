cmake_minimum_required(VERSION 3.28)

# 注意 FFmpegPro是工程名称，开发这需要按照实际进行替换
project(FFmpegPro)

set(CMAKE_CXX_STANDARD 17)

# FFmpeg的安装目录，可以通过命令"brew info ffmpeg"获取
#set(FFMPEG_DIR /usr/local/Cellar/ffmpeg@6/6.1.1_1)
set(FFMPEG_DIR /usr/local)
# 头文件搜索路径
include_directories(${FFMPEG_DIR}/include/)
# 动态链接库或静态链接库的搜索路径
link_directories(${FFMPEG_DIR}/lib/)

add_executable(FFmpegPro
#        ../ffmpeg/libavformat/demux.c
        #        audio_encoder.cpp
)

#链接库
target_link_libraries(FFmpegPro
        #FFmpeg 库
        avcodec
#        avdevice
#        avfilter
        avformat
        avutil
        swresample
        swscale
)

# 添加头文件目录
include_directories(${PROJECT_SOURCE_DIR}/inc)  # 自定义头文件

# 添加源文件目录
file(GLOB SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cpp)
target_sources(FFmpegPro PRIVATE ${SRC_FILES})

# 添加avformat目录
#file(GLOB AVFORMAT_FILES ../ffmpeg/libavformat/*.c)
#target_sources(FFmpegPro PRIVATE ${AVFORMAT_FILES})

# 视频编码
#file(GLOB V_ENCODE_FILES ${PROJECT_SOURCE_DIR}/src/video_encoder/*.cpp)
#target_sources(FFmpegPro PRIVATE ${V_ENCODE_FILES})

# 音频编码
#file(GLOB A_ENCODE_FILES ${PROJECT_SOURCE_DIR}/src/audio_encoder/*.cpp)
#target_sources(FFmpegPro PRIVATE ${A_ENCODE_FILES})

# 音频解码
#file(GLOB A_DECODE_FILES ${PROJECT_SOURCE_DIR}/src/audio_decoder/*.cpp)
#target_sources(FFmpegPro PRIVATE ${A_DECODE_FILES})

# 音视频解封装
#file(GLOB DEMUX_FILES ${PROJECT_SOURCE_DIR}/src/demuxer/*.cpp)
#target_sources(FFmpegPro PRIVATE ${DEMUX_FILES})

# 音视频封装
#file(GLOB MUX_FILES ${PROJECT_SOURCE_DIR}/src/muxer/*.cpp)
#target_sources(FFmpegPro PRIVATE ${MUX_FILES})

# 视频格式转换
#file(GLOB VIDEOTRANS_FILES ${PROJECT_SOURCE_DIR}/src/video_transformer/*.cpp)
#target_sources(FFmpegPro PRIVATE ${VIDEOTRANS_FILES})

# 音频重采样
file(GLOB AUDIO_RESAMPLE_FILES ${PROJECT_SOURCE_DIR}/src/audio_resampler/*.cpp)
target_sources(FFmpegPro PRIVATE ${AUDIO_RESAMPLE_FILES})
