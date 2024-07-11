//
// Created by lxp on 2024/7/7.
//

/**
 * 简单的视频播放器，只能解码播放视频，不能播放音频，没有倍速和快进快退
 * 读取packet后解码成frame，使用frame->data的YUV数据更新 texture
 * TODO：
 * 播放音频
 * 使用额外的线程处理 packet和frame队列，避免主线程解码耗时影响播放
 */

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <SDL.h>

#include <iostream>
#include <chrono>



//Refresh Event
#define REFRESH_EVENT  (SDL_USEREVENT + 1)
#define BREAK_EVENT  (SDL_USEREVENT + 2)


static bool appQuit = false;
static bool appPause = false;
static uint32_t delayMs = 0;

int refresh_video(void *opaque) {
    while (!appQuit) {
        if (!appPause) {
            SDL_Event event;
            event.type = REFRESH_EVENT;
            SDL_PushEvent(&event);
        }
        SDL_Delay(delayMs);
    }
    return 0;
}


int main(int argc, char *args[]) {

    const char INPUT_FILE[] = "88.mp4";
    // FFmpeg
    AVFormatContext *pFormatCtx;
    const AVCodec *videoCodec;
    AVCodecContext *videoCodecCtx;
    AVFrame *pFrame;
    AVPacket *pkt;
    int ret;
    int videoStreamIdx = -1;
    AVStream *videoStream = nullptr;
    AVCodecParserContext *parserCtx = nullptr;

    // 打开视频文件
    if (avformat_open_input(&pFormatCtx, INPUT_FILE, nullptr, nullptr) < 0) {
        std::cerr << "Error! open input file! " << std::endl;
        return -1;
    }

    // 获取文件信息
    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        std::cerr << "find stream info error!" << std::endl;
        return -1;
    }

    // 分离视频流
    videoStreamIdx = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO,
                                         -1, -1, &videoCodec, 0);
    if (videoStreamIdx < 0) {
        std::cerr << "find best video stream error! " << std::endl;
        return -1;
    }
    parserCtx = av_parser_init(videoCodec->id);
    videoStream = pFormatCtx->streams[videoStreamIdx];

    videoCodecCtx = avcodec_alloc_context3(videoCodec);
    if (videoCodecCtx == nullptr) {
        std::cerr << "alloc context3 error!" << std::endl;
        return -1;
    }

    // 根据 AVCodecParameters 填充 AVCodecContext
    if (avcodec_parameters_to_context(videoCodecCtx, videoStream->codecpar) < 0) {
        std::cerr << "Error: Failed to copy codec parameters to decoder context."
                  << std::endl;
        return -1;
    }

    // 初始化AVCodecContext
    if (avcodec_open2(videoCodecCtx, videoCodec, nullptr) < 0) {
        std::cerr << "Error: AVCodec open failed!" << std::endl;
        exit(-1);
    }

    // 打印文件信息
    av_dump_format(pFormatCtx, 0, INPUT_FILE, 0);

    pkt = av_packet_alloc();
    pFrame = av_frame_alloc();
    if (pkt == nullptr || pFrame == nullptr) {
        std::cerr << "Error! pkt or pFrame alloc failed!" << std::endl;
        exit(-1);
    }

    /**
     * 展示SDL窗口
     */

    int screen_w = videoStream->codecpar->width;
    int screen_h = videoStream->codecpar->height;

    delayMs = (uint32_t)(1000.0 / av_q2d(videoStream->avg_frame_rate));

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Window *window = SDL_CreateWindow("Blank Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                          screen_w, screen_h, SDL_WINDOW_SHOWN);

    if (window == nullptr) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }

    auto renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    if (renderer == nullptr) {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV,
                                                             SDL_TEXTUREACCESS_STREAMING, screen_w, screen_h);

    // 开启独立线程发送refresh event
    SDL_Thread *refresh_thread = SDL_CreateThread(refresh_video, nullptr, nullptr);
    SDL_Event event;

    std::chrono::high_resolution_clock::time_point start, end1, end2;
    start=end1=end2 = std::chrono::high_resolution_clock::now();

    // 读取文件中的下一个packet，保存到pkt中，pkt引用计数+1，需要av_packet_unref释放
    // 使用合适的编解码器解码packet为frame，保存到文件
    while (av_read_frame(pFormatCtx, pkt) >= 0 && !appQuit) {
        if (pkt->stream_index != videoStreamIdx) continue;

        // 将packet送入解码器，由于B帧的存在，送入packet后可能无法解码，此时receive_frame返回AVERROR(EAGAIN)
        // 应当再送入packet，之后从解码器可能取出多个frame，直到AVERROR_EOF或是AVERROR(EAGAIN)
        ret = avcodec_send_packet(videoCodecCtx, pkt);
        if (ret < 0) {
            std::cerr << "Error! decode packet failed!" << std::endl;
            exit(-1);
        }
        while (!appQuit) {
            // 帧率很不稳定，加载大画面很卡
            ret = avcodec_receive_frame(videoCodecCtx, pFrame);
            if (ret < 0) {
                if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) break;
                else {
                    std::cerr << "receive_frame return " << ret << std::endl;
                    return -1;
                }
            }

            SDL_WaitEvent(&event);
            if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_q:
                        appQuit = true;
                        exit(0);
                        break;
                    case SDLK_SPACE:
                        appPause = !appPause;
                        break;
                }
            } else if (event.type == REFRESH_EVENT) { // 刷新画面
                SDL_UpdateYUVTexture(texture, nullptr,
                                     pFrame->data[0], pFrame->linesize[0],
                                     pFrame->data[1], pFrame->linesize[1],
                                     pFrame->data[2], pFrame->linesize[2]);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, nullptr, nullptr);
                SDL_RenderPresent(renderer);
                end1 = end2;
                end2 = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end2 - end1);
                auto nowTime = std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start);

                std::cout << "pts: " << pFrame->pts << ", now time: " << nowTime.count() << ", duration: " << duration.count() << std::endl;

            }


            av_frame_unref(pFrame);
        }
        av_packet_unref(pkt);
    }


    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    av_packet_free(&pkt);
    av_frame_free(&pFrame);
    return 0;
}

