//
// Created by lxp on 2024/5/30.
//
// io_data.cpp
#include "io_data.h"

#include <cstdlib>
#include <cstring>

#include <iostream>

static FILE *input_file = nullptr;
static FILE *output_file = nullptr;

/**
 * 打开输入输出文件，保存文件指针，后面读输入文件和写输出文件会用到
 * 按照二进制读写数据
 */
int32_t open_input_output_files(const char *input_name,
                                const char *output_name) {
    if (strlen(input_name) == 0 || strlen(output_name) == 0) {
        std::cerr << "Error: empty input or output file name." << std::endl;
        return -1;
    }
    close_input_output_files();
    input_file = fopen(input_name, "rb");
    if (input_file == nullptr) {
        std::cerr << "Error: failed to open input file." << std::endl;
        return -1;
    }
    output_file = fopen(output_name, "wb");
    if (output_file == nullptr) {
        std::cerr << "Error: failed to open output file." << std::endl;
        return -1;
    }
    return 0;
}

/**
 * 关闭输入输出文件
 */
void close_input_output_files() {
    if (input_file != nullptr) {
        fclose(input_file);
        input_file = nullptr;
    }
    if (output_file != nullptr) {
        fclose(output_file);
        output_file = nullptr;
    }
}

int32_t end_of_input_file() { return feof(input_file); }

/**
 * 从 input_file 读取 size 个字节到缓存区 buf
 */
int32_t read_data_to_buf(uint8_t *buf, int32_t size, int32_t &out_size) {
    int32_t read_size = fread(buf, 1, size, input_file);
    if (read_size == 0) {
        std::cerr << "Error: read_data_to_buf failed." << std::endl;
        return -1;
    }
    out_size = read_size;
    return 0;
}

/**
 * 将 frame 中的 data 写入文件，按照 422 格式写入
 * linesize 是 data 数据区的宽度，可能会大于 frame->width。
 */
int32_t write_frame_to_yuv(AVFrame *frame) {
    uint8_t **pBuf = frame->data;
    int *pStride = frame->linesize;
    // 422模式存储YUV，Y是全尺寸，UV是半尺寸
    for (size_t i = 0; i < 3; i++) {
        int32_t width = (i == 0 ? frame->width : frame->width / 2);
        int32_t height = (i == 0 ? frame->height : frame->height / 2);
        for (size_t j = 0; j < height; j++) {
            // ptr是要写入的数据的指针，后面数据指针会后移；size是每个数据项的大小，nitems是数据项的数量，output_file是要写入的文件
            fwrite(pBuf[i], 1, width, output_file);
            pBuf[i] += pStride[i];
        }
    }
    return 0;
}

/**
 * 读取yuv数据到frame，很奇怪，这里的yuv是420格式，上面是422格式
 */
int32_t read_yuv_to_frame(AVFrame *frame) {
    int32_t frame_width = frame->width;
    int32_t frame_height = frame->height;
    int32_t luma_stride = frame->linesize[0];
    int32_t chroma_stride = frame->linesize[1];
    int32_t frame_size = frame_width * frame_height * 3 / 2;
    int32_t read_size = 0;

    if (frame_width == luma_stride) {
        // 如果 width 等于 stride，说明 frame 中不存在 padding
        // 字节，可向其中整体读取
        read_size +=
                fread(frame->data[0], 1, frame_width * frame_height, input_file);
        read_size +=
                fread(frame->data[1], 1, frame_width * frame_height / 4, input_file);
        read_size +=
                fread(frame->data[2], 1, frame_width * frame_height / 4, input_file);
    } else {
        //  如果 width 不等于 stride，说明 frame 一行的末尾存在 padding
        //  字节，三个分量都应当逐行读取
        for (size_t i = 0; i < frame_height; i++) {
            read_size +=
                    fread(frame->data[0] + i * luma_stride, 1, frame_width, input_file);
        }
        for (size_t uv = 1; uv <= 2; uv++) {
            for (size_t i = 0; i < frame_height / 2; i++) {
                read_size += fread(frame->data[uv] + i * chroma_stride, 1,
                                   frame_width / 2, input_file);
            }
        }
    }

    // 验证读取数据正确
    if (read_size != frame_size) {
        std::cerr << "Error: Read data error, frame_size:" << frame_size
                  << ", read_size:" << read_size << std::endl;
        return -1;
    }
    return 0;
}


void write_pkt_to_file(AVPacket *pkt) {
    fwrite(pkt->data, 1, pkt->size, output_file);
}

/**
 * 将音频 frame->data 写入pcm文件
 * 每个 frame 中会包含一段音频，这一段音频可能有1152个采样，每个采样又由多位二进制表示
 */
int32_t write_samples_to_pcm(AVFrame *frame, AVCodecContext *codec_ctx) {
    // data_size = 4, 每个采样4字节，也就是说每个采样点的值是使用32位保存的
    int data_size = av_get_bytes_per_sample(codec_ctx->sample_fmt);
    if (data_size < 0) {
        /* This should not occur, checking just for paranoia */
        std::cerr << "Failed to calculate data size" << std::endl;
        exit(1);
    }
    // mp3格式每一帧保存1152个采样值
    // 对于每一个采样值，按照packet格式同时写入左右声道，
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < codec_ctx->ch_layout.nb_channels; ch++) {
            fwrite(frame->data[ch] + data_size * i, 1, data_size, output_file);
        }
    }
    return 0;
}

/**
 * 读取pcm文件保存到frame中，pcm文件有两种存储方式
 * packet: LRLRLR
 * planar: LLLRRR
 * 按照下面的循环方式，pcm文件的格式是packet格式，读取到frame->data数据，
 * 数组中使用planar格式保存
 */
int32_t read_pcm_to_frame(AVFrame *frame, AVCodecContext *codec_ctx) {
    int data_size = av_get_bytes_per_sample(codec_ctx->sample_fmt);
    if (data_size < 0) {
        /* This should not occur, checking just for paranoia */
        std::cerr << "Failed to calculate data size" << std::endl;
        return -1;
    }

    // 从输入文件中交替读取一个采样值的各个声道的数据，
    // 保存到AVFrame结构的存储分量中
    // nc_samples：每个声道的采样数量
    // nb_channels: 声道的数量
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < codec_ctx->ch_layout.nb_channels; ch++) {
            fread(frame->data[ch] + data_size * i, 1, data_size, input_file);
        }
    }
    return 0;
}

int32_t write_samples_to_pcm2(AVFrame *frame, enum AVSampleFormat format,
                              int channels) {
    int data_size = av_get_bytes_per_sample(format);
    if (data_size < 0) {
        /* This should not occur, checking just for paranoia */
        std::cerr << "Failed to calculate data size" << std::endl;
        exit(1);
    }
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < channels; ch++) {
            fwrite(frame->data[ch] + data_size * i, 1, data_size, output_file);
        }
    }
    return 0;
}

/**
 * 从packed文件里读取一个frame，保存到data[ch]对应声道里 planar
 */
int32_t read_pcm_to_frame2(AVFrame *frame, enum AVSampleFormat format,
                           int channels) {
    // 一个frame包含很多个sample，每个sample用data_size个字节表示
    int data_size = av_get_bytes_per_sample(format);
    if (data_size < 0) {
        /* This should not occur, checking just for paranoia */
        std::cerr << "Failed to calculate data size" << std::endl;
        return -1;
    }

    // 从输入文件中交替读取一个采样值的各个声道的数据，
    // 保存到AVFrame结构的存储分量中
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < channels; ch++) {
            // 交替读取声道信息，所以文件是packed格式保存的
            // 不同的生成存储在data数组里对应的项，所以是planar保存
            fread(frame->data[ch] + data_size * i, 1, data_size, input_file);
        }
    }
    return 0;
}

void write_packed_data_to_file(const uint8_t *buf, int32_t size) {
    fwrite(buf, 1, size, output_file);
}
