#include "Game.h"

#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <iostream>

Game::Game(bool isServer, const std::string& serverAddr, uint16_t port)
    : window(nullptr), renderer(nullptr), board(nullptr)
      , isServer(isServer), port(port), serverAddress(serverAddr)
      , running(false)
      , currentRenderState(), myMark(isServer ? TileState::X : TileState::O)
      , currentTurn(TileState::X) {
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

    // Parse command line
    bool isServer = argc > 1 && std::string(argv[1]) == "server";
    std::string serverAddr = argc > 2 ? argv[2] : "127.0.0.1";
    uint16_t port = 27015;

    if (isServer) {
        // Server mode
        if (argc > 2) {
            try {
                port = static_cast<uint16_t>(std::stoi(argv[2]));
            } catch (const std::exception& e) {
                printf("Invalid port number: %s\n", argv[2]);
                return SDL_APP_FAILURE;
            }
        }
        printf("Starting in SERVER mode on port %d...\n", port);
    } else {
        // Client mode
        if (argc > 2) {
            serverAddr = argv[2];
        }
        if (argc > 3) {
            port = static_cast<uint16_t>(std::stoi(argv[3]));
        }
        printf("Starting in CLIENT mode, connecting to %s:%d...\n", serverAddr.c_str(), port);
    }

    // Create game instance
    Game* game = new Game(isServer, serverAddr, port);
    
    if (!game->initialize()) {
        delete game;
        return SDL_APP_FAILURE;
    }
    
    // Store game instance in appstate
    *appstate = game;
    return SDL_APP_CONTINUE;
}

// Initialize game resources
bool Game::initialize() {
    // Create window with SDL_Renderer graphics context
    Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    window = SDL_CreateWindow(
        isServer ? "Tic-Tac-Toe Server (X)" : "Tic-Tac-Toe Client, (O)",
        WINDOW_WIDTH, WINDOW_HEIGHT,
        window_flags);

    if (window == nullptr)
    {
        printf("Error while creating window: %s\n", SDL_GetError());
        return false;
    }
    renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);
    if (renderer == nullptr)
    {
        SDL_Log("Error while creating renderer: %s\n", SDL_GetError());
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
    board = std::make_unique<Board>();
    board->resetBoard();

    // Initialize render state
    currentRenderState.currentPlayer = TileState::X;
    currentRenderState.result = GameResult::IN_PROGRESS;
    currentRenderState.isMyTurn = isServer;

    // Start network
    if (isServer) {
        gameServer = std::make_unique<GameServer>(port);
        if (!gameServer->startServer(port)) {
            return false;
        }
    } else {
        gameClient = std::make_unique<GameClient>();
        if (!gameClient->connectToServer(serverAddress, port)) {
            return false;
        }
    }

    // Start threads
    running = true;

    printf("===MULTITHREADING===\n");
    std::cout << "Main thread ID: " << std::this_thread::get_id() << std::endl;

    logicThread = std::thread(&Game::logicThreadFunc, this);
    networkThread = std::thread(&Game::networkThreadFunc, this);

    printf("[MAIN] All threads started successfully. Game initialized.\n");
    return true;
}

// ============================================================================
//                          RENDER THREAD (Main)
// ============================================================================

// Static callback: Main loop iteration
SDL_AppResult Game::AppIterate(void* appstate) {
    Game* game = static_cast<Game*>(appstate);

    if (SDL_GetWindowFlags(game->window) & SDL_WINDOW_MINIMIZED)
    {
        SDL_WaitEvent(nullptr);
        return SDL_APP_CONTINUE;
    }

    // Get latest state from logic thread
    GameStateSnapshot newState{};
    if (game->gameStateQueue.try_dequeue(newState)) {
        game->currentRenderState = newState;
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
    }
}

void Game::handleMouseClick(int mouseX, int mouseY) {
    // Only allow clicks if it's the player's turn
    if (!currentRenderState.isMyTurn) {
        printf("[RENDER] Not your turn!\n");
        return;
    }

    if (currentRenderState.result != GameResult::IN_PROGRESS) {
        printf("[RENDER] Game over! Press R to reset.\n");
        return;
    }

    auto pos = board->screenToGrid(mouseX, mouseY, CELL_SIZE, GRID_OFFSET_X, GRID_OFFSET_Y);

    if (pos.valid) {
        Command cmd{};
        cmd.type = CommandType::PLACE_MARK;
        cmd.x = pos.x;
        cmd.y = pos.y;
        cmd.mark = myMark;

        commandInputQueue.enqueue(cmd);
        printf("[RENDER] Enqueued command: Place %c at (%d, %d)\n", myMark == TileState::X ? 'X' : 'O', cmd.x, cmd.y);
    }

    /*
    // Convert screen coordinates to grid coordinates
    int gridX = (mouseX - GRID_OFFSET_X) / CELL_SIZE;
    int gridY = (mouseY - GRID_OFFSET_Y) / CELL_SIZE;

    if (board->isValidPosition(gridX, gridY)) {
        std::cout << "Clicked tile: (" << gridX << ", " << gridY << ")" << std::endl;
        //std::cout << "Value: " << board->getTile(gridX, gridY) << std::endl;
    }
    */
}

void Game::render() {
    ImGui::Render();

    // Clear screen
    SDL_SetRenderDrawColorFloat(renderer, clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

    // Render board
    board->render(renderer, CELL_SIZE, GRID_OFFSET_X, GRID_OFFSET_Y);

    // Draw status text
    std::string statusText;
    SDL_Color color = {0, 0 ,0, 255};

    if (currentRenderState.result == GameResult::X_WINS) {
        statusText = "X Wins!";
        color = {255, 0, 0, 255};
    } else if (currentRenderState.result == GameResult::O_WINS) {
        statusText = "O Wins!";
        color = {0, 0, 255, 255};
    } else if (currentRenderState.result == GameResult::DRAW) {
        statusText = "It's a draw!";
        color = {128, 128, 128, 255};
    } else {
        if (currentRenderState.isMyTurn) {
            statusText = "Your turn (" + std::string(myMark == TileState::X ? "X" : "O") + ")";
            color = {0, 128, 0, 255};
        } else {
            statusText = "Opponent's turn (" + std::string(currentRenderState.currentPlayer == TileState::X ? "X" : "O") + ")";
            color = {128, 0, 128, 255};
        }
    }

    drawText(statusText, WINDOW_WIDTH / 2 - 80, 20, color);

    // Draw network status
    std::string netStatus;
    if (isServer) {
        netStatus = "Server | Players: " + std::to_string(gameServer->getClientCount() + 1) + "/2";
    }
    else {
        netStatus = gameClient->isConnected() ? "Connected to server" : "Connecting...";
    }
    drawText(netStatus, WINDOW_WIDTH / 2 - 50, 20, color);

    // Present
    SDL_RenderPresent(renderer);
}

void Game::drawText(const std::string &text, int x, int y, SDL_Color color) {
    // change to im gui
}

// ============================================================================
//                          LOGIC THREAD
// ============================================================================

void Game::logicThreadFunc() {
    std::cout << "[LOGIC] Thread started (ID: " << std::this_thread::get_id() << ")" << std::endl;

    TileState localCurrentPlayer = TileState::X;
    GameResult localResult = GameResult::IN_PROGRESS;

    auto lastUpdateTime = std::chrono::steady_clock::now();

    while (running) {
        // Process incoming commands
        Command cmd;
        while (commandInputQueue.try_dequeue(cmd)) {
            printf("[LOGIC] Processing command: Type=%d, X=%d, Y=%d, Mark=%c\n",
                   static_cast<int>(cmd.type), cmd.x, cmd.y,
                   cmd.mark == TileState::X ? 'X' : 'O');

            if (cmd.type == CommandType::PLACE_MARK) {
                // Validate move
                if (localResult == GameResult::IN_PROGRESS &&
                        cmd.mark == localCurrentPlayer &&
                        board->setTile(cmd.x, cmd.y, cmd.mark)) {
                    printf("[LOGIC] Placed %c at (%d, %d)\n", cmd.mark == TileState::X ? 'X' : 'O', cmd.x, cmd.y);

                    // Send to network
                    NetworkPacket packet;
                    packet.type = PacketType::PLAYER_MOVE;
                    packet.data["x"] = cmd.x;
                    packet.data["y"] = cmd.y;
                    packet.data["mark"] = static_cast<int>(cmd.mark);

                    if (isServer) {
                        gameServer->broadcastPacket(packet);
                    } else {
                        gameClient->sendPacketToServer(packet);
                    }

                    // Check winner
                    localResult = board->checkWinner();

                    // Switch turn
                    if (localResult == GameResult::IN_PROGRESS) {
                        localCurrentPlayer = (localCurrentPlayer == TileState::X) ? TileState::O : TileState::X;
                    } else {
                        printf("[LOGIC] Invalid move\n");
                    }
                }
            } else if (cmd.type == CommandType::RESET_GAME) {
                board->resetBoard();
                localCurrentPlayer = TileState::X;
                localResult = GameResult::IN_PROGRESS;

                NetworkPacket packet;
                packet.type = PacketType::GAME_RESET;
                if (isServer) {
                    gameServer->broadcastPacket(packet);
                } else {
                    gameClient->sendPacketToServer(packet);
                }

                printf("[LOGIC] Game reset\n");
            } else if (cmd.type == CommandType::NETWORK_MOVE) {
                // Network move already applied to board in network thread
                localResult = board->checkWinner();
                if (localResult == GameResult::IN_PROGRESS) {
                    localCurrentPlayer = (localCurrentPlayer == TileState::X) ? TileState::O : TileState::X;
                }
            }
        }

        // Send state updates to render thread periodically
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdateTime).count() > 50) {
            GameStateSnapshot snapshot;
            snapshot.boardState = board->getGrid();
            snapshot.currentPlayer = localCurrentPlayer;
            snapshot.result = localResult;
            snapshot.isMyTurn = (localCurrentPlayer == myMark);

            gameStateQueue.enqueue(snapshot);
            lastUpdateTime = now;
        }

        // Sleep briefly to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    printf("[LOGIC] Thread exiting...\n");
}

// ============================================================================
//                          NETWORK THREAD
// ============================================================================

void Game::networkThreadFunc() {
    std::cout << "[NETWORK] Thread started (ID: " << std::this_thread::get_id() << ")" << std::endl;
    std::cout << "[NETWORK] Mode: " << (isServer ? "SERVER" : "CLIENT") << std::endl;

    while (running) {
        // Update network
        if (isServer) {
            gameServer->updateServer();

            NetworkPacket packet;
            while (gameServer->incomingPackets.try_dequeue(packet)) {
                printf("[NETWORK] Server received packet type %d\n", static_cast<int>(packet.type));

                if (packet.type == PacketType::PLAYER_MOVE) {
                    int x = static_cast<int>(packet.data["x"]);
                    int y = static_cast<int>(packet.data["y"]);
                    auto mark = static_cast<TileState>(packet.data["mark"].get<int>());

                    board->setTile(x, y, mark);

                    Command cmd{};
                    cmd.type = CommandType::NETWORK_MOVE;
                    cmd.x = x;
                    cmd.y = y;
                    cmd.mark = mark;
                    commandInputQueue.enqueue(cmd);

                    gameServer->broadcastPacket(packet);
                }
                //processPacket(packet, true);
            }
        } else {
            gameClient->updateClient();

            NetworkPacket packet;
            while (gameClient->incomingPackets.try_dequeue(packet)) {
                printf("[NETWORK] Client received packet type %d\n", static_cast<int>(packet.type));

                if (packet.type == PacketType::PLAYER_MOVE) {
                    int x = static_cast<int>(packet.data["x"]);
                    int y = static_cast<int>(packet.data["y"]);
                    auto mark = static_cast<TileState>(packet.data["mark"].get<int>());

                    board->setTile(x, y, mark);

                    Command cmd{};
                    cmd.type = CommandType::NETWORK_MOVE;
                    cmd.x = x;
                    cmd.y = y;
                    cmd.mark = mark;
                    commandInputQueue.enqueue(cmd);
                }

                //processPacket(packet, false);
            }
        }

        // Sleep briefly to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    printf("[NETWORK] Thread exiting...\n");
}

NetworkPacket Game::processPacket(NetworkPacket& packet, bool fromServer) {
    if (packet.type == PacketType::PLAYER_MOVE) {
        int x = static_cast<int>(packet.data["x"]);
        int y = static_cast<int>(packet.data["y"]);
        auto mark = static_cast<TileState>(packet.data["mark"].get<int>());

        board->setTile(x, y, mark);

        Command cmd{};
        cmd.type = CommandType::NETWORK_MOVE;
        cmd.x = x;
        cmd.y = y;
        cmd.mark = mark;
        commandInputQueue.enqueue(cmd);

        if (fromServer) {
            gameServer->broadcastPacket(packet);
        }
    }

    return packet;
}

// ============================================================================
//                         APP QUIT/CLEANUP
// ===========================================================================

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



void Game::update() {
    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Show the big demo window
    if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);
}


void Game::cleanup() {
    printf("\n===SHUTDOWN===\n");

    running = false;

    if (logicThread.joinable()) {
        logicThread.join();
        std::cout << "[MAIN] Logic thread joined successfully." << std::endl;
    }

    if (networkThread.joinable()) {
        networkThread.join();
        std::cout << "[MAIN] Network thread joined successfully." << std::endl;
    }

    std::cout << "[MAIN] All threads stopped. Cleaning up resources..." << std::endl;

    gameServer.reset();
    gameClient.reset();
    board.reset();

    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    
    printf("[MAIN] Cleanup complete. Exiting.\n");
}



