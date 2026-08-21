#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "circle.cpp"

static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;

std::vector<Circle> circles = {Circle(200, 300, 50, true), Circle(400, 300, 50, true)};

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    if (!SDL_CreateWindowAndRenderer("Test", 800, 600, SDL_WINDOW_FULLSCREEN, &window, &renderer)) {
        SDL_Log("Couldn't create window and renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    circles[0].setVelocity(1.f, 0.f);
    circles[1].setVelocity(-1.f, 0.f);

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
    SDL_SetRenderDrawColor(renderer, 15, 15, 15, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    circles[0].draw(renderer, 255, 255, 0);
    circles[1].draw(renderer, 0, 0, 255);
    circles[0].update(circles, 0);
    circles[1].update(circles, 1);

    SDL_RenderPresent(renderer);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {}
