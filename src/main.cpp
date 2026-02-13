#include "Game.h"

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

// SDL3 main callbacks entry point
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    return Game::AppInit(appstate, argc, argv);
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    return Game::AppIterate(appstate);
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    return Game::AppEvent(appstate, event);
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    Game::AppQuit(appstate, result);
}