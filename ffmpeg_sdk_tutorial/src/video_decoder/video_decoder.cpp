//
// Created by lxp on 2024/5/30.
// 将h.264视频文件解码成YUV图像
//

#include <cstdlib>
#include <iostream>
#include <string>

#include "io_data.h"
#include "video_decoder_core.h"


int main() {

    char input_file_name[] = "2_football.h264";
    char output_file_name[] = "output";

    std::cout << "Input file:" << std::string(input_file_name) << std::endl;
    std::cout << "output file:" << std::string(output_file_name) << std::endl;

    int32_t result = open_input_output_files(input_file_name, output_file_name);
    if (result < 0) {
        return result;
    }

    result = init_video_decoder();
    if (result < 0) {
        return result;
    }

    result = decoding();
    if (result < 0) {
        return result;
    }

    destroy_video_decoder();
    close_input_output_files();
    return 0;
}

