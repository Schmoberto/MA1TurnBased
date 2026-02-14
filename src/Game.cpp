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
    printf("[GAME] Constructor called\n");
}

Game::~Game() {
    cleanup();
}

// Static callback: Initialize the app
SDL_AppResult Game::AppInit(void** appstate, int argc, char* argv[]) {
    printf("[AppInit] Starting initialization...\n");
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::cerr << "[AppInit] SDL init failed: " << SDL_GetError() << std::endl;
        return SDL_APP_FAILURE;
    }
    printf("[AppInit] SDL initialized successfully.\n");

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
                std::cerr << "[AppInit] Invalid port number: " << argv[2] << ". Shutting down." << std::endl;
                return SDL_APP_FAILURE;
            }
        }
        printf("[AppInit] Starting in SERVER mode on port %d...\n", port);
    } else {
        // Client mode
        if (argc > 2) {
            serverAddr = argv[2];
        }
        if (argc > 3) {
            port = static_cast<uint16_t>(std::stoi(argv[3]));
        }
        printf("[AppInit] Starting in CLIENT mode, connecting to %s:%d...\n", serverAddr.c_str(), port);
    }

    printf("[AppInit] Initializing game...\n");
    // Create game instance
    Game* game = new Game(isServer, serverAddr, port);

    if (!game->initialize()) {
        delete game;
        std::cerr << "[AppInit] Game initialization failed!" << std::endl;
        return SDL_APP_FAILURE;
    }

    printf("[AppInit] Game initialization complete.\n");
    // Store game instance in appstate
    *appstate = game;
    return SDL_APP_CONTINUE;
}

// Initialize game resources
bool Game::initialize() {
    printf("[AppInit] Creating window..\n");
    // Create window with SDL_Renderer graphics context
    Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    window = SDL_CreateWindow(
        isServer ? "Tic-Tac-Toe Server (X)" : "Tic-Tac-Toe Client, (O)",
        WINDOW_WIDTH, WINDOW_HEIGHT,
        window_flags);

    if (window == nullptr)
    {
        std::cerr << "[AppInit] Error while creating window: " << SDL_GetError() << std::endl;
        return false;
    }
    printf("[AppInit] Window created.\n");

    printf("[AppInit] Creating renderer...\n");
    renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);
    if (renderer == nullptr)
    {
        std::cerr << "[AppInit] Error while creating renderer: " << SDL_GetError() << std::endl;
        return false;
    }
    printf("[AppInit] Renderer created.\n");

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    printf("[AppInit] Initializing ImGui...\n");

    try {
        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        printf("-- [AppInit:ImGui] Creating ImGui context...\n");
        imguiContext = ImGui::CreateContext();

        if (!imguiContext) {
            std::cerr << "-- [AppInit:ImGui] Failed to create ImGui context." << std::endl;
        }

        printf("-- [AppInit:ImGui] Setting current ImGui context.\n");
        ImGui::SetCurrentContext(imguiContext);

        printf("-- [AppInit:ImGui] Getting IO...\n");
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();

        // Setup Platform/Renderer backends
        if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)) {
            std::cerr << "-- [AppInit:ImGui] ImGui SDL3 init failed!" << std::endl;
            return false;
        }

        if (!ImGui_ImplSDLRenderer3_Init(renderer)) {
            std::cerr << "-- [AppInit:ImGui] ImGui SDLRenderer3 init failed." << std::endl;
            return false;
        }

        printf("[AppInit:ImGui] ImGui initialized.\n");
    } catch (const std::exception& e) {
        std::cerr << "[AppInit:ImGui] Exception during ImGui initialization: " << e.what() << std::endl;
    }


    printf("[AppInit] Initializing board...\n");
    // Initialize board
    board = std::make_unique<Board>();
    board->resetBoard();

    // Initialize render state
    currentRenderState.currentPlayer = TileState::X;
    currentRenderState.result = GameResult::IN_PROGRESS;
    currentRenderState.isMyTurn = isServer;

    printf("[AppInit] Starting network...\n");
    // Start network
    if (isServer) {
        gameServer = std::make_unique<GameServer>(port);
        if (!gameServer->startServer(port)) {
            std::cerr << "[AppInit:GameServer] Failed to start server." << std::endl;
            return false;
        }
    } else {
        gameClient = std::make_unique<GameClient>();
        if (!gameClient->connectToServer(serverAddress, port)) {
            std::cerr << "[AppInit:GameClient] Failed to connect to server." << std::endl;
            return false;
        }
    }
    printf("[AppInit] Network started successfully.\n");

    printf("[AppInit] Starting threads...\n");
    // Start threads
    running = true;

    printf("===MULTITHREADING===\n");
    std::cout << "Main thread ID: " << std::this_thread::get_id() << std::endl;

    logicThread = std::thread(&Game::logicThreadFunc, this);
    networkThread = std::thread(&Game::networkThreadFunc, this);

    printf("[AppInit] All threads started successfully. Game initialized.\n");
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
        std::cerr << "[AppIterate] Game pointer is null!" << std::endl;
        SDL_WaitEvent(nullptr);
        return SDL_APP_CONTINUE;
    }

    // Get latest state from logic thread
    GameStateSnapshot newState{};
    if (game->gameStateQueue.try_dequeue(newState)) {
        game->currentRenderState = newState;
    }

    game->render();

    return SDL_APP_CONTINUE;
}

// Static callback: Event handling
SDL_AppResult Game::AppEvent(void* appstate, SDL_Event* event) {
    Game* game = static_cast<Game*>(appstate);

    if (!game) {
        std::cerr << "[AppEvent] Game pointer is null!" << std::endl;
        return SDL_APP_FAILURE;
    }

    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;  // Exit gracefully
    }

    if (game->imguiContext) {
        // Let ImGui process the event first
        ImGui_ImplSDL3_ProcessEvent(event);

        // If ImGui wants input, dont process game input
        ImGuiIO& io = ImGui::GetIO();
        if  (io.WantCaptureMouse && (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                                    event->type == SDL_EVENT_MOUSE_MOTION)) {
            return SDL_APP_CONTINUE;
                                    }
        if (io.WantCaptureKeyboard && (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP)) {
            return SDL_APP_CONTINUE;
        }
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
}

void Game::render() {
    if (!renderer) {
        std::cerr << "[RENDER] Renderer is null!" << std::endl;
        return;
    }

    if (!imguiContext) {
        std::cerr << "[RENDER] ImGui context is null!" << std::endl;
        return;
    }

    // Set ImGui context
    ImGui::SetCurrentContext(imguiContext);

    // Start ImGui frame
    try {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    } catch (const std::exception& e) {
        std::cerr << "[render] ImGui NewFrame exception: " << e.what() << std::endl;
        return;
    }

    // Clear screen
    SDL_SetRenderDrawColor(renderer, 50, 50, 60, 255);
    SDL_RenderClear(renderer);

    // Draw board
    if (board) {
        board->render(renderer, CELL_SIZE, GRID_OFFSET_X, GRID_OFFSET_Y);
    }

    // Render ImGui windows
    renderImGui();

    // Render ImGui
    try {
        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    } catch (const std::exception& e) {
        std::cerr << "[render] ImGui Render exception: " << e.what() << std::endl;
        return;
    }

    SDL_RenderPresent(renderer);
}

void Game::renderImGui() {

    // Game Status Window
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(580, 100), ImGuiCond_FirstUseEver);

    ImGui::Begin("Game Status", nullptr, ImGuiWindowFlags_NoCollapse);

    // Game result or current turn
    if (currentRenderState.result == GameResult::X_WINS) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
        ImGui::TextWrapped("X Wins!");
        ImGui::PopStyleColor();
    } else if (currentRenderState.result == GameResult::O_WINS) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.2f, 1.0f, 1.0f));
        ImGui::TextWrapped("O Wins!");
        ImGui::PopStyleColor();
    } else if (currentRenderState.result == GameResult::DRAW) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
        ImGui::TextWrapped("It's a Draw!");
        ImGui::PopStyleColor();
    } else {
        // Game in progress
        if (currentRenderState.isMyTurn) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
            ImGui::TextWrapped("Your turn (%s)", myMark == TileState::X ? "X" : "O");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            ImGui::TextWrapped("Opponent's turn...");
            ImGui::PopStyleColor();
        }
    }

    ImGui::Separator();

    // Network status
    std::string netStatus;
    if (isServer) {
        int clientCount = gameServer ? gameServer->getClientCount() : 0;
        netStatus = "Server | Players: " + std::to_string(clientCount + 1) + "/2";
    } else {
        netStatus = gameClient && gameClient->isConnected() ? "Connected to server" : "Connecting...";
    }
    ImGui::Text("%s", netStatus.c_str());

    // Reset button
    if (ImGui::Button("Reset Game (R)")) {
        Command cmd{};
        cmd.type = CommandType::RESET_GAME;
        commandInputQueue.enqueue(cmd);
    }

    ImGui::End();
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

                    if (isServer && gameServer) {
                        printf("[OLGIC] Server broadcasting move to client\n");
                        gameServer->broadcastPacket(packet);
                    } else if (!isServer && gameClient) {
                        printf("[LOGIC] Client sending move to server\n");
                        gameClient->sendPacketToServer(packet);
                    }

                    // Check winner
                    localResult = board->checkWinner();

                    // Switch turn
                    if (localResult == GameResult::IN_PROGRESS) {
                        localCurrentPlayer = (localCurrentPlayer == TileState::X) ? TileState::O : TileState::X;
                        printf("[LOGIC] Turn switched to %c\n", localCurrentPlayer == TileState::X ? 'X' : 'O');
                    } else {
                        printf("[LOGIC] Invalid move\n");
                    }
                }


            } else if (cmd.type == CommandType::NETWORK_MOVE) {
                if (board->setTile(cmd.x, cmd.y, cmd.mark)) {
                    std::cout << "[LOGIC] Applied network move: "
                              << (cmd.mark == TileState::X ? "X" : "O")
                              << " at (" << cmd.x << ", " << cmd.y << ")" << std::endl;

                    localResult = board->checkWinner();

                    if (localResult == GameResult::IN_PROGRESS) {
                        localCurrentPlayer = (localCurrentPlayer == TileState::X) ? TileState::O : TileState::X;
                        printf("[LOGIC] Turn switched to %c\n", localCurrentPlayer == TileState::X ? 'X' : 'O');
                    }
                } else {
                    printf("[LOGIC] Failed to apply network move: Invalid position or tile already occupied");
                }


            } else if (cmd.type == CommandType::RESET_GAME) {
                printf("[LOGIC] Local reset, sending to network...\n");
                board->resetBoard();
                localCurrentPlayer = TileState::X;
                localResult = GameResult::IN_PROGRESS;

                if (!cmd.fromNetwork) {
                    NetworkPacket packet;
                    packet.type = PacketType::GAME_RESET;
                    if (isServer && gameServer) {
                        gameServer->broadcastPacket(packet);
                    } else if (!isServer && gameClient) {
                        gameClient->sendPacketToServer(packet);
                    }
                }
                printf("[LOGIC] Game reset\n");
            } else if (cmd.type == CommandType::NETWORK_RESET) {
                printf("[LOGIC] Received network reset\n");
                board->resetBoard();
                localCurrentPlayer = TileState::X;
                localResult = GameResult::IN_PROGRESS;
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
        if (isServer && gameServer) {
            gameServer->updateServer();

            NetworkPacket packet;
            while (gameServer->incomingPackets.try_dequeue(packet)) {
                printf("[NETWORK] Server received packet type %d\n", static_cast<int>(packet.type));

                if (packet.type == PacketType::PLAYER_MOVE) {
                    int x = static_cast<int>(packet.data["x"]);
                    int y = static_cast<int>(packet.data["y"]);
                    auto mark = static_cast<TileState>(packet.data["mark"].get<int>());

                    //board->setTile(x, y, mark);

                    Command cmd{};
                    cmd.type = CommandType::NETWORK_MOVE;
                    cmd.x = x;
                    cmd.y = y;
                    cmd.mark = mark;
                    commandInputQueue.enqueue(cmd);

                   //gameServer->broadcastPacket(packet);
                } else if (packet.type == PacketType::GAME_RESET) {
                    Command cmd;
                    cmd.type = CommandType::RESET_GAME;
                    cmd.fromNetwork = true;
                    commandInputQueue.enqueue(cmd);
                }
                //processPacket(packet, true);
            }
        } else if (!isServer && gameClient) {
            gameClient->updateClient();

            NetworkPacket packet;
            while (gameClient->incomingPackets.try_dequeue(packet)) {
                printf("[NETWORK] Client received packet type %d\n", static_cast<int>(packet.type));

                if (packet.type == PacketType::PLAYER_MOVE) {
                    int x = static_cast<int>(packet.data["x"]);
                    int y = static_cast<int>(packet.data["y"]);
                    auto mark = static_cast<TileState>(packet.data["mark"].get<int>());

                    //board->setTile(x, y, mark);
                    printf("[NETWORK] Client processing server move: %c at (%d, %d)\n", mark == TileState::X ? 'X' : 'O', x, y);

                    Command cmd{};
                    cmd.type = CommandType::NETWORK_MOVE;
                    cmd.x = x;
                    cmd.y = y;
                    cmd.mark = mark;
                    commandInputQueue.enqueue(cmd);
                } else if (packet.type == PacketType::GAME_RESET) {
                    printf("[NETWORK] Client received reset (not echoing)\n");
                    Command cmd;
                    cmd.type = CommandType::RESET_GAME;
                    cmd.fromNetwork = true;
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


    // Cleanup ImGui
    if (imguiContext) {
        std::cout << "a0" << std::endl;
        ImGui::SetCurrentContext(imguiContext);
        try {
            ImGui_ImplSDLRenderer3_Shutdown();
        } catch (std::exception& e) {
            std::cerr << "Exception during ImGui_ImplSDLRenderer3_Shutdown: " << e.what() << std::endl;
        }
        std::cout << "a0.5" << std::endl;
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(imguiContext);
        imguiContext = nullptr;
    }


    std::cout << "a1" << std::endl;
    gameServer.reset();
    gameClient.reset();
    board.reset();

    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    std::cout << "a2" << std::endl;
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    printf("[MAIN] Cleanup complete. Exiting.\n");
}



