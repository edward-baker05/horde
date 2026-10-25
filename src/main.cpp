#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>

#include "app/App.hpp"
#include "app/CommandLine.hpp"

// main entry point
int main(int argc, char* argv[]) {
    horde::app::AppConfig config;

    switch (horde::app::parseCommandLine(argc, argv, config)) {
        case horde::app::ParseResult::Exit:
            return 0;
        case horde::app::ParseResult::Error:
            return 2;
        case horde::app::ParseResult::Run:
            break;
    }

    // create app object
    horde::app::App app;

    // start app object
    if (!app.init(config)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Startup failed");
        return 1;
    }

    return app.run();
}
