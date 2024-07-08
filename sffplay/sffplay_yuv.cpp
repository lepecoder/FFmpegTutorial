//
// Created by lxp on 2024/7/7.
//

/**
 * 只播放yuv格式数据的播放器

初始化SDL: 首先，你需要初始化SDL库。
创建窗口: 使用SDL_CreateWindow创建一个窗口。
加载YUV图像: 使用loadYUV函数加载YUV图像。
创建表面: 使用SDL_CreateRGBSurface创建一个表面，用于将YUV图像转换为RGBA格式。
转换YUV到RGBA: 读取YUV图像数据转换到创建的RGBA表面。
创建纹理: 使用SDL_CreateTextureFromSurface将RGB表面转换为纹理。
渲染纹理到窗口: 使用SDL_RenderCopy将纹理渲染到窗口。
事件处理: 处理用户输入，例如关闭窗口, 暂停，更新纹理
使用RGBA像素信息更新texture。
清理资源: 在退出程序前，释放所有分配的资源。

 SDL_image 没有直接加载 YUV 文件的方法，需要手动读取 YUV 文件，转换成 RGB 图像。
 */

#include <SDL.h>

#include <iostream>
#include <fstream>


//Refresh Event
#define REFRESH_EVENT  (SDL_USEREVENT + 1)
#define BREAK_EVENT  (SDL_USEREVENT + 2)

bool appQuit = false;
bool appPause = false;

int refresh_video(void *opaque) {
    while (!appQuit) {
        if (!appPause) {
            SDL_Event event;
            event.type = REFRESH_EVENT;
            SDL_PushEvent(&event);
        }
        SDL_Delay(25);
    }
    return 0;
}

/**
 * 读取 YUV420p 格式文件
 * 每一帧图像的大小是 width*height*1.5
 */
SDL_Surface *loadYUV(const char *filePath, int width, int height) {
    // 创建一个RGBA颜色surface，每一个颜色用8bit表示，一共32bit 4byte
    SDL_Surface *surface = SDL_CreateRGBSurface(
            0,  // 表面标志，0表示默认
            width,
            height,
            32, // 每个像素的位数，RGBA32是32位
            0x00FF0000, // 红色掩码
            0x0000FF00, // 绿色掩码
            0x000000FF, // 蓝色掩码
            0xFF000000  // Alpha掩码
    );

    if (surface == nullptr) {
        std::cerr << "create RGB surface error!! " << std::endl;
        return nullptr;
    }

    int ySize = width * height;
    int uvSize = width * height / 4;
    int frameSize = width * height * 1.5;
    uint8_t *yData = new uint8_t[ySize];
    uint8_t *uData = new uint8_t[uvSize];
    uint8_t *vData = new uint8_t[uvSize];

    // 打开YUV文件
    static std::ifstream fp(filePath, std::ios::binary);
    if (!fp) {
        std::cerr << "fp seek error! " << std::endl;
        fp.close();
        return nullptr;
    }
    fp.read((char *) yData, ySize);
    fp.read((char *) uData, uvSize);
    fp.read((char *) vData, uvSize);
    fp.seekg(frameSize * 1, std::ios::cur);

    // 填充像素
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int yIndex = y * width + x;
            int uvIndex = (y / 2) * (width / 2) + (x / 2);

            // YUV to RGB转换公式（简化版）
            int yValue = yData[yIndex];
            int uValue = uData[uvIndex] - 128;
            int vValue = vData[uvIndex] - 128;
            int r = yValue + (1.0 * vValue);
            int g = yValue - (0.344 * uValue) - (0.714 * vValue);
            int b = yValue + (1.772 * uValue);
            // 确保值在0-255范围内
            r = (r < 0) ? 0 : (r > 255 ? 255 : r);
            g = (g < 0) ? 0 : (g > 255 ? 255 : g);
            b = (b < 0) ? 0 : (b > 255 ? 255 : b);

            // pitch 是每一行的字节数, 720*3=2160, 每一行有2160个byte
            uint32_t *pixel = reinterpret_cast<uint32_t *>((char *) (surface->pixels) + y * surface->pitch + x * 4);
            *pixel = (r << 16) | (g << 8) | b;
            *pixel |= 0xFF000000;  // 填充 alpha 通道
        }
    }
    return surface;
}


int main(int argc, char *args[]) {

    const char INPUT_FILE[] = "example_720x720.yuv";
    const int WIDTH = 720;
    const int HEIGHT = 720;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Window *window = SDL_CreateWindow("Blank Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                          WIDTH, HEIGHT, SDL_WINDOW_SHOWN);

    if (window == nullptr) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    if (renderer == nullptr) {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }
    SDL_Surface *imageSurface = loadYUV(INPUT_FILE, WIDTH, HEIGHT);
    if (imageSurface == nullptr) {
        std::cerr << "Failed to load image! SDL_Error: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_Texture *imageTexture = SDL_CreateTextureFromSurface(renderer, imageSurface);
    SDL_FreeSurface(imageSurface);
    SDL_Rect sdlRect;
    sdlRect.x = 0, sdlRect.y = 0, sdlRect.w = 720, sdlRect.h = 720;

    SDL_RenderCopy(renderer, imageTexture, nullptr, nullptr);  // 将texture复制到renderer
    SDL_RenderPresent(renderer);  // renderer上屏

    // 开启独立线程发送refresh event
    SDL_Thread *refresh_thread = SDL_CreateThread(refresh_video, nullptr, nullptr);

    SDL_Event event;
    while (!appQuit) {
        SDL_WaitEvent(&event);
        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_q:
                    appQuit = true;
                    break;
                case SDLK_SPACE:
                    appPause = !appPause;
                    break;
            }
        } else if (event.type == REFRESH_EVENT) { // 刷新画面
            auto surface = loadYUV(INPUT_FILE, WIDTH, HEIGHT);
            if (surface == nullptr) {
                break;
            }
            SDL_UpdateTexture(imageTexture, &sdlRect, surface->pixels, surface->pitch);
            SDL_RenderClear(renderer);  // 清空renderer
            SDL_RenderCopy(renderer, imageTexture, nullptr, nullptr);  // 将texture复制到renderer
            SDL_RenderPresent(renderer);  // renderer上屏

        }
    }

    SDL_DestroyTexture(imageTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

