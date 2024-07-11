//
// Created by lxp on 2024/7/7.
//

/**
 * 简单的视频播放器，只能解码播放视频，不能播放音频，没有倍速和快进快退
 * 读取packet后解码成frame，使用frame->data的YUV数据更新 texture
 * 使用read_thread()解封装获取AVPacket放入PacketQueue
 * TODO：
 * 使用video_thread()从PacketQueue获取AVPacket解码成AVFrame放入FrameQueue
 */

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <SDL.h>

#include <iostream>
#include <chrono>
#include <libavutil/fifo.h>
#define FRAME_QUEUE_MAX_SIZE 32

typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

/**
 * AVFrame并不直接包含数据，而是包含使用data指针指向数据，所有可以用
 * 一个环形队列表示，循环利用AVFrame，更新环形队列指针
 */
typedef struct FrameQueue {
    AVFrame frames[FRAME_QUEUE_MAX_SIZE];
    int rindex;  // read index
    int windex;  // write index
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} FrameQueue;

/**
 * 主要视频文件数据结构
 */
typedef struct VideoState {
    char filename[1024];
    AVFormatContext *pFormatCtx;
    bool quit=false; SDL_mutex *pictq_mutex;

    SDL_cond *pictq_cond;
    SDL_Thread *parse_tid;
    SDL_Thread *video_tid;


    //video
    int videoStreamIdx;
    AVStream *videoSt;
    AVCodecContext *videoCodecCtx;
    PacketQueue videoq;



}VideoState;

//Refresh Event
#define REFRESH_EVENT  (SDL_USEREVENT + 1)
#define BREAK_EVENT  (SDL_USEREVENT + 2)

static int MAX_VIDEOQ_SIZE = 5 * 256 * 1024;
static VideoState *global_video_state;
static bool appQuit = false;
static bool appPause = false;
static uint32_t delayMs = 0;
static int screen_w, screen_h;
static SDL_cond *read_cond;
static SDL_mutex *read_mtx;

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

/**
 * 初始化FrameQueue
 */
int frame_queue_init() {

}

int frame_queue_put() {

}

int frame_queue_get() {

}

//// 初始化队列
void packet_queue_init(PacketQueue *q) {
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

    AVPacketList *pkt1;
    if (pkt!= nullptr && av_packet_make_refcounted(pkt) < 0) {
        return -1;
    }
    pkt1 = (AVPacketList*)av_malloc(sizeof(AVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    SDL_LockMutex(q->mutex);

    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;
    SDL_CondSignal(q->cond);
    SDL_UnlockMutex(q->mutex);
    return 0;
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
    AVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;) {

        if (global_video_state->quit) {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            *pkt = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            std::cout << "cond1, ";
            if (SDL_CondWaitTimeout(q->cond, q->mutex, 200) == SDL_MUTEX_TIMEDOUT) {
                return -2;
            }
            std::cout << "cond2 ";
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}


/**
 * 读取AVPacket放入队列中
 * 传入VideoState
 *
 */
int read_thread(void* arg) {
    auto is = (VideoState*) arg;

    // 打开视频文件
    if (avformat_open_input(&is->pFormatCtx, is->filename, nullptr, nullptr) < 0) {
        std::cerr << "Error! open input file! " << std::endl;
        return -1;
    }

    // 获取文件信息
    if (avformat_find_stream_info(is->pFormatCtx, nullptr) < 0) {
        std::cerr << "find stream info error!" << std::endl;
        return -1;
    }

    const AVCodec *videoCodec;
    // 分离视频流
    is->videoStreamIdx = av_find_best_stream(is->pFormatCtx, AVMEDIA_TYPE_VIDEO,
                                         -1, -1, &videoCodec, 0);
    if (is->videoStreamIdx < 0) {
        std::cerr << "find best video stream error! " << std::endl;
        return -1;
    }
    auto parserCtx = av_parser_init(videoCodec->id);
    is->videoSt = is->pFormatCtx->streams[is->videoStreamIdx];

    is->videoCodecCtx = avcodec_alloc_context3(videoCodec);
    if (is->videoCodecCtx == nullptr) {
        std::cerr << "alloc context3 error!" << std::endl;
        return -1;
    }
    // 获取视频信息
    screen_w = is->videoSt->codecpar->width;
    screen_h = is->videoSt->codecpar->height;
    delayMs = (uint32_t)(1000.0 / av_q2d(is->videoSt->avg_frame_rate));
    SDL_LockMutex(read_mtx);
    SDL_CondSignal(read_cond);
    SDL_UnlockMutex(read_mtx);

    // 根据 AVCodecParameters 填充 AVCodecContext
    if (avcodec_parameters_to_context(is->videoCodecCtx, is->videoSt->codecpar) < 0) {
        std::cerr << "Error: Failed to copy codec parameters to decoder context."
                  << std::endl;
        return -1;
    }

    // 初始化AVCodecContext
    if (avcodec_open2(is->videoCodecCtx, videoCodec, nullptr) < 0) {
        std::cerr << "Error: AVCodec open failed!" << std::endl;
        exit(-1);
    }

    // 打印文件信息
    av_dump_format(is->pFormatCtx, 0, is->filename, 0);

    auto pkt = av_packet_alloc();

    while (!appQuit) {
        if (is->videoq.size >= MAX_VIDEOQ_SIZE) {
            SDL_Delay(10);
            continue;
        }

        if (int ret = av_read_frame(is->pFormatCtx, pkt) < 0) {
            break;
        }
        if (pkt->stream_index == is->videoStreamIdx) {
            packet_queue_put(&is->videoq, pkt);
        } else {
            av_packet_unref(pkt);
        }
    }
    return 0;
}

int main(int argc, char *args[]) {

    const char INPUT_FILE[] = "88.mp4";
    read_mtx = SDL_CreateMutex();
    read_cond = SDL_CreateCond();
    auto *is = new VideoState;
    global_video_state = is;
    strlcpy(is->filename, INPUT_FILE, sizeof(is->filename));

    // 创建解复用线程
    packet_queue_init(&is->videoq);
    is->parse_tid = SDL_CreateThread(read_thread, "read_thread", is);

    // FFmpeg
    AVFrame *pFrame;
    AVPacket *pkt;
    int ret;

    pFrame = av_frame_alloc();
    pkt = av_packet_alloc();


    // 等待读取视频的宽高和帧率
    SDL_LockMutex(read_mtx);
    SDL_CondWait(read_cond, read_mtx);
    SDL_UnlockMutex(read_mtx);
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
    while (!appQuit) {
        // 从队列获取packet

        if (packet_queue_get(&is->videoq, pkt, 1) < 0) {
            break;
        }
        std::cout << "pkt == nullptr " << int(pkt==nullptr) << std::endl;
        if (pkt == nullptr) break;

        // 将packet送入解码器，由于B帧的存在，送入packet后可能无法解码，此时receive_frame返回AVERROR(EAGAIN)
        // 应当再送入packet，之后从解码器可能取出多个frame，直到AVERROR_EOF或是AVERROR(EAGAIN)
        ret = avcodec_send_packet(is->videoCodecCtx, pkt);
        if (ret < 0) {
            std::cerr << "Error! decode packet failed!" << std::endl;
            exit(-1);
        }
        while (!appQuit) {
            // 帧率很不稳定，加载大画面很卡
            ret = avcodec_receive_frame(is->videoCodecCtx, pFrame);
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

    if (pkt != nullptr) {
        av_packet_free(&pkt);
    }
    if (pFrame != nullptr) {
        av_frame_free(&pFrame);
    }
    return 0;
}

