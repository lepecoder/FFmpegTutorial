#include <cstdlib>
#include <iostream>
#include <string>

#include "muxer_core.h"

/**
 * 将音频流和视频流封装成mp4格式
 */
int main(int argc, char **argv) {
    char input_v[] = "muxer.h264";
    char input_a[] = "muxer.mp3";
    char output_file[] = "muxer.mp4";
    int32_t result = 0;
    do {
        result = init_muxer(input_v, input_a, output_file);
        if (result < 0) {
            break;
        }
        result = muxing();
        if (result < 0) {
            break;
        }

    } while (0);
    destroy_muxer();

    return result;
}
