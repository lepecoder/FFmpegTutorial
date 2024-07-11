//
// Created by lxp on 2024/7/7.
//

/**
 * 简单的视频播放器，只能解码播放视频，不能播放音频，没有倍速和快进快退
 * 读取packet后解码成frame，使用frame->data的YUV数据更新 texture
 *
 * 使用read_thread()读取文件解封装AVPacket，放入PacketQueue
 * 使用video_thread()从PacketQueue videoq队列拿AVPacket解码成AVFrame到添加到FrameQueue
 * TODO：
 * 播放音频
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
#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000 //channels(2) * data_size(2) * sample_rate(48000)

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

#define VIDEO_PICTURE_QUEUE_SIZE 1

typedef struct PacketQueue {
    // 从last_pkt插入，从first_pkt取出
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

typedef struct VideoPicture {
    AVFrame *frame;
    int width, height; /* source height & width */
    double pts;
} VideoPicture;

typedef struct VideoState {
    char filename[1024];
    AVFormatContext *pFormatCtx;
    int videoStream, audioStream;

    double audio_clock;
    double frame_timer;
    double frame_last_pts;
    double frame_last_delay;

    double video_clock; ///<pts of last decoded frame / predicted pts of next decoded frame
    double video_current_pts; ///<current displayed pts (different from video_clock if frame fifos are used)
    int64_t video_current_pts_time;  ///<time (av_gettime) at which we updated video_current_pts - used to have running video pts

    //audio
    AVStream *audio_st;
    AVCodecContext *audio_ctx;
    PacketQueue audioq;
    uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    unsigned int audio_buf_size;
    unsigned int audio_buf_index;
    AVFrame audio_frame;

    AVPacket audio_pkt;
    uint8_t *audio_pkt_data;
    int audio_pkt_size;
    int audio_hw_buf_size;
    struct SwrContext *audio_swr_ctx;

    //video
    AVStream *video_st;
    AVCodecContext *video_ctx;
    PacketQueue videoq;

    VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
    int pictq_size, pictq_rindex , pictq_windex;
    SDL_mutex *pictq_mutex;
    SDL_cond *pictq_cond;

    SDL_Thread *parse_tid;
    SDL_Thread *video_tid;

    int quit;
} VideoState;

VideoState *global_video_state;

//// 初始化队列
void packet_queue_init(PacketQueue *q) {
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

    AVPacketList *pkt1;
    if (av_packet_make_refcounted(pkt) < 0) {
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

    while (true) {

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
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}


/**
 * 视频解码线程
 * 入参 VideoState，VideoState里保存视频所有相关状态
 * 解复用，获取音频、视频流，并将packet放入队列中
 */
int demux_thread(void *arg) {

    int err_code;
    char errors[1024] = {0,};

    int w, h;

    VideoState *is = (VideoState *) arg;
    AVFormatContext *pFormatCtx = NULL;
    AVPacket pkt1, *packet = &pkt1;
    const AVCodec *videoCodec;

    int video_index = -1;
    int audio_index = -1;
    int i;

    is->videoStream = -1;
    is->audioStream = -1;

    global_video_state = is;

    /* open input file, and allocate format context */
    if ((err_code = avformat_open_input(&pFormatCtx, is->filename, NULL, NULL)) < 0) {
        av_strerror(err_code, errors, 1024);
        fprintf(stderr, "Could not open source file %s, %d(%s)\n", is->filename, err_code, errors);
        return -1;
    }

    is->pFormatCtx = pFormatCtx;

    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
        return -1; // Couldn't find stream information

    // Dump information about file onto standard error
//    av_dump_format(pFormatCtx, 0, is->filename, 0);

    // Find the first video stream
    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            video_index < 0) {
            video_index = i;
        }
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
            audio_index < 0) {
            audio_index = i;
        }
    }

//    if (audio_index >= 0) {
//        stream_component_open(is, audio_index);
//    }
//    if (video_index >= 0) {
//        stream_component_open(is, video_index);
//    }
    packet_queue_init(&is->videoq);

    if (is->videoStream < 0 || is->audioStream < 0) {
        fprintf(stderr, "%s: could not open codecs\n", is->filename);
        goto fail;
    }

    while (true) {
        if (is->quit) {
            break;
        }
        // seek stuff goes here
        if (is->audioq.size > MAX_AUDIOQ_SIZE ||
            is->videoq.size > MAX_VIDEOQ_SIZE) {
            SDL_Delay(10);
            continue;
        }
        if (av_read_frame(is->pFormatCtx, packet) < 0) {
            if (is->pFormatCtx->pb->error == 0) {
                SDL_Delay(100); /* no error; wait for user input */
                continue;
            } else {
                break;
            }
        }
        // Is this a packet from the video stream?
        if (packet->stream_index == is->videoStream) {
            packet_queue_put(&is->videoq, packet);
        } else if (packet->stream_index == is->audioStream) {
//            packet_queue_put(&is->audioq, packet);
        } else {
            av_packet_unref(packet);
        }
    }
    /* all done - wait for it */
    while (!is->quit) {
        SDL_Delay(100);
    }

    fail:
    if (1) {
        SDL_Event event;
        event.type = FF_QUIT_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);
    }
    return 0;
}

//// 视频解码
int decode_video_thread(void *arg) {
    VideoState *is = (VideoState *) arg;
    AVPacket pkt1, *packet = &pkt1;
    AVFrame *pFrame;
    double pts;

    pFrame = av_frame_alloc();

    for (;;) {
        if (packet_queue_get(&is->videoq, packet, 1) < 0) {
            // means we quit getting packets
            break;
        }

        // Decode video frame
        avcodec_send_packet(is->video_ctx, packet);
        while (avcodec_receive_frame(is->video_ctx, pFrame) == 0) {
            if ((pts = pFrame->best_effort_timestamp) != AV_NOPTS_VALUE) {
            } else {
                pts = 0;
            }
            pts *= av_q2d(is->video_st->time_base);


            av_packet_unref(packet);
        }
    }
    av_frame_free(&pFrame);
    return 0;
}


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
//    exit(0);

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

