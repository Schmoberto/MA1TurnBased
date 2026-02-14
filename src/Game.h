#pragma once

#include "Board.h"
#include "NetworkManager.h"
#include "MainMenu.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <memory>
#include <thread>
#include <atomic>
#include <moodycamel/concurrentqueue.h>

// Commands for inter-thread communication
enum class CommandType {
    PLACE_MARK,
    RESET_GAME,
    NETWORK_MOVE,
    NETWORK_RESET
};

enum class GameState {
    MAIN_MENU,
    IN_GAME,
    DISCONNECTED
};

struct Command {
    CommandType type;
    int x, y;
    TileState mark;
    bool fromNetwork = false;
};

struct GameStateSnapshot {
    std::array<std::array<TileState, 3>, 3> boardState;
    TileState currentPlayer;
    GameResult result;
    bool isMyTurn;
};

class Game {
public:
    Game();
    ~Game();

    static SDL_AppResult AppInit(void** appstate, int argc, char** argv);
    static SDL_AppResult AppIterate(void* appstate);
    static SDL_AppResult AppEvent(void* appstate, SDL_Event* event);
    static void AppQuit(void* appstate, SDL_AppResult result);

private:
    // Constants
    static const int WINDOW_WIDTH = 800;
    static const int WINDOW_HEIGHT = 800;
    static const int GRID_SIZE = 30;
    static const int CELL_SIZE = 200;
    static const int GRID_OFFSET_X = 15;
    static const int GRID_OFFSET_Y = 15;

    // SDL
    SDL_Window* window;
    SDL_Renderer* renderer;
    bool show_demo_window = true;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    std::unique_ptr<Board> board;

    // ImGui (persistent)
    ImGuiContext* imguiContext;

    // Game state
    GameState gameState;
    std::unique_ptr<MainMenu> mainMenu;

    // Networking
    bool isServer;
    std::unique_ptr<GameServer> gameServer;
    std::unique_ptr<GameClient> gameClient;
    uint16_t port;
    std::string serverAddress;

    // Threading
    std::thread logicThread;
    std::thread networkThread;
    std::atomic<bool> running;

    // Inter-thread queues
    moodycamel::ConcurrentQueue<Command> commandInputQueue;
    moodycamel::ConcurrentQueue<GameStateSnapshot> gameStateQueue;

    // Render state
    GameStateSnapshot currentRenderState;

    // Game state
    TileState myMark; // X or O
    TileState currentTurn;

    // Thread functions
    void logicThreadFunc();
    void networkThreadFunc();

    // Network helper
    NetworkPacket processPacket(NetworkPacket& packet, bool fromServer);

    // Methods
    bool initialize();
    bool startGame(bool asServer, const std::string& serverAddr, uint16_t port);
    void stopGame();

    void handleEvent(SDL_Event* event);
    void update();
    void render();
    void renderMenu();
    void renderGame();
    void renderImGui();
    void cleanup();

    void handleKeyPress(SDL_Keycode key);
    void handleMouseClick(int mouseX, int mouseY);

    void drawText(const std::string& text, int x, int y, SDL_Color color);
};

