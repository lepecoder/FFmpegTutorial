//
// Created by lxp on 2024/7/6.
//

/**
 * 使用SDL展示一帧图像
1. 初始化SDL：调用SDL_Init函数来初始化SDL库。
2. 创建一个窗口和渲染器：调用SDL_CreateWindow和SDL_CreateRenderer函数来创建一个窗口和渲染器。
3. 加载图像：加载需要显示的图像，SDL_LoadBMP加载图像为surface
4. 创建一个纹理：使用加载的图像创建一个纹理，使用SDL_CreateTextureFromSurface函数。
5. 将纹理渲染到屏幕：使用SDL_RenderCopy函数将纹理渲染到屏幕上。
6. 刷新屏幕：使用SDL_RenderPresent函数来刷新屏幕。
7. 释放资源：使用SDL_DestroyTexture、SDL_DestroyRenderer、SDL_DestroyWindow等函数释放分配的资源。
 播放视频时需要循环5 6步骤，更新纹理并刷新屏幕
 */

#include <SDL.h>
//#include <SDL_image.h>
#include "iostream"


const int SCREEN_WIDTH = 640;
const int SCREEN_HEIGHT = 480;

int main(int argc, char* args[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow("Blank Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                          SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);

    if (window == nullptr) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    if (renderer == nullptr) {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }
    SDL_Surface* imageSurface = SDL_LoadBMP("../example.bmp");
    if (imageSurface == nullptr) {
        printf("read image error!!");
        std::cout << "Failed to load image! SDL_Error: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_Texture* imageTexture = SDL_CreateTextureFromSurface(renderer, imageSurface);
    SDL_FreeSurface(imageSurface);


    bool quit = false;
    SDL_Event e;

    SDL_RenderCopy(renderer, imageTexture, nullptr, nullptr);  // 将texture复制到renderer
    SDL_RenderPresent(renderer);  // renderer上屏
    while (!quit) {
        while (SDL_PollEvent(&e)!= 0) {
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


