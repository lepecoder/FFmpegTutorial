#include "video_swscale_core.h"

#include <stdlib.h>
#include <string.h>

#include <iostream>

#include "io_data.h"

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/parseutils.h>
#include <libswscale/swscale.h>
}

static AVFrame *input_frame = nullptr;
static struct SwsContext *sws_ctx;
static int32_t src_width = 0, src_height = 0, dst_width = 0, dst_height = 0;
static enum AVPixelFormat src_pix_fmt = AV_PIX_FMT_NONE,
        dst_pix_fmt = AV_PIX_FMT_NONE;

/**
 * 初始化AVFrame，设置AVFrame的宽高和格式信息，分配data和buf字段，并确保data可写入
 */
static int32_t init_frame(int32_t width, int32_t height,
                          enum AVPixelFormat pix_fmt) {
    int result = 0;
    input_frame = av_frame_alloc();
    if (!input_frame) {
        std::cerr << "Error: frame allocation failed." << std::endl;
        return -1;
    }

    input_frame->width = width;
    input_frame->height = height;
    input_frame->format = pix_fmt;

    // 为 AVFormat 分配data和buf数组，必须先设置format和width height
    result = av_frame_get_buffer(input_frame, 0);
    if (result < 0) {
        std::cerr << "Error: could not get AVFrame buffer." << std::endl;
        return -1;
    }

    // AVFrame里data字段是对数据区的一个指针，可能数据区并不是可写入的，此时调用 av_frame_make_writeable
    // 会将data区域拷贝到一个可写区域，更新data指针，确保frame的数据可写入，比如no-refcounted frame总是不可写的
    result = av_frame_make_writable(input_frame);
    if (result < 0) {
        std::cerr << "Error: input frame is not writable." << std::endl;
        return -1;
    }
    return 0;
}

/**
 * libswscale 图像格式转换、颜色空间转换、缩放
 */
int32_t init_video_swscale(char *src_size, char *src_fmt, char *dst_size,
                           char *dst_fmt) {
    int32_t result = 0;

    // 解析输入视频和输出视频的图像尺寸
    result = av_parse_video_size(&src_width, &src_height, src_size);
    if (result < 0) {
        std::cerr << "Error: Invalid input size. Must be in the form WxH or a "
                     "valid size abbreviation.Input : "
                  << std::string(src_size) << std::endl;
        return -1;
    }
    // 解析目标尺寸
    result = av_parse_video_size(&dst_width, &dst_height, dst_size);
    if (result < 0) {
        std::cerr << "Error: Invalid output size. Must be in the form WxH or a "
                     "valid size abbreviation.Input : "
                  << std::string(src_size) << std::endl;
        return -1;
    }

    // 选择输入视频和输出视频的图像格式
    if (!strcasecmp(src_fmt, "YUV420P")) {
        src_pix_fmt = AV_PIX_FMT_YUV410P;
    } else if (!strcasecmp(src_fmt, "RGB24")) {
        src_pix_fmt = AV_PIX_FMT_RGB24;
    } else {
        std::cerr << "Error: Unsupported input pixel format:"
                  << std::string(src_fmt) << std::endl;
        return -1;
    }

    // 解析输出媒体格式
    if (!strcasecmp(dst_fmt, "YUV420P")) {
        dst_pix_fmt = AV_PIX_FMT_YUV410P;
    } else if (!strcasecmp(dst_fmt, "RGB24")) {
        dst_pix_fmt = AV_PIX_FMT_RGB24;
    } else {
        std::cerr << "Error: Unsupported output pixel format:"
                  << std::string(dst_fmt) << std::endl;
        return -1;
    }

    // 获取SwsContext结构，传出源宽高、源格式，输出宽高、输出格式
    // TODO 查看SwsContext
    sws_ctx =
            sws_getContext(src_width, src_height, src_pix_fmt, dst_width, dst_height,
                           dst_pix_fmt, SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_ctx) {
        std::cerr << "Error: failed to get SwsContext." << std::endl;
        return -1;
    }

    // 初始化AVFrame结构
    result = init_frame(src_width, src_height, src_pix_fmt);
    if (result < 0) {
        std::cerr << "Error: failed to initialize input frame." << std::endl;
        return -1;
    }

    return result;
}

/**
 * 视频格式转换，处理前frame_cnt个frame
 * 1. 分配image数组，dst_data
 * 2. 读取yuv转换成frame
 * 3. sws_scale库函数转换，传入input_frame，结果保存在dst_data里
 */
int32_t transforming(int32_t frame_cnt) {
    int32_t result = 0;
    uint8_t *dst_data[4];
    int32_t dst_linesize[4] = {0}, dst_bufsize = 0;

    // 按照宽高和format分配一张图片的buffer到pointers里，buffer的宽是linesize，需要大于图片的宽
    result = av_image_alloc(dst_data, dst_linesize, dst_width, dst_height,
                            dst_pix_fmt, 1);
    if (!result) {
        std::cerr << "Error: failed to alloc output frame buffer." << std::endl;
        return -1;
    }

    dst_bufsize = result;
    for (int idx = 0; idx < frame_cnt; idx++) {
        // frame里保存了宽高和linesize信息
        result = read_yuv_to_frame(input_frame);
        if (result < 0) {
            std::cerr << "Error: read_yuv_to_frame failed." << std::endl;
            return result;
        }
        // 缩放src，缩放参数在SwsContext里，缩放数据在srcSlice里拉平成一维数组，处理一维数组里的Y->H之间的数据
        sws_scale(sws_ctx, input_frame->data, input_frame->linesize, 0, src_height,
                  dst_data, dst_linesize);

        write_packed_data_to_file(dst_data[0], dst_bufsize);
    }

    av_freep(&dst_data[0]);
    return result;
}

void destroy_video_swscale() {
    av_frame_free(&input_frame);
    sws_freeContext(sws_ctx);
}
