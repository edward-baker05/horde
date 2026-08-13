#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>

#include "app/App.hpp"

//main entry point
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    // create app object
    horde::app::App app;

    //start app object
    if (!app.init(horde::app::AppConfig{})) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Startup failed");
        return 1;
    }

    return app.run();
}
