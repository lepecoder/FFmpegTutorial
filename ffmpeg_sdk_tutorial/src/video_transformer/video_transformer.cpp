#include <cstdlib>
#include <iostream>
#include <string>

#include "io_data.h"
#include "video_swscale_core.h"


int main(int argc, char **argv) {
    int result = 0;

    char input_file_name[] = "vt.yuv";
    char input_pic_size[] = "720x720";
    char input_pix_fmt[] = "YUV420P";
    char output_file_name[] = "scaled.data";
    char output_pic_size[] = "200x200";
    char output_pix_fmt[] = "RGB24";

    do {
        result = open_input_output_files(input_file_name, output_file_name);
        if (result < 0) {
            break;
        }
        // 初始化视频转换相关结构，SwsContext和AVFrame
        result = init_video_swscale(input_pic_size, input_pix_fmt, output_pic_size,
                                    output_pix_fmt);
        if (result < 0) {
            break;
        }
        // 在io_data里添加一个函数，读取yuv里有多少帧
        result = transforming(100);
        if (result < 0) {
            break;
        }
    } while (0);

    failed:
    destroy_video_swscale();
    close_input_output_files();
    return result;
}
