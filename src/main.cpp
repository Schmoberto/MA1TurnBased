/*******************************************************************************
 * main.cpp
 *
 * Entry point for the Multithreaded Networked Tic-Tac-Toe game. Initializes SDL,
 * creates the main Game instance, and sets up the main loop using SDL's callback system.
 *
 * Architecture:
 * - Uses SDL3's SDL_main callbacks for application lifecycle management
 * - Delegates initialization, event handling, and rendering to the Game class
 * - Provides a clean separation between SDL setup and game logic
 ******************************************************************************/

#include "Game.h"

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

// SDL3 main callbacks entry point
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    printf("\n[MAIN] Starting Multithreaded Networked TicTacToe...\n");

    if (argc < 2) {
        printf("Usage: %s [server|client] [server_address] [port]\n", argv[0]);
    }

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