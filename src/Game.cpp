#include "Game.h"

#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <iostream>

Game::Game() 
    : window(nullptr), renderer(nullptr), board(nullptr) {
}

Game::~Game() {
    cleanup();
}

// Static callback: Initialize the app
SDL_AppResult Game::AppInit(void** appstate, int argc, char* argv[]) {
   if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    
    // Create game instance
    Game* game = new Game();
    
    if (!game->initialize()) {
        delete game;
        return SDL_APP_FAILURE;
    }
    
    // Store game instance in appstate
    *appstate = game;
    
    return SDL_APP_CONTINUE;
}

// Static callback: Main loop iteration
SDL_AppResult Game::AppIterate(void* appstate) {
    Game* game = static_cast<Game*>(appstate);

    if (SDL_GetWindowFlags(game->window) & SDL_WINDOW_MINIMIZED)
    {
        SDL_WaitEvent(nullptr);
        return SDL_APP_CONTINUE;
    }

    game->update();
    game->render();
    
    return SDL_APP_CONTINUE;
}

// Static callback: Event handling
SDL_AppResult Game::AppEvent(void* appstate, SDL_Event* event) {
    Game* game = static_cast<Game*>(appstate);
    
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;  // Exit gracefully
    }
    
    game->handleEvent(event);
    
    return SDL_APP_CONTINUE;
}

// Static callback: Cleanup
void Game::AppQuit(void* appstate, SDL_AppResult result) {
    Game* game = static_cast<Game*>(appstate);

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (game) {
        game->cleanup();
        delete game;
    }

    SDL_Quit();

    if (result == SDL_APP_SUCCESS) {
        std::cout << "Game exited successfully" << std::endl;
    } else {
        std::cerr << "Game exited with error" << std::endl;
    }
}

// Initialize game resources
bool Game::initialize() {
    // Create window with SDL_Renderer graphics context
    Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    window = SDL_CreateWindow("Dear ImGui SDL3+SDL_Renderer example", 1280, 720, window_flags);

    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return false;
    }
    renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);
    if (renderer == nullptr)
    {
        SDL_Log("Error: SDL_CreateRenderer(): %s\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    
    // Initialize board
    board = std::make_unique<Board>(GRID_SIZE);
    board->resetBoard();
    
    std::cout << "Game initialized successfully!" << std::endl;
    return true;
}

// Handle events
void Game::handleEvent(SDL_Event* event) {
    switch (event->type) {
        case SDL_EVENT_KEY_DOWN:
            handleKeyPress(event->key.key);
            break;
            
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event->button.button == SDL_BUTTON_LEFT) {
                handleMouseClick(event->button.x, event->button.y);
            }
            break;
    }
}

void Game::handleKeyPress(SDL_Keycode key) {
    switch (key) {
        case SDLK_R:
            board->resetBoard();
            std::cout << "Board reset!" << std::endl;
            break;
            
        // Arrow key handling for 2048 movement
        case SDLK_UP:
        case SDLK_DOWN:
        case SDLK_LEFT:
        case SDLK_RIGHT:
            // TODO: Implement tile movement
            std::cout << "Arrow key pressed" << std::endl;
            break;
    }
}

void Game::handleMouseClick(int mouseX, int mouseY) {
    // Convert screen coordinates to grid coordinates
    int gridX = (mouseX - GRID_OFFSET_X) / CELL_SIZE;
    int gridY = (mouseY - GRID_OFFSET_Y) / CELL_SIZE;
    
    if (board->isValidPosition(gridX, gridY)) {
        std::cout << "Clicked tile: (" << gridX << ", " << gridY << ")" << std::endl;
        std::cout << "Value: " << board->getTile(gridX, gridY) << std::endl;
    }
}

void Game::update() {
    // Game logic updates
    //if (!board->canMove()) {
        // Game over state
        // Could add a flag here to show game over message
    //}

    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Show the big demo window
    if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);
}

void Game::render() {
    ImGui::Render();

    // Clear screen
    SDL_SetRenderDrawColorFloat(renderer, clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    
    // Render board
    board->render(renderer, CELL_SIZE, GRID_OFFSET_X, GRID_OFFSET_Y);
    
    // Present
    SDL_RenderPresent(renderer);
}

void Game::cleanup() {
    board.reset();  // Clean up board first

    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    
    std::cout << "Game cleaned up" << std::endl;
}