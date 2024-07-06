#include <cstdlib>
#include <iostream>
#include <string>

#include "audio_encoder_core.h"
#include "io_data.h"


/**
 * ffmpeg -y -i bw.mp3 -acodec pcm_s16le -f s16le -ac 2 -ar 41000 bw.pcm
 * 编码器，将pcm文件编码成MP3文件
 * 视频的编解码使用 FFmpeg 7.0.1 版本，
 * 音频的编解码使用 FFmpeg 6.1.1_1 版本，为了和书籍做兼容，主要是 AVCodecContext.ch_channels做了很多改变
 * pcm 音频编码过程
 * 1. 打开输入输出文件
 * 2. 初始化音频编码器
 * 2.1 设置编码器上下文参数，包括码率、采样率、采样格式、声道数和声道布局
 * 2.2 avcodec_open2打开编码器
 * 2.3 分配frame和packet结构体内存空间
 * 3. 读取pcm文件到frame中，前面给frame.data分配空间就是就是这时候要用到
 * 3.1 读取的时候使用 av_get_bytes_per_sample 可以获得每个 frame 的采样字节数
 * 3.2 使用 packet 方式存储，交替读取 data_size 个字节，保存到frame.data对应的声道里
 * 4. 编码前面获取的 frame，avcodec_send_frame, avcodec_receive_packet，获得编码后的数据 packet
 * 5. 将packet直接写入文件，保存为 out.mp3 write_pkt_to_file
 */

int main() {
    std::cout << "audio encoder" << std::endl;

    char input_file_name[] = "tt.pcm";
    char output_file_name[] = "out.mp3";
    char codec_name[] = "MP3";

    std::cout << "Input file:" << std::string(input_file_name) << std::endl;
    std::cout << "output file:" << std::string(output_file_name) << std::endl;
    std::cout << "codec name:" << std::string(codec_name) << std::endl;

    int32_t result = open_input_output_files(input_file_name, output_file_name);
    if (result < 0) {
        return result;
    }

    result = init_audio_encoder(codec_name);
    if (result < 0) {
        return result;
    }
    result = audio_encoding();
    if (result < 0) {
        goto failed;
    }

    failed:
    destroy_audio_encoder();
    close_input_output_files();
    return 0;
}