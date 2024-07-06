//
// Created by lxp on 24-6-2.
//

#ifndef FFMPEGPRO_VIDEO_ENCODER_CORE_H
#define FFMPEGPRO_VIDEO_ENCODER_CORE_H

#include <stdint.h>

// 初始化视频编码器
int32_t init_video_encoder(const char *codec_name);

// 销毁视频编码器
void destroy_video_encoder();

// 循环编码
int32_t encoding(int32_t frame_cnt);


#endif //FFMPEGPRO_VIDEO_ENCODER_CORE_H
