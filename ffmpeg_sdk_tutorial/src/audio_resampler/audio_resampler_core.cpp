#include "audio_resampler_core.h"

#include <stdlib.h>
#include <string.h>

#include <iostream>

#include "io_data.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#define SRC_NB_SAMPLES 1152

static struct SwrContext *swr_ctx;
static AVFrame *input_frame = nullptr;
int32_t dst_nb_samples, max_dst_nb_samples, dst_nb_channels, dst_rate, src_rate;
enum AVSampleFormat src_sample_fmt = AV_SAMPLE_FMT_NONE,
        dst_sample_fmt = AV_SAMPLE_FMT_NONE;
uint8_t **dst_data = NULL;
int32_t dst_linesize = 0;

/**
 * 初始化音频帧信息
 */
static int32_t init_frame(int sample_rate, int sample_format,
                          uint64_t channel_layout) {
    int32_t result = 0;
    input_frame->sample_rate = sample_rate;  // 采样率
    input_frame->nb_samples = SRC_NB_SAMPLES;  // 每个frame的采样点数量
    input_frame->format = sample_format;  // 采样格式 enum AVSampleFormat
    input_frame->channel_layout = channel_layout;  // 通道布局

    // 为frame分配data和buffer内存
    result = av_frame_get_buffer(input_frame, 0);
    if (result < 0) {
        std::cerr << "Error: AVFrame could not get buffer." << std::endl;
        return -1;
    }

    return result;
}

/**
 * 初始化声音重采样，输入采样率，音频格式，声道信息
 * 1. swr_alloc() 创建SwrContext
 * 2. opt_set_  设置参数
 * 3. swr_init() 初始化SwrContext
 *
 * 初始化 AVFrame 信息
 */
int32_t init_audio_resampler(int32_t in_sample_rate, const char *in_sample_fmt,
                             const char *in_ch_layout, int32_t out_sample_rate,
                             const char *out_sample_fmt,
                             const char *out_ch_layout) {
    int32_t result = 0;
    // 创建 SwrContext
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
        std::cerr << "Error: failed to allocate SwrContext." << std::endl;
        return -1;
    }

    int64_t src_ch_layout = -1, dst_ch_layout = -1;
    if (!strcasecmp(in_ch_layout, "MONO")) {
        src_ch_layout = AV_CH_LAYOUT_MONO;  // 前置中央声道
    } else if (!strcasecmp(in_ch_layout, "STEREO")) {
        src_ch_layout = AV_CH_LAYOUT_STEREO;  // 前左或前右声道
    } else if (!strcasecmp(in_ch_layout, "SURROUND")) {
        src_ch_layout = AV_CH_LAYOUT_SURROUND;  // 前左或前右或前中
    } else {
        std::cerr << "Error: unsupported input channel layout." << std::endl;
        return -1;
    }
    if (!strcasecmp(out_ch_layout, "MONO")) {
        dst_ch_layout = AV_CH_LAYOUT_MONO;
    } else if (!strcasecmp(out_ch_layout, "STEREO")) {
        dst_ch_layout = AV_CH_LAYOUT_STEREO;
    } else if (!strcasecmp(out_ch_layout, "SURROUND")) {
        dst_ch_layout = AV_CH_LAYOUT_SURROUND;
    } else {
        std::cerr << "Error: unsupported output channel layout." << std::endl;
        return -1;
    }

    if (!strcasecmp(in_sample_fmt, "fltp")) {
        src_sample_fmt = AV_SAMPLE_FMT_FLTP;
    } else if (!strcasecmp(in_sample_fmt, "s16")) {
//        src_sample_fmt = AV_SAMPLE_FMT_S32;
        src_sample_fmt = AV_SAMPLE_FMT_S16P;
    } else {
        std::cerr << "Error: unsupported input sample format." << std::endl;
        return -1;
    }
    if (!strcasecmp(out_sample_fmt, "fltp")) {
        dst_sample_fmt = AV_SAMPLE_FMT_FLTP;
    } else if (!strcasecmp(out_sample_fmt, "s16")) {
        dst_sample_fmt = AV_SAMPLE_FMT_S16;
    } else {
        std::cerr << "Error: unsupported output sample format." << std::endl;
        return -1;
    }

    src_rate = in_sample_rate;
    dst_rate = out_sample_rate;
    // 使用av_opt_set来设置swr_ctx的值，更加容易阅读，可以明确设置值的类型，而且有返回值，确保值被正确设置
    av_opt_set_int(swr_ctx, "in_channel_layout", src_ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", src_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", src_sample_fmt, 0);

    av_opt_set_int(swr_ctx, "out_channel_layout", dst_ch_layout, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", dst_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_sample_fmt, 0);

    // 初始化SwrContext
    result = swr_init(swr_ctx);
    if (result < 0) {
        std::cerr << "Error: failed to initialize SwrContext." << std::endl;
        return -1;
    }

    // 创建 AVFrame
    input_frame = av_frame_alloc();
    if (!input_frame) {
        std::cerr << "Error: could not alloc input frame." << std::endl;
        return -1;
    }
    // 初始化frame的一些信息
    result = init_frame(in_sample_rate, src_sample_fmt, src_ch_layout);
    if (result < 0) {
        std::cerr << "Error: failed to initialize input frame." << std::endl;
        return -1;
    }
    // 输入音频的采样率1152，那么根据输入采样率和输出采样率，上取整可以计算输出音频每个frame的样本数量
    // 当输入采样率是44100时，nb_sample是1152，现在输出采样率是22050，那么输出的nb_sample就是576
    max_dst_nb_samples = dst_nb_samples = av_rescale_rnd(
            SRC_NB_SAMPLES, out_sample_rate, in_sample_rate, AV_ROUND_UP);
    // 返回 ch_layout中的通道数量
    dst_nb_channels = av_get_channel_layout_nb_channels(dst_ch_layout);
    std::cout << "max_dst_nb_samples:" << max_dst_nb_samples
              << ", dst_nb_channels : " << dst_nb_channels << std::endl;

    return result;
}

/**
 * 对frame进行重采样，
 */
static int32_t resampling_frame() {
    int32_t result = 0;
    int32_t dst_bufsize = 0;
    // swr_get_delay 下一个输入采样率和下一个输出采样之间的延迟
    // TODO 不知道为什么要加一个延迟
    // 总之因为采样率的变化，这里的输出nb_samples是576
    int a = swr_get_delay(swr_ctx, src_rate);
    dst_nb_samples =
            av_rescale_rnd(swr_get_delay(swr_ctx, src_rate) + SRC_NB_SAMPLES,
                           dst_rate, src_rate, AV_ROUND_UP);
    // 有delay的情况下
    if (dst_nb_samples > max_dst_nb_samples) {
        // 释放 av_alloc() 分配的空间，使用av_samples_alloc和新的dst_nb_samples重新分配空间
        av_freep(&dst_data[0]);
        result = av_samples_alloc(dst_data, &dst_linesize, dst_nb_channels,
                                  dst_nb_samples, dst_sample_fmt, 1);
        if (result < 0) {
            std::cerr << "Error:failed to reallocat dst_data." << std::endl;
            return -1;
        }
        std::cout << "nb_samples exceeds max_dst_nb_samples, buffer reallocated."
                  << std::endl;
        max_dst_nb_samples = dst_nb_samples;
    }
    result = swr_convert(swr_ctx, dst_data, dst_nb_samples,
                         (const uint8_t **) input_frame->data, SRC_NB_SAMPLES);
    if (result < 0) {
        std::cerr << "Error:swr_convert failed." << std::endl;
        return -1;
    }
    dst_bufsize = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels,
                                             result, dst_sample_fmt, 1);
    if (dst_bufsize < 0) {
        std::cerr << "Error:Could not get sample buffer size." << std::endl;
        return -1;
    }
    write_packed_data_to_file(dst_data[0], dst_bufsize);

    return result;
}

/**
 * 音频重采样
 */
int32_t audio_resampling() {
    // 分配dst_data 数组空间和采样缓冲区空间
    int32_t result = av_samples_alloc_array_and_samples(
            &dst_data, &dst_linesize, dst_nb_channels, dst_nb_samples, dst_sample_fmt,
            0);
    if (result < 0) {
        std::cerr << "Error: av_samples_alloc_array_and_samples failed."
                  << std::endl;
        return -1;
    }
    std::cout << "dst_linesize:" << dst_linesize << std::endl;

    while (!end_of_input_file()) {
        // 读取一个frame
        result = read_pcm_to_frame2(input_frame, src_sample_fmt, 2);
        if (result < 0) {
            std::cerr << "Error: read_pcm_to_frame failed." << std::endl;
            return -1;
        }
        result = resampling_frame();
        if (result < 0) {
            std::cerr << "Error: resampling_frame failed." << std::endl;
            return -1;
        }
    }

    return result;
}

void destroy_audio_resampler() {
    av_frame_free(&input_frame);
    if (dst_data) av_freep(&dst_data[0]);
    av_freep(&dst_data);
    swr_free(&swr_ctx);
}
