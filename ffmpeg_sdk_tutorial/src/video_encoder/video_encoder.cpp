//
// Created by lxp on 24-6-2.
//

#include <cstdlib>
#include <iostream>
#include <string>

#include "io_data.h"
#include "video_encoder_core.h"

/**
 * 不知道为什么，编码出的视频下面会有一行绿色，而且视频颜色不正确，使用 FFmpeg
 * 官方例子也是一样；最后证实是生成的 frame 有错误。
 */

int main(int argc, char **argv) {

    char input_file_name[] = "1_soccor.yuv";
    char output_file_name[] = "soccor1.yuv";
    char codec_name[] = "libx264";

    std::cout << "Input file:" << std::string(input_file_name) << std::endl;
    std::cout << "output file:" << std::string(output_file_name) << std::endl;
    std::cout << "codec name:" << std::string(codec_name) << std::endl;

    int32_t result = open_input_output_files(input_file_name, output_file_name);
    if (result < 0) {
        return result;
    }
    result = init_video_encoder(codec_name);
    if (result < 0) {
        goto failed;
    }
    result = encoding(300);
    if (result < 0) {
        goto failed;
    }

    failed:
    destroy_video_encoder();
    close_input_output_files();
    return 0;
}
