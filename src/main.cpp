#include <cmath>
#include <iostream>

#include "SDL3/SDL_rect.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_stdinc.h"

#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;

struct Circle {
    SDL_FPoint points[360];
};

const double pi = std::acos(-1);
Circle circles[10];

struct Circle getCirclePoints(int x, int y, int r) {
    struct Circle circle_mem;

    for (int i = 0; i < 360; i++) {
        circle_mem.points[i].x = x + sin(i * pi / 180) * r;
        circle_mem.points[i].y = y + cos(i * pi / 180) * r;
    }

    return circle_mem;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    if (!SDL_CreateWindowAndRenderer("Test", 800, 600, SDL_WINDOW_FULLSCREEN, &window, &renderer)) {
        SDL_Log("Couldn't create window and renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    for (int i = 0; i < 10; i++) {
        int x = SDL_rand(800);
        int y = SDL_rand(600);
        int r = SDL_rand(50);
        SDL_Log("%d: %d %d %d", i, x, y, r);
        circles[i] = getCirclePoints(x, y, r);
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        return SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    SDL_SetRenderDrawColor(renderer, 10, 10, 10, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 0, 255, 0, SDL_ALPHA_OPAQUE);
    for (int i = 0; i < 10; i++) {
        SDL_RenderPoints(renderer, circles[i].points, 360);
    }

    // SDL_FPoint* points = getCirclePoints(400, 300, 100);
    // SDL_RenderPoints(renderer, points, 360);

    SDL_RenderPresent(renderer);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {}
