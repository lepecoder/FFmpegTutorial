#include "muxer_core.h"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
}

#include <iostream>


#define STREAM_FRAME_RATE 25 /* 25 images/s */

static AVFormatContext *video_fmt_ctx = nullptr, *audio_fmt_ctx = nullptr,
        *output_fmt_ctx = nullptr;
static AVPacket pkt;
static int32_t in_video_st_idx = -1, in_audio_st_idx = -1;
static int32_t out_video_st_idx = -1, out_audio_st_idx = -1;

/**
 * 初始化输入视频的信息，打开视频文件，构造 AVInputFormat， AVFormatContext
 */
static int32_t init_input_video(char *video_input_file, const char *video_format) {
    int32_t result = 0;
    // TODO AVInputFormat是什么作用
    const AVInputFormat *video_input_format = av_find_input_format(video_format);
    if (!video_input_format) {
        std::cerr << "Error: failed to find proper AVInputFormat for format:"
                  << std::string(video_format) << std::endl;
        return -1;
    }
    // 打开输出文件，读取文件头，不创建AVCodec，会分配AVFormatContext
    result = avformat_open_input(&video_fmt_ctx, video_input_file,
                                 video_input_format, nullptr);
    if (result < 0) {
        std::cerr << "Error: avformat_open_input failed!" << std::endl;
        return -1;
    }
    /**
     * 读取packet获取文件流信息，对于没有文件头的格式比较有用
     * TODO 这里为什么获取的帧率是60 AVRational
     * avformat_find_stream_info里设置的实际帧率 r_frame_rate=60，但在这之前已经获取
     * 到了avg_frame_rate=25，实际播放和ffprobe也是25帧，不知道为什么封装到MP4里就是60帧
     */
    result = avformat_find_stream_info(video_fmt_ctx, nullptr);
//    video_fmt_ctx->streams[0]->r_frame_rate = video_fmt_ctx->streams[0]->avg_frame_rate;
    if (result < 0) {
        std::cerr << "Error: avformat_find_stream_info failed!" << std::endl;
        return -1;
    }
    return result;
}

/**
 * 初始化音频流的信息，打开音频文件，构造 AVInputFormat AVFormatContext
 */
static int32_t init_input_audio(char *audio_input_file,
                                const char *audio_format) {
    int32_t result = 0;
    const AVInputFormat *audio_input_format = av_find_input_format(audio_format);
    if (!audio_input_format) {
        std::cerr << "Error: failed to find proper AVInputFormat for format:"
                  << std::string(audio_format) << std::endl;
        return -1;
    }

    result = avformat_open_input(&audio_fmt_ctx, audio_input_file,
                                 audio_input_format, nullptr);
    if (result < 0) {
        std::cerr << "Error: avformat_open_input failed!" << std::endl;
        return -1;
    }
    result = avformat_find_stream_info(audio_fmt_ctx, nullptr);
    if (result < 0) {
        std::cerr << "Error: avformat_find_stream_info failed!" << std::endl;
        return -1;
    }
    return result;
}

/**
 * 配置输出文件的AVFormatContext，添加新的流媒体 avformat_new_stream，包括一个视频流和一个音频流，流媒体的信息是通过
 * avcodec_parameters_copy拷贝的两路输入流媒体的信息
 */
static int32_t init_output(char *output_file) {
    int32_t result = 0;
    // 为输出文件分配AVFormatContext
    avformat_alloc_output_context2(&output_fmt_ctx, nullptr, nullptr,
                                   output_file);
    if (!output_fmt_ctx) {
        std::cerr << "Error: alloc output format context failed!" << std::endl;
        return -1;
    }

    const AVOutputFormat *fmt = output_fmt_ctx->oformat;
    std::cout << "Default video codec id:" << fmt->video_codec
              << ", audio codec id:" << fmt->audio_codec << std::endl;

    // AVFormatContext保存了输出文件的所有信息，avformat_new_stream向输出文件里添加一路stream
    AVStream *video_stream = avformat_new_stream(output_fmt_ctx, nullptr);
    if (!video_stream) {
        std::cerr << "Error: add video stream to output format context failed!"
                  << std::endl;
        return -1;
    }
    // video_stream 流在输出文件中的序号
    out_video_st_idx = video_stream->index;
    // 找到输入视频文件中视频流的序号
    in_video_st_idx = av_find_best_stream(video_fmt_ctx, AVMEDIA_TYPE_VIDEO, -1,
                                          -1, nullptr, 0);
    if (in_video_st_idx < 0) {
        std::cerr << "Error: find video stream in input video file failed!"
                  << std::endl;
        return -1;
    }
    // 拷贝视频流参数
    result = avcodec_parameters_copy(
            video_stream->codecpar,
            video_fmt_ctx->streams[in_video_st_idx]->codecpar);
    if (result < 0) {
        std::cerr << "Error: copy video codec parameters failed!" << std::endl;
        return -1;
    }

    video_stream->id = output_fmt_ctx->nb_streams - 1;  // 设置id是最后一个流的序号
    video_stream->time_base = (AVRational) {1, STREAM_FRAME_RATE};  // 每秒25帧，每帧的时间就是1/25

    /*  同样的在输出文件里添加音频流  */
    AVStream *audio_stream = avformat_new_stream(output_fmt_ctx, nullptr);
    if (!audio_stream) {
        std::cerr << "Error: add audio stream to output format context failed!"
                  << std::endl;
        return -1;
    }
    out_audio_st_idx = audio_stream->index;
    in_audio_st_idx = av_find_best_stream(audio_fmt_ctx, AVMEDIA_TYPE_AUDIO, -1,
                                          -1, nullptr, 0);
    if (in_audio_st_idx < 0) {
        std::cerr << "Error: find audio stream in input audio file failed!"
                  << std::endl;
        return -1;
    }
    result = avcodec_parameters_copy(
            audio_stream->codecpar,
            audio_fmt_ctx->streams[in_audio_st_idx]->codecpar);
    if (result < 0) {
        std::cerr << "Error: copy audio codec parameters failed!" << std::endl;
        return -1;
    }
    audio_stream->id = output_fmt_ctx->nb_streams - 1;  // nb_streams是流的数量，那么 nb_streams-1应该是最新流的序号
    audio_stream->time_base =
            (AVRational) {1, audio_stream->codecpar->sample_rate};  // 音频帧的持续时间和采样率有关

    av_dump_format(output_fmt_ctx, 0, output_file, 1);
    std::cout << "Output video idx:" << out_video_st_idx
              << ", audio idx:" << out_audio_st_idx << std::endl;

    if (!(fmt->flags & AVFMT_NOFILE)) {
        result = avio_open(&output_fmt_ctx->pb, output_file, AVIO_FLAG_WRITE);
        if (result < 0) {
            std::cerr << "Error: avio_open output file failed!"
                      << std::string(output_file) << std::endl;
            return -1;
        }
    }
    return result;
}

/**
 * 初始化封装器，打开输出的视频文件，音频文件，创建输出文件，向输出文件里添加视频流和音频流
 */
int32_t init_muxer(char *video_input_file, char *audio_input_file,
                   char *output_file) {
    int32_t result = init_input_video(video_input_file, "h264");
    if (result < 0) {
        return result;
    }
    result = init_input_audio(audio_input_file, "mp3");
    if (result < 0) {
        return result;
    }
    result = init_output(output_file);
    if (result < 0) {
        return result;
    }
    return 0;
}

/**
 * 封装音频和视频文件
 */
int32_t muxing() {
    int32_t result = 0;
    // dts 编码时间戳
    int64_t prev_video_dts = -1;
    // pts 展示时间戳
    int64_t cur_video_pts = 0, cur_audio_pts = 0;
    // 输入的音频流和视频流
    AVStream *in_video_st = video_fmt_ctx->streams[in_video_st_idx];
    AVStream *in_audio_st = audio_fmt_ctx->streams[in_audio_st_idx];
    // 这里的输入流是什么
    AVStream *output_stream = nullptr, *input_stream = nullptr;

    int32_t video_frame_idx = 0;

    // 分配stream私有信息，并将stream header写入输出文件
    result = avformat_write_header(output_fmt_ctx, nullptr);
    if (result < 0) {
        return result;
    }

    av_init_packet(&pkt);
    pkt.data = nullptr;
    pkt.size = 0;

    std::cout << "Video r_frame_rate:" << in_video_st->r_frame_rate.num << "/"
              << in_video_st->r_frame_rate.den << std::endl;
    std::cout << "Video time_base:" << in_video_st->time_base.num << "/"
              << in_video_st->time_base.den << std::endl;

    while (1) {
        // 比较视频帧和音频帧的位置, 小于0说明视频在前
        if (av_compare_ts(cur_video_pts, in_video_st->time_base, cur_audio_pts,
                          in_audio_st->time_base) <= 0) {
            // Write video
            input_stream = in_video_st;
            // 从输入视频流里读一个 packet
            result = av_read_frame(video_fmt_ctx, &pkt);
            if (result < 0) {
                av_packet_unref(&pkt);
                break;
            }

            // packet 没有编码时间戳信息，补充信息
            if (pkt.pts == AV_NOPTS_VALUE) {
                int64_t frame_duration =
                        (double) AV_TIME_BASE / av_q2d(in_video_st->avg_frame_rate);
                pkt.duration = (double) frame_duration /
                               (double) (av_q2d(in_video_st->time_base) * AV_TIME_BASE);
                pkt.pts = (double) (video_frame_idx * frame_duration) /
                          (double) (av_q2d(in_video_st->time_base) * AV_TIME_BASE);
                pkt.dts = pkt.dts;
                std::cout << "frame_duration:" << frame_duration
                          << ", pkt.duration : " << pkt.duration << ", pkt.pts "
                          << pkt.pts << std::endl;
            }

            video_frame_idx++;
            cur_video_pts = pkt.pts;
            pkt.stream_index = out_video_st_idx;
            output_stream = output_fmt_ctx->streams[out_video_st_idx];
        } else {
            // Write audio
            input_stream = in_audio_st;
            result = av_read_frame(audio_fmt_ctx, &pkt);
            if (result < 0) {
                av_packet_unref(&pkt);
                break;
            }

            cur_audio_pts = pkt.pts;
            pkt.stream_index = out_audio_st_idx;
            output_stream = output_fmt_ctx->streams[out_audio_st_idx];
        }

        /* 从视频流或音频流里读取一个pkt，设置stream_index等于输出文件对应的流，后面设置packet的一些信息 */
        // pts的单位是多少个time_base，所以要从输入流的time_base转换成输出流的time_base
        pkt.pts = av_rescale_q_rnd(
                pkt.pts, input_stream->time_base, output_stream->time_base,
                (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(
                pkt.dts, input_stream->time_base, output_stream->time_base,
                (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, input_stream->time_base,
                                    output_stream->time_base);
        std::cout << "Final pts:" << pkt.pts << ", duration:" << pkt.duration
                  << ", output_stream->time_base:" << output_stream->time_base.num
                  << "/" << output_stream->time_base.den << std::endl;
        // 将 packet 写入输出文件，确保正确交错，TODO(这里的交错是什么)
        // 将pkt写入编码器的buffer，然后重新根据dts排序
//        if (av_interleaved_write_frame(output_fmt_ctx, &pkt) < 0) {
        if (av_write_frame(output_fmt_ctx, &pkt) < 0) {
            std::cerr << "Error: failed to mux packet!" << std::endl;
            break;
        }
        // av_read_frame会增加一个引用计数，
        av_packet_unref(&pkt);
    }
    // 写入尾部信息，释放输出文件私有数据
    // 在 avformat_write_header 之后调用
    result = av_write_trailer(output_fmt_ctx);
    if (result < 0) {
        return result;
    }
    return result;
}

void destroy_muxer() {
    avformat_free_context(video_fmt_ctx);
    avformat_free_context(audio_fmt_ctx);

    if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_fmt_ctx->pb);
    }
    avformat_free_context(output_fmt_ctx);
}


