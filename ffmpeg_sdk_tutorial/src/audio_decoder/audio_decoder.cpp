#include <cstdlib>
#include <iostream>
#include <string>

#include "audio_decoder_core.h"
#include "io_data.h"

/**
 * 音频解码
 * 将编码后的音频文件解码成原始的pcm文件
 */
int main(int argc, char **argv) {

    char input_file_name[] = "a.mp3";
    char output_file_name[] = "a.pcm";

    std::cout << "Input file:" << std::string(input_file_name) << std::endl;
    std::cout << "output file:" << std::string(output_file_name) << std::endl;

    int32_t result = open_input_output_files(input_file_name, output_file_name);
    if (result < 0) {
        return result;
    }

    result = init_audio_decoder("MP3");
    if (result < 0) {
        return result;
    }

    result = audio_decoding();
    if (result < 0) {
        return result;
    }

    destroy_audio_decoder();

    close_input_output_files();
    return 0;
}