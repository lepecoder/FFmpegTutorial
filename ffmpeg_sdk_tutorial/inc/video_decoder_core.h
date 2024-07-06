//
// Created by lxp on 2024/5/30.
//

#ifndef FFMPEG_SDK_TUTORIAL_VIDEO_DECODER_CORE_H
#define FFMPEG_SDK_TUTORIAL_VIDEO_DECODER_CORE_H

#include <stdint.h>

int32_t init_video_decoder();

void destroy_video_decoder();

int32_t decoding();

#endif //FFMPEG_SDK_TUTORIAL_VIDEO_DECODER_CORE_H
