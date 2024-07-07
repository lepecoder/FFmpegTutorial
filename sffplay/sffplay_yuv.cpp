//
// Created by lxp on 2024/7/7.
//

/**
 * 只播放yuv格式数据的播放器

初始化SDL: 首先，你需要初始化SDL库。
创建窗口: 使用SDL_CreateWindow创建一个窗口。
加载YUV图像: 使用SDL_LoadYUV函数加载YUV图像。
创建表面: 使用SDL_CreateRGBSurface创建一个表面，用于将YUV图像转换为RGB格式。
转换YUV到RGB: 将YUV图像数据转换到创建的RGB表面。
创建纹理: 使用SDL_CreateTextureFromSurface将RGB表面转换为纹理。
渲染纹理到窗口: 使用SDL_RenderCopy将纹理渲染到窗口。
事件处理: 处理用户输入，例如关闭窗口。
清理资源: 在退出程序前，释放所有分配的资源。

 SDL_image 没有直接加载 YUV 文件的方法，需要手动读取 YUV 文件，转换成 RGB 图像。
 */

#include <SDL.h>
#include <SDL_image.h>

#include <iostream>
#include <fstream>


/**
 * 读取 YUV420p 格式文件
 */
SDL_Surface *loadYUV(const char *filePath, int width, int height) {
    // 创建一个RGB颜色surface，每一个颜色用8bit表示，一共24bit
    SDL_Surface *surface = SDL_CreateRGBSurface(0, width, height, 24,
                                                0x00FF0000, 0x0000FF00, 0x000000FF, 0);
    if (surface == nullptr) {
        std::cerr << "create RGB surface error!! " << std::endl;
        return nullptr;
    }


    int y_size = width*height;
    int uv_size = width*height/4;
    uint8_t *yData = new uint8_t[y_size];
    uint8_t *uData = new uint8_t[uv_size];
    uint8_t *vData = new uint8_t[uv_size];

    // 打开YUV文件
    std::ifstream fp(filePath, std::ios::binary);
    fp.read((char*)yData, y_size);
    fp.read((char*)uData, uv_size);
    fp.read((char*)vData, uv_size);
    fp.close();

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
            uint32_t *pixel = reinterpret_cast<uint32_t *>((char*) (surface->pixels) + y * surface->pitch + x * 3);
            *pixel = (r << 16) | (g << 8) | b;
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
    SDL_Surface *imageSurface = SDL_LoadBMP("../example.bmp");
    imageSurface = loadYUV(INPUT_FILE, WIDTH, HEIGHT);
    if (imageSurface == nullptr) {
        printf("read image error!!");
        std::cout << "Failed to load image! SDL_Error: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_Texture *imageTexture = SDL_CreateTextureFromSurface(renderer, imageSurface);
    SDL_FreeSurface(imageSurface);


    bool quit = false;
    SDL_Event e;

    SDL_RenderCopy(renderer, imageTexture, nullptr, nullptr);  // 将texture复制到renderer
    SDL_RenderPresent(renderer);  // renderer上屏
    while (!quit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_q) {
                quit = true;
            }
        }
        /**
         * 这个死循环里可以播放不同的图片
         */

//        SDL_RenderClear(renderer);  // 清空renderer
//        SDL_RenderCopy(renderer, imageTexture, NULL, NULL);  // 将texture复制到renderer
//        SDL_RenderPresent(renderer);  // renderer上屏
    }

    SDL_DestroyTexture(imageTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

