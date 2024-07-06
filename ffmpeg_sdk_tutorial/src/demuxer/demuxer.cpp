#include <cstdlib>
#include <iostream>
#include <string>

#include "demuxer_core.h"

/**
 * 音视频解封装，将封装格式 FLV MPEG-TS MP4 解成音频和视频格式
 */
int main(int argc, char **argv) {
    char input_file[] = "demuxer.mp4";
    char output_v[] = "demuxer.yuv";
    char output_a[] = "demuxer.pcm";
    int32_t result = init_demuxer(input_file, output_v, output_a);
    if (result < 0) {
        return -1;
    }
    result = demuxing(output_v, output_a);

    destroy_demuxer();
    return 0;
}
