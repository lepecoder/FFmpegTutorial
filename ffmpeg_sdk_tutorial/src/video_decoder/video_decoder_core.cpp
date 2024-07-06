//
// Created by lxp on 2024/5/30.
//
// video_encoder_core.cpp
extern "C" {
#include <libavcodec/avcodec.h>
}

#include <iostream>

#include "io_data.h"
#include "video_decoder_core.h"

#define INBUF_SIZE 4096

const static AVCodec *codec = nullptr;
static AVCodecContext *codec_ctx = nullptr;

static AVCodecParserContext *parser = nullptr;

static AVFrame *frame = nullptr;
static AVPacket *pkt = nullptr;

/**
 * 初始化解码相关数据结构
 * AVCodec:
 * AVCodecContext
 * AVCodecParserContext
 */
int32_t init_video_decoder() {
    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "Error: could not find codec." << std::endl;
        return -1;
    }

    parser = av_parser_init(codec->id);
    if (!parser) {
        std::cerr << "Error: could not init parser." << std::endl;
        return -1;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "Error: could not alloc codec." << std::endl;
        return -1;
    }

    int32_t result = avcodec_open2(codec_ctx, codec, nullptr);
    if (result < 0) {
        std::cerr << "Error: could not open codec." << std::endl;
        return -1;
    }

    frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "Error: could not alloc frame." << std::endl;
        return -1;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        std::cerr << "Error: could not alloc packet." << std::endl;
        return -1;
    }

    return 0;
}

/**
 * 将 packet 转换成 frame，再提取 frame 中的yuv图像写入到文件
 */
static int32_t decode_packet(bool flushing) {
    int32_t result = 0;
    result = avcodec_send_packet(codec_ctx, flushing ? nullptr : pkt);
    if (result < 0) {
        std::cerr << "Error: failed to send packet, result:" << result << std::endl;
        return -1;
    }

    while (result) {
        result = avcodec_receive_frame(codec_ctx, frame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
            return 1;
        else if (result < 0) {
            std::cerr << "Error: failed to receive frame, result:" << result
                      << std::endl;
            return -1;
        }
        if (flushing) {
            std::cout << "Flushing:";
        }
        std::cout << "Write frame pic_num:" << frame->pts
                  << std::endl;
        write_frame_to_yuv(frame);
    }
    return 0;
}

/**
 * 读取二进制文件到 inbuf, 转换成 AVPacket，之后使用 decode_packet 解码packet
 */
int32_t decoding() {
    uint8_t inbuf[INBUF_SIZE] = {0};
    int32_t result = 0;
    uint8_t *data = nullptr;
    int32_t data_size = 0;
    while (!end_of_input_file()) {
        // data_size 是输入文件的大小
        result = read_data_to_buf(inbuf, INBUF_SIZE, data_size);
        if (result < 0) {
            std::cerr << "Error: read_data_to_buf failed." << std::endl;
            return -1;
        }

        // data是缓冲区指针，指向缓冲区开始位置
        data = inbuf;
        while (data_size > 0) {
            result = av_parser_parse2(parser, codec_ctx, &pkt->data, &pkt->size, data,
                                      data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (result < 0) {
                std::cerr << "Error: av_parser_parse2 failed." << std::endl;
                return -1;
            }

            // 读取result个字节之后，data 指针向后移动 result 个位置
            data += result;
            data_size -= result;

            if (pkt->size) {
                std::cout << "Parsed packet size:" << pkt->size << std::endl;
                result = decode_packet(false);
                if (result < 0) {
                    break;
                }
            }
        }
    }
    result = decode_packet(true);
    if (result < 0) {
        return result;
    }
    return 0;
}

void destroy_video_decoder() {
    av_parser_close(parser);
    avcodec_free_context(&codec_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
}
