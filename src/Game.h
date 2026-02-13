#pragma once

#include "Board.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <memory>

class Game {
public:
    Game();
    ~Game();

    static SDL_AppResult AppInit(void** appstate, int argc, char** argv);
    static SDL_AppResult AppIterate(void* appstate);
    static SDL_AppResult AppEvent(void* appstate, SDL_Event* event);
    static void AppQuit(void* appstate, SDL_AppResult result);

private:
    std::unique_ptr<Board> board;

    static const int WINDOW_WIDTH = 800;
    static const int WINDOW_HEIGHT = 800;
    static const int GRID_SIZE = 3;
    static const int CELL_SIZE = 200;
    static const int GRID_OFFSET_X = 5;
    static const int GRID_OFFSET_Y = 5;

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool show_demo_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    bool initialize();
    void handleEvent(SDL_Event* event);
    void update();
    void render();
    void cleanup();

    void handleKeyPress(SDL_Keycode key);
    void handleMouseClick(int mouseX, int mouseY);

};

