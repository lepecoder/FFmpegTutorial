#include <cstdlib>
#include <iostream>
#include <string>

#include "audio_resampler_core.h"
#include "io_data.h"

/**
 * 对音频文件重采样，转换采样率和音频文件格式，等价于
 *  ffmpeg -f s16le -i tt.pcm -f f32le output.pcm
 */
int main(int argc, char **argv) {

    int result = 0;

    char input_file_name[] = "ar.fltp";
    int32_t in_sample_rate = 44100;
    char in_sample_fmt[] = "fltp";
    char in_sample_layout[] = "STEREO";

    char output_file_name[] = "aro.pcm";
    int32_t out_sample_rate = 44100;
    char out_sample_fmt[] = "s16";
    char out_sample_layout[] = "STEREO";

    result = open_input_output_files(input_file_name, output_file_name);
    if (result < 0) {
        std::cerr << "open input and output error!!";
    }
    // 做重采样的所有准备
    result = init_audio_resampler(in_sample_rate, in_sample_fmt,
                                  in_sample_layout, out_sample_rate,
                                  out_sample_fmt, out_sample_layout);
    if (result < 0) {
        std::cerr << "Error: init_audio_resampler failed." << std::endl;
        return result;
    }
    // 开始重采样
    result = audio_resampling();
    if (result < 0) {
        std::cerr << "Error: audio_resampling failed." << std::endl;
        return result;
    }

    close_input_output_files();
    destroy_audio_resampler();
    std::cout << "END!!!!" << std::endl;
    return result;
}
