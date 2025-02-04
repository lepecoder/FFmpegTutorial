#include "demuxer_core.h"

extern "C" {  // C 格式的头文件，用于告诉编译器这些头文件使用C语言的方式进行编译，不进行名称修饰
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
}

#include <iostream>

#include "io_data.h"

static AVFormatContext *format_ctx = nullptr;
static AVCodecContext *video_dec_ctx = nullptr, *audio_dec_ctx = nullptr;
static int video_stream_index = -1, audio_stream_index = -1;
static AVStream *video_stream = nullptr, *audio_stream = nullptr;
static FILE *output_video_file = nullptr, *output_audio_file = nullptr;
static AVFrame *frame = nullptr;
static AVPacket *pkt = nullptr;
//static AVPacket pkt;

/**
 * 获取输入文件里最佳的 type 流，序号是 stream_idx, 创建对应的 AVCodec 和 AVCodecContext 并打开编码器。
 */
static int open_codec_context(int32_t *stream_idx, AVCodecContext **dec_ctx,
                              AVFormatContext *fmt_ctx, enum AVMediaType type) {
    int ret, stream_index;
    AVStream *st;
    const AVCodec *dec = NULL;

    // 获取最佳的 type 流
    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        std::cerr << "Error: Could not find "
                  << std::string(av_get_media_type_string(type))
                  << " stream in input file." << std::endl;
        return ret;
    } else {
        stream_index = ret;
        st = fmt_ctx->streams[stream_index];

        /* find decoder for the stream */
        dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            std::cerr << "Error: Failed to find codec:"
                      << std::string(av_get_media_type_string(type)) << std::endl;
            return -1;
        }

        // 根据AVCodec创建AVCodecContext
        *dec_ctx = avcodec_alloc_context3(dec);
        if (!*dec_ctx) {
            std::cerr << "Error: Failed to alloc codec context:"
                      << std::string(av_get_media_type_string(type)) << std::endl;
            return -1;
        }

        // 根据 AVCodecParameters 填充 AVCodecContext
        if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
            std::cerr << "Error: Failed to copy codec parameters to decoder context."
                      << std::endl;
            return ret;
        }

        // 根据 AVCodec 初始化 AVCodecContext，在此之前比如使用 avcodec_alloc_context3分配上下文
        if ((ret = avcodec_open2(*dec_ctx, dec, nullptr)) < 0) {
            std::cerr << "Error: Could not open "
                      << std::string(av_get_media_type_string(type)) << " codec."
                      << std::endl;
            return ret;
        }
        *stream_idx = stream_index;
    }
    return 0;
}

/**
 * 初始化解封装器，打开输入文件，解析所有媒体流，选择最佳的一路
 */
int32_t init_demuxer(char *input_name, char *video_output_name,
                     char *audio_output_name) {
    if (strlen(input_name) == 0) {
        std::cerr << "Error: empty input file name." << std::endl;
        exit(-1);
    }

    // 打开输入流，填充 AVFormatContext信息
    int32_t result =
            avformat_open_input(&format_ctx, input_name, nullptr, nullptr);
    if (result < 0) {
        std::cerr << "Error: avformat_open_input failed." << std::endl;
        exit(-1);
    }

    /**
     * 解析输入文件的音视频流信息
     * 遍历输入文件的所有媒体流，针对每一路音频流、视频流、字幕流打开对应的解码器
     * 读取部分数据进行解码，同时将解码过程的多个参数保存到AVFormatContext的AVFormat成员中
     */
    result = avformat_find_stream_info(format_ctx, nullptr);
    if (result < 0) {
        std::cerr << "Error: avformat_find_stream_info failed." << std::endl;
        exit(-1);
    }

    // 选择最佳的一路视频流
    result = open_codec_context(&video_stream_index, &video_dec_ctx, format_ctx,
                                AVMEDIA_TYPE_VIDEO);
    if (result >= 0) {
        video_stream = format_ctx->streams[video_stream_index];
        output_video_file = fopen(video_output_name, "wb");
        if (!output_video_file) {
            std::cerr << "Error: failed to open video output file." << std::endl;
            return -1;
        }
    }
    // 选择最佳的一路音频流
    result = open_codec_context(&audio_stream_index, &audio_dec_ctx, format_ctx,
                                AVMEDIA_TYPE_AUDIO);
    if (result >= 0) {
        audio_stream = format_ctx->streams[audio_stream_index];
        output_audio_file = fopen(audio_output_name, "wb");
        if (!output_audio_file) {
            std::cerr << "Error: failed to open audio output file." << std::endl;
            return -1;
        }
    }

    /* dump input information to stderr */
    av_dump_format(format_ctx, 0, input_name, 0);

    if (!audio_stream && !video_stream) {
        std::cerr
                << "Error: Could not find audio or video stream in the input, aborting "
                << std::endl;
        return -1;
    }

    // 使用默认值初始化packet的可选字段
    pkt = av_packet_alloc();
//    av_init_packet(pkt);
//    pkt->data = NULL;
//    pkt->size = 0;

    frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "Error: Failed to alloc frame." << std::endl;
        return -1;
    }

    if (video_stream) {
        std::cout << "Demuxing video from file " << std::string(input_name)
                  << " into " << std::string(video_output_name) << std::endl;
    }
    if (audio_stream) {
        std::cout << "Demuxing audio from file " << std::string(input_name)
                  << " into " << std::string(audio_output_name) << std::endl;
    }

    return 0;
}

/**
 * 将解码后的 frame 保存成yuv422格式
 */
static int32_t write_frame_to_yuv1(AVFrame *frame) {
    uint8_t **pBuf = frame->data;
    int *pStride = frame->linesize;
    for (size_t i = 0; i < 3; i++) {
        int32_t width = (i == 0 ? frame->width : frame->width / 2);
        int32_t height = (i == 0 ? frame->height : frame->height / 2);
        for (size_t j = 0; j < height; j++) {
            fwrite(pBuf[i], 1, width, output_video_file);
            pBuf[i] += pStride[i];
        }
    }
    return 0;
}

/**
 * 保存解码后的音频，每个声道交叉存储 packed 格式
 */
static int32_t write_samples_to_pcm1(AVFrame *frame,
                                     AVCodecContext *codec_ctx) {
    int data_size = av_get_bytes_per_sample(codec_ctx->sample_fmt);
    if (data_size < 0) {
        /* This should not occur, checking just for paranoia */
        std::cerr << "Failed to calculate data size" << std::endl;
        exit(1);
    }
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < codec_ctx->ch_layout.nb_channels; ch++) {
            fwrite(frame->data[ch] + data_size * i, 1, data_size, output_audio_file);
        }
    }
    return 0;
}

/**
 * 解码 packet，还是常规那一套，send_packet->receive_frame
 */
static int32_t decode_packet(AVCodecContext *dec, const AVPacket *pkt) {
    int32_t result = 0;
    result = avcodec_send_packet(dec, pkt);
    if (result < 0) {
        std::cerr << "Error: avcodec_send_packet failed." << std::endl;
        return result;
    }

    while (result >= 0) {
        result = avcodec_receive_frame(dec, frame);
        if (result < 0) {
            if (result == AVERROR_EOF || result == AVERROR(EAGAIN)) return 0;

            std::cerr << "Error:Error during decoding" << std::endl;
            return result;
        }

        if (dec->codec->type == AVMEDIA_TYPE_VIDEO) {
            write_frame_to_yuv1(frame);
            std::cout << "Write frame to yuv file" << std::endl;
        } else {
            write_samples_to_pcm1(frame, audio_dec_ctx);
            std::cout << "Write sample to pcm file" << std::endl;
        }

        av_frame_unref(frame);
    }

    return result;
}

/**
 * 根据 sample_fmt 获取 fmt
 */
static int get_format_from_sample_fmt(const char **fmt,
                                      enum AVSampleFormat sample_fmt) {
    int i;
    struct sample_fmt_entry {
        enum AVSampleFormat sample_fmt;
        const char *fmt_be, *fmt_le;
    } sample_fmt_entries[] = {
            {AV_SAMPLE_FMT_U8,  "u8",    "u8"},
            {AV_SAMPLE_FMT_S16, "s16be", "s16le"},
            {AV_SAMPLE_FMT_S32, "s32be", "s32le"},
            {AV_SAMPLE_FMT_FLT, "f32be", "f32le"},
            {AV_SAMPLE_FMT_DBL, "f64be", "f64le"},
    };
    *fmt = NULL;

    for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
        struct sample_fmt_entry *entry = &sample_fmt_entries[i];
        if (sample_fmt == entry->sample_fmt) {
            *fmt = AV_NE(entry->fmt_be, entry->fmt_le);
            return 0;
        }
    }

    std::cerr << "sample format %s is not supported as output format\n"
              << av_get_sample_fmt_name(sample_fmt) << std::endl;
    return -1;
}

/**
 * 解封装，
 */
int32_t demuxing(char *video_output_name, char *audio_output_name) {
    int32_t result = 0;

    // 读取文件中的下一个packet，保存到pkt中，pkt引用计数+1，需要av_packet_unref释放
    // 使用合适的编解码器解码packet为frame，保存到文件
    while (av_read_frame(format_ctx, pkt) >= 0) {
        std::cout << "Read packet, pts:" << pkt->pts
                  << ", stream:" << pkt->stream_index << ", size:" << pkt->size
                  << std::endl;
        if (pkt->stream_index == audio_stream_index) {
            result = decode_packet(audio_dec_ctx, pkt);
        } else if (pkt->stream_index == video_stream_index) {
            result = decode_packet(video_dec_ctx, pkt);
        }
        av_packet_unref(pkt);
        if (result < 0) {
            break;
        }
    }

    /* flush the decoders */
    // 传入空指针packet，刷新解码器
    if (video_dec_ctx) decode_packet(video_dec_ctx, nullptr);
    if (audio_dec_ctx) decode_packet(audio_dec_ctx, nullptr);

    std::cout << "Demuxing succeeded." << std::endl;
    if (video_dec_ctx) {
        std::cout << "Play the output video file with the command:" << std::endl
                  << "   ffplay -f rawvideo -pixel_format "
                  << std::string(av_get_pix_fmt_name(video_dec_ctx->pix_fmt))
                  << " -video_size " << video_dec_ctx->width << "x"
                  << video_dec_ctx->height << " " << std::string(video_output_name)
                  << std::endl;
    }
    if (audio_dec_ctx) {
        // 这里获取的是 AV_SAMPLE_FMT_FLTP 格式，表示 float planar
        enum AVSampleFormat sfmt = audio_dec_ctx->sample_fmt;
        int n_channels = audio_dec_ctx->ch_layout.nb_channels;
        const char *fmt;

        // 这个就有点扯了，虽然audio在封装后的数据中是sfmt(planar)格式
        // 但是pcm文件写入的方式是packed格式，所以播放的时候还是要按照packet格式播放
//        if (av_sample_fmt_is_planar(sfmt)) {
//            const char *packed = av_get_sample_fmt_name(sfmt);
//            sfmt = av_get_packed_sample_fmt(sfmt);
//            n_channels = 1;
//        }
        // 这里改成对应的packed格式，因为我们输出的pcm文件是按照packed格式保存的
        sfmt = av_get_packed_sample_fmt(sfmt);
        result = get_format_from_sample_fmt(&fmt, sfmt);
        if (result < 0) {
            return -1;
        }
        std::cout << "Play the output video file with the command:" << std::endl
                  << "    ffplay -f " << std::string(fmt) << " -ac " << n_channels
                  << " -ar " << audio_dec_ctx->sample_rate << " "
                  << std::string(audio_output_name) << std::endl;
    }
    return 0;
}

void destroy_demuxer() {
    avcodec_free_context(&video_dec_ctx);
    avcodec_free_context(&audio_dec_ctx);
    avformat_close_input(&format_ctx);
    av_packet_free(&pkt);
    if (output_video_file != nullptr) {
        fclose(output_video_file);
        output_video_file = nullptr;
    }
    if (output_audio_file != nullptr) {
        fclose(output_audio_file);
        output_audio_file = nullptr;
    }
}
