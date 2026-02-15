/*******************************************************************************
* Game.cpp
 *
 * Main game controller implementing the 3-thread architecture:
 * - Render Thread: SDL main loop, user input, ImGui rendering
 * - Logic Thread: Game rules, move validation, win detection
 * - Network Thread: Send/receive packets, connection management
 *
 * Thread Communication:
 * - Lock-free concurrent queues for cross-thread messaging
 * - Direct state updates for instant UI responsiveness
 ******************************************************************************/

#include "Game.h"

#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <iostream>
#include <ctime>

Game::Game()
    : window(nullptr), renderer(nullptr), board(nullptr)
      , gameState(GameState::MAIN_MENU)
      , isServer(isServer), port(port), serverAddress("127.0.0.1")
      , running(false)
      , currentRenderState(), myMark(isServer ? TileState::X : TileState::O)
      , currentTurn(TileState::X) {
    printf("[GAME] Constructor called\n");
}

Game::~Game() {
    cleanup();
}


/*******************************************************************************
 *                          SDL CALLBACK FUNCTIONS
 ******************************************************************************/

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
    Game* game = new Game();

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
        "MA1 - TicTacToe - Multithreaded Networked Game",
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

        mainMenu = std::make_unique<MainMenu>();

        printf("[AppInit:ImGui] ImGui initialized.\n");
    } catch (const std::exception& e) {
        std::cerr << "[AppInit:ImGui] Exception during ImGui initialization: " << e.what() << std::endl;
    }

    printf("Game initialized.\n");
    return true;
}

bool Game::startGame(bool asServer, const std::string &serverAddr, uint16_t port) {
    std::cout << "[GAME] Starting game as " << (asServer ? "SERVER" : "CLIENT") << std::endl;

    // Set game mode and network parameters
    isServer = asServer;
    serverAddress = serverAddr;
    this->port = port;
    myMark = isServer ? TileState::X : TileState::O;

    // Reset connection state
    connectionState.isConnected = false;
    connectionState.isReconnecting = false;
    connectionState.reconnectAttempts = 0;
    clientDisconnected = false;

    // Create board
    board = std::make_unique<Board>();
    board->setGridThickness(6);
    board->setGridColor({30, 30, 30, 255});
    board->setBackgroundColor({245, 245, 220, 255});
    board->setBackgroundPadding(15);

    // Initialize render state
    currentRenderState.currentPlayer = TileState::X;
    currentRenderState.result = GameResult::IN_PROGRESS;
    currentRenderState.isMyTurn = isServer;  // This is correct for initial state

    // Start network
    if (isServer) {
        gameServer = std::make_unique<GameServer>(port);
        if (!gameServer->startServer(port)) {
            std::cerr << "[GAME] Failed to start server!" << std::endl;
            addMessage("Failed to start server!", MessageType::ERROR);
            return false;
        }
        connectionState.isConnected = true;
        addMessage("Server started successfully!", MessageType::SUCCESS);
    } else {
        gameClient = std::make_unique<GameClient>();
        if (!gameClient->connectToServer(serverAddress, port)) {
            std::cerr << "[GAME] Failed to connect to server!" << std::endl;
            addMessage("Failed to connect to server!", MessageType::ERROR);
            return false;
        }

        addMessage("Connecting to server...", MessageType::INFO);
    }

    // Start threads
    printf("[AppInit] Starting threads...\n");
    running = true;
    printf("===MULTITHREADING===\n");
    std::cout << "Main thread ID: " << std::this_thread::get_id() << std::endl;

    logicThread = std::thread(&Game::logicThreadFunc, this);
    networkThread = std::thread(&Game::networkThreadFunc, this);

    gameState = GameState::IN_GAME;
    std::cout << "[GAME] Game started successfully!" << std::endl;
    return true;
}

void Game::stopGame() {
    printf("[GAME] Stopping game...\n");
    running = false;

    if (logicThread.joinable()) {
        logicThread.join();
    }
    if (networkThread.joinable()) {
        networkThread.join();
    }

    if (gameServer) {
        gameServer.reset();
    }
    if (gameClient) {
        gameClient.reset();
    }
    if (board) {
        board.reset();
    }

    // Clear messages
    activeMessages.clear();
    UIMessage msg;
    while (messageQueue.try_dequeue(msg)) {} // Clear queue

    gameState = GameState::MAIN_MENU;
    printf("[GAME] Game stopped. Retuning to menu.\n");
}

// ============================================================================
// MESSAGE SYSTEM
// ============================================================================

void Game::addMessage(const std::string& text, MessageType type) {
    UIMessage msg;
    msg.text = text;
    msg.type = type;
    msg.timestamp = std::chrono::steady_clock::now();
    msg.systemTime = std::chrono::system_clock::now();

    messageQueue.enqueue(msg);

    // Log with timestamp
    auto now_c = std::chrono::system_clock::to_time_t(msg.systemTime);
    std::tm now_tm;
#ifdef _WIN32
    localtime_s(&now_tm, &now_c);
#else
    localtime_r(&now_c, &now_tm);
#endif

    char timeStr[32];
    std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &now_tm);
    printf("[MESSAGE %s] %s\n", timeStr, text.c_str());
}

void Game::updateMessages() {
    // Add new messages from queue
    UIMessage msg;
    while (messageQueue.try_dequeue(msg)) {
        activeMessages.push_back(msg);

        // Keep only last MAX_MESSAGES
        if (activeMessages.size() > MAX_MESSAGES) {
            activeMessages.erase(activeMessages.begin());
        }
    }

    // Remove old messages
    auto now = std::chrono::steady_clock::now();
    activeMessages.erase(
        std::remove_if(activeMessages.begin(), activeMessages.end(),
            [now, this](const UIMessage& m) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m.timestamp);
                return elapsed.count() > MESSAGE_DURATION_MS;
            }),
        activeMessages.end()
    );
}

void Game::renderMessages() {
    if (activeMessages.empty()) return;

    // Render messages at bottom of game status window
    ImGui::Separator();
    ImGui::Text("Messages:");

    for (const auto& msg : activeMessages) {
        ImVec4 color;
        const char* prefix = "";

        switch (msg.type) {
            case MessageType::INFO:
                color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                prefix = "[INFO] ";
                break;
            case MessageType::SUCCESS:
                color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
                prefix = "[+] ";
                break;
            case MessageType::WARNING:
                color = ImVec4(0.9f, 0.7f, 0.2f, 1.0f);
                prefix = "[!] ";
                break;
            case MessageType::ERROR:
                color = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
                prefix = "[-] ";
                break;
        }

        // Format timestamp
        auto time_c = std::chrono::system_clock::to_time_t(msg.systemTime);
        std::tm time_tm;
#ifdef _WIN32
        localtime_s(&time_tm, &time_c);
#else
        localtime_r(&time_c, &time_tm);
#endif

        char timeStr[32];
        std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &time_tm);

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("[%s] %s%s", timeStr, prefix, msg.text.c_str());
        ImGui::PopStyleColor();
    }
}

// ============================================================================
// RECONNECTION LOGIC
// ============================================================================

void Game::handleDisconnection() {
    if (isServer) {
        // Server detected client disconnect
        clientDisconnected = true;
        addMessage("Client disconnected. Waiting for reconnection...", MessageType::WARNING);
    } else {
        // Client detected server disconnect
        if (!connectionState.isReconnecting) {
            connectionState.isConnected = false;
            connectionState.isReconnecting = true;
            addMessage("Lost connection to server...", MessageType::ERROR);

            // Give server some time, then give up
            std::thread([this]() {
                std::this_thread::sleep_for(std::chrono::seconds(10)); // Wait 10 seconds

                if (!connectionState.isConnected && running) {
                    addMessage("Could not reconnect. Returning to menu...", MessageType::ERROR);
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    stopGame();
                }
            }).detach();
        }
    }
}

// ============================================================================
//                          RENDER THREAD (Main)
// ============================================================================

// Static callback: Main loop iteration
SDL_AppResult Game::AppIterate(void* appstate) {
    Game* game = static_cast<Game*>(appstate);

    if (!game) {
        std::cerr << "[AppIterate] Game pointer is null!" << std::endl;
        return SDL_APP_FAILURE;
    }

    // If window is minimized, pause rendering to save resources
    if (SDL_GetWindowFlags(game->window) & SDL_WINDOW_MINIMIZED)
    {
        SDL_WaitEvent(nullptr);
        return SDL_APP_CONTINUE;
    }

    // Check menu choices if we're in the menu
    if (game->gameState == GameState::MAIN_MENU && game->mainMenu) {
        MenuChoice choice = game->mainMenu->getChoice();

        if (choice == MenuChoice::HOST_SERVER) {
            game->mainMenu->resetChoice();
            if (!game->startGame(true, "", game->mainMenu->getServerPort())) {
                std::cerr << "[MAIN MENU] Failed to start server game." << std::endl;
                // Stay in menu
            }
        } else if (choice == MenuChoice::JOIN_SERVER) {
            game->mainMenu->resetChoice();
            if (!game->startGame(false, game->mainMenu->getServerIP(), game->mainMenu->getServerPort())) {
                std::cerr << "[MAIN MENU] Failed to join server." << std::endl;
                // Stay in menu
            }
        } else if (choice == MenuChoice::QUIT) {
            return SDL_APP_SUCCESS; // Exit app
        }
    }

    // Get latest state from logic thread
    if (game->gameState == GameState::IN_GAME) {
        GameStateSnapshot newState;

        // Process all available states in the queue, but only keep the latest one for rendering
        while (game->gameStateQueue.try_dequeue(newState)) {
            game->currentRenderState = newState;
        }

        game->updateMessages();
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

    // Only handle events if we're in the game
    if (game->gameState == GameState::IN_GAME) {
        game->handleEvent(event);
    }

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
    // Check if game is over
    if (currentRenderState.result != GameResult::IN_PROGRESS) {
        printf("[RENDER] Game over! Press R to reset.\n");
        addMessage("Game is over! Press Reset to play again.", MessageType::INFO);
        return;
    }

    // Only allow clicks if it's the player's turn
    if (!currentRenderState.isMyTurn) {
        printf("[RENDER] Not your turn!\n");
        addMessage("Not your turn!", MessageType::WARNING);
        return;
    }

    auto pos = board->screenToGrid(mouseX, mouseY, CELL_SIZE, GRID_OFFSET_X, GRID_OFFSET_Y);

    if (!pos.valid) {
        addMessage("Click inside the grid!", MessageType::WARNING);
        return;
    }

    if (!board) {
        std::cerr << "[RENDER] Board is null!" << std::endl;
        return;
    }

    TileState tileState = board->getTile(pos.x, pos.y);
    printf("[RENDER] Tile state: %d\n", static_cast<int>(tileState));

    // Check if tile is already occupied
    if (tileState != TileState::EMPTY) {
        printf("[RENDER] Cell at (%d, %d) is already occupied by %c\n", pos.x, pos.y, tileState == TileState::X ? 'X' : 'O');
        addMessage("Cell already occupied!", MessageType::WARNING);
        return;
    }

    Command cmd{};
    cmd.type = CommandType::PLACE_MARK;
    cmd.x = pos.x;
    cmd.y = pos.y;
    cmd.mark = myMark;

    commandInputQueue.enqueue(cmd);
    printf("[RENDER] Enqueued command: Place %c at (%d, %d)\n", myMark == TileState::X ? 'X' : 'O', cmd.x, cmd.y);
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

    if (gameState == GameState::MAIN_MENU) {
        renderMenu();
    } else if (gameState == GameState::IN_GAME) {
        renderGame();
    }

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

void Game::renderMenu() {
    if (mainMenu) {
        mainMenu->render();
    }
}

void Game::renderGame() {
    // Draw board
    if (board) {
        board->render(renderer, CELL_SIZE, GRID_OFFSET_X, GRID_OFFSET_Y);
    }

    // Draw game UI
    renderImGui();
}

void Game::renderImGui() {

    // Game Status Window
    ImGui::SetNextWindowPos(ImVec2(500, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(50, 100), ImGuiCond_FirstUseEver);

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

    // Connection status
    ImGui::Text("Connection:");
    ImGui::SameLine();

    std::string netStatus;
    if (isServer) {
        int clientCount = gameServer ? gameServer->getClientCount() : 0;
        netStatus = "Server | Players: " + std::to_string(clientCount + 1) + "/2";

        if (clientDisconnected) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.2f, 1.0f));
            ImGui::Text("Client disconnected - Waiting...");
            ImGui::PopStyleColor();
        } else if (clientCount == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::Text("Waiting for player... (0/2)");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
            ImGui::Text("Connected (%d/2)", clientCount + 1);
            ImGui::PopStyleColor();
        }
    } else {
        netStatus = gameClient && gameClient->isConnected() ? "Connected to server" : "Connecting...";
        if (connectionState.isReconnecting) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.2f, 1.0f));
            ImGui::Text("Reconnecting... (%d/3)", connectionState.reconnectAttempts);
            ImGui::PopStyleColor();
        } else if (gameClient && gameClient->isConnected()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
            ImGui::Text("Connected");
            ImGui::PopStyleColor();
            connectionState.isConnected = true;
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::Text("Connecting...");
            ImGui::PopStyleColor();
        }
    }
    ImGui::Text("%s", netStatus.c_str());

    // Buttons
    ImGui::Separator();

    // Reset button
    if (ImGui::Button("Reset Game (R)")) {
        Command cmd{};
        cmd.type = CommandType::RESET_GAME;
        commandInputQueue.enqueue(cmd);
    }

    ImGui::SameLine();

    // Disconnect button
    if (ImGui::Button("Disconnect")) {
        stopGame();
    }

    // Render messages at the bottom of the status window
    renderMessages();

    ImGui::End();
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
                    addMessage("Move placed!", MessageType::SUCCESS);

                    // Send to network
                    NetworkPacket packet;
                    packet.type = PacketType::PLAYER_MOVE;
                    packet.data["x"] = cmd.x;
                    packet.data["y"] = cmd.y;
                    packet.data["mark"] = static_cast<int>(cmd.mark);

                    if (isServer && gameServer) {
                        printf("[LOGIC] Server broadcasting move to client\n");
                        gameServer->broadcastPacket(packet);
                    } else if (!isServer && gameClient) {
                        printf("[LOGIC] Client sending move to server\n");
                        gameClient->sendPacketToServer(packet);
                    }

                    // Check winner
                    localResult = board->checkWinner();

                    // Show win messages for BOTH local and network moves
                    if (localResult == GameResult::X_WINS) {
                        if (myMark == TileState::X) {
                            addMessage("🎉 You win!", MessageType::SUCCESS);
                        } else {
                            addMessage("X wins - You lose!", MessageType::ERROR);
                        }
                    } else if (localResult == GameResult::O_WINS) {
                        if (myMark == TileState::O) {
                            addMessage("🎉 You win!", MessageType::SUCCESS);
                        } else {
                            addMessage("O wins - You lose!", MessageType::ERROR);
                        }
                    } else if (localResult == GameResult::DRAW) {
                        addMessage("It's a draw!", MessageType::INFO);
                    }

                    // Switch turn
                    if (localResult == GameResult::IN_PROGRESS) {
                        localCurrentPlayer = (localCurrentPlayer == TileState::X) ? TileState::O : TileState::X;
                        printf("[LOGIC] Turn switched to %c\n", localCurrentPlayer == TileState::X ? 'X' : 'O');
                    }

                    createSnapshot(board->getGrid(), localCurrentPlayer, localResult, myMark);
                } else {
                    printf("[LOGIC] Invalid move\n");
                }

                // Note: We don't switch turns here - we wait for the network move to confirm the turn switch
            } else if (cmd.type == CommandType::NETWORK_MOVE) {
                if (board->setTile(cmd.x, cmd.y, cmd.mark)) {
                    std::cout << "[LOGIC] Applied network move: "
                              << (cmd.mark == TileState::X ? "X" : "O")
                              << " at (" << cmd.x << ", " << cmd.y << ")" << std::endl;

                    addMessage("Opponent moved!", MessageType::INFO);

                    // Check winner AFTER network move too
                    localResult = board->checkWinner();

                    if (localResult == GameResult::X_WINS) {
                        if (myMark == TileState::X) {
                            addMessage("🎉 You win!", MessageType::SUCCESS);
                        } else {
                            addMessage("X wins - You lose!", MessageType::ERROR);
                        }
                    } else if (localResult == GameResult::O_WINS) {
                        if (myMark == TileState::O) {
                            addMessage("🎉 You win!", MessageType::SUCCESS);
                        } else {
                            addMessage("O wins - You lose!", MessageType::ERROR);
                        }
                    } else if (localResult == GameResult::DRAW) {
                        addMessage("It's a draw!", MessageType::INFO);
                    }

                    // Switch turn after processing network move
                    if (localResult == GameResult::IN_PROGRESS) {
                        localCurrentPlayer = (localCurrentPlayer == TileState::X) ? TileState::O : TileState::X;
                    }


                    GameStateSnapshot snapshot = createSnapshot(board->getGrid(), localCurrentPlayer, localResult, myMark);
                    printf("a");

                    printf("[LOGIC] Client sent state after network move: player=%c, myTurn=%s\n",
                       localCurrentPlayer == TileState::X ? 'X' : 'O',
                       snapshot.isMyTurn ? "YES" : "NO");

                } else {
                    printf("[LOGIC] Failed to apply network move: Invalid position or tile already occupied");
                }


            } else if (cmd.type == CommandType::RESET_GAME) {
                printf("[LOGIC] Local reset, sending to network...\n");
                board->resetBoard();
                localCurrentPlayer = TileState::X;
                localResult = GameResult::IN_PROGRESS;

                addMessage("Game reset!", MessageType::INFO);

                // Immediately update render state
                createSnapshot(board->getGrid(), TileState::X, GameResult::IN_PROGRESS, TileState::X);
                printf("b");

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

                addMessage("Game reset by opponent!", MessageType::INFO);

                // Immediately update render state
                createSnapshot(board->getGrid(), TileState::X, GameResult::IN_PROGRESS, TileState::X);
                printf("c");

            } else if (cmd.type == CommandType::SYNC_STATE_REQUEST) {
                printf("[LOGIC] Syncing full state to clients...\n");

                if (isServer && gameServer && board) {
                    NetworkPacket syncPacket;
                    syncPacket.type = PacketType::GAME_STATE;

                    // Serialize board state
                    auto cells = board->getGrid();
                    std::vector<int> boardData;
                    for (int y = 0; y < 3; y++) {
                        for (int x = 0; x < 3; x++) {
                            boardData.push_back(static_cast<int>(cells[y][x]));
                        }
                    }

                    syncPacket.data["board"] = boardData;
                    syncPacket.data["currentPlayer"] = static_cast<int>(localCurrentPlayer);  // ⭐ CRITICAL
                    syncPacket.data["result"] = static_cast<int>(localResult);

                    gameServer->broadcastPacket(syncPacket);

                    printf("[LOGIC] Sent state sync: currentPlayer=%s, result=%d\n",
                           (localCurrentPlayer == TileState::X ? "X" : "O"),
                           static_cast<int>(localResult));
                }
            } else if (cmd.type == CommandType::SYNC_STATE_RECEIVED) {
                // Client received sync from server - update local state
                printf("[LOGIC] Received sync from network thread\n");

                localCurrentPlayer = cmd.mark;  // Passed in mark field
                localResult = board->checkWinner();

                // Determine if it's our turn based on the current player
                bool isMyTurn = (localCurrentPlayer == myMark);

                printf("[LOGIC] Updated local state: currentPlayer=%c\n",
                       localCurrentPlayer == TileState::X ? 'X' : 'O');

                // Send immediate state update
                GameStateSnapshot snapshot = createSnapshot(board->getGrid(), localCurrentPlayer, board->checkWinner(), myMark);
                printf("d");

                printf("[LOGIC] Sent updated state: isMyTurn=%s\n",
                       snapshot.isMyTurn ? "YES" : "NO");
            }
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

    int previousClientCount = 0;
    bool wasConnected = false;
    bool hasShownDisconnect = false;

    while (running) {
        // Update network
        if (isServer && gameServer) {
            gameServer->updateServer();

            // Track client connections
            int currentClientCount = gameServer->getClientCount();
            if (currentClientCount != previousClientCount) {
                if (currentClientCount > previousClientCount) {
                    addMessage("Player connected!", MessageType::SUCCESS);
                    clientDisconnected = false;
                    hasShownDisconnect = false;

                    std::thread([this]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));

                        // Request state sync from logic thread
                        Command syncCmd;
                        syncCmd.type = CommandType::SYNC_STATE_REQUEST;
                        commandInputQueue.enqueue(syncCmd);
                        printf("[NETWORK] New client connected, requested state sync from logic thread\n");
                        }).detach();
                    } else if (previousClientCount > 0 && currentClientCount == 0) {
                        // Only mark as disconnected if we had a client before (to avoid false positives on server start)

                        if (!hasShownDisconnect) {
                            addMessage("Player disconnected!", MessageType::WARNING);
                            addMessage("Client disconnected. Waiting for reconnection...", MessageType::WARNING);
                            clientDisconnected = true;
                            hasShownDisconnect = true;
                        }
                    }
                printf("[NETWORK] Client count changed: %d -> %d\n", previousClientCount, currentClientCount);
                previousClientCount = currentClientCount;
            }

            // Process incoming packets
            NetworkPacket packet;
            while (gameServer->incomingPackets.try_dequeue(packet)) {
                printf("[NETWORK] Server received packet type %d\n", static_cast<int>(packet.type));

                if (packet.type == PacketType::PLAYER_MOVE) {
                    int x = static_cast<int>(packet.data["x"]);
                    int y = static_cast<int>(packet.data["y"]);
                    auto mark = static_cast<TileState>(packet.data["mark"].get<int>());

                    Command cmd{};
                    cmd.type = CommandType::NETWORK_MOVE;
                    cmd.x = x;
                    cmd.y = y;
                    cmd.mark = mark;
                    commandInputQueue.enqueue(cmd);

                } else if (packet.type == PacketType::GAME_RESET) {
                    Command cmd;
                    cmd.type = CommandType::RESET_GAME;
                    cmd.fromNetwork = true;
                    commandInputQueue.enqueue(cmd);
                }
            }
        } else if (!isServer && gameClient) {
            gameClient->updateClient();

            // Track connection state with debouncing
            bool currentlyConnected = gameClient->isConnected();

            if (currentlyConnected && !wasConnected) {
                addMessage("Connected to server!", MessageType::SUCCESS);
                connectionState.isConnected = true;
                connectionState.isReconnecting = false;
                hasShownDisconnect = false;

            } else if (!currentlyConnected && wasConnected) {
                if (!hasShownDisconnect) {
                    addMessage("Lost connection!", MessageType::ERROR);
                    addMessage("Returning to menu...", MessageType::WARNING);
                    hasShownDisconnect = true;

                    std::thread([this]() {
                        std::this_thread::sleep_for(std::chrono::seconds(5));
                        if (running) {
                            stopGame();
                        }
                    }).detach();
                }
            }

            wasConnected = currentlyConnected;

            // Process incoming packets
            NetworkPacket packet;
            while (gameClient->incomingPackets.try_dequeue(packet)) {
                printf("[NETWORK] Client received packet type %d\n", static_cast<int>(packet.type));

                // Handle board state sync
                if (packet.type == PacketType::GAME_STATE) {
                    printf("[NETWORK] Received board state sync.\n");

                    if (packet.data.contains("board") && board) {
                        auto boardData = packet.data["board"].get<std::vector<int>>();

                        // Apply board state
                        board->resetBoard();
                        int idx = 0;
                        for (int y = 0; y < 3; y++) {
                            for (int x = 0; x < 3; x++) {
                                if (idx < boardData.size()) {
                                    TileState state = static_cast<TileState>(boardData[idx]);
                                    if (state != TileState::EMPTY) {
                                        board->setTile(x, y, state);
                                    }
                                }
                                idx++;
                            }
                        }

                        // Create snapshot for RENDER thread
                        if (packet.data.contains("currentPlayer")) {

                            GameStateSnapshot snapshot = createSnapshot(board->getGrid(),
                                           static_cast<TileState>(packet.data["currentPlayer"].get<int>()),
                                           packet.data.contains("result") ?
                                               static_cast<GameResult>(packet.data["result"].get<int>()) :
                                               GameResult::IN_PROGRESS,
                                           static_cast<TileState>(packet.data["currentPlayer"].get<int>()));
                            printf("f");

                            printf("[NETWORK] Synced state:\n");
                            printf("[NETWORK]   currentPlayer: %c\n",
                                   snapshot.currentPlayer == TileState::X ? 'X' : 'O');
                            printf("[NETWORK]   myMark: %c\n",
                                   myMark == TileState::X ? 'X' : 'O');
                            printf("[NETWORK]   isMyTurn: %s\n",
                                   snapshot.isMyTurn ? "YES" : "NO");

                            printf("[NETWORK] Updated currentRenderState directly!\n");


                            // ALSO send to logic thread so it knows the current turn
                            Command syncToLogic;
                            syncToLogic.type = CommandType::SYNC_STATE_RECEIVED;
                            syncToLogic.mark = snapshot.currentPlayer;  // Use mark field to pass player
                            commandInputQueue.enqueue(syncToLogic);
                            printf("[NETWORK] Sent sync to logic thread\n");

                            addMessage("Board and turn synchronized!", MessageType::SUCCESS);
                        }
                    }

                } else if (packet.type == PacketType::PLAYER_MOVE) {
                    int x = static_cast<int>(packet.data["x"]);
                    int y = static_cast<int>(packet.data["y"]);
                    auto mark = static_cast<TileState>(packet.data["mark"].get<int>());

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
        ImGui::SetCurrentContext(imguiContext);
        try {
            ImGui_ImplSDLRenderer3_Shutdown();
        } catch (std::exception& e) {
            std::cerr << "Exception during ImGui_ImplSDLRenderer3_Shutdown: " << e.what() << std::endl;
        }
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(imguiContext);
        imguiContext = nullptr;
    }

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

GameStateSnapshot Game::createSnapshot(std::array<std::array<TileState, 3>, 3> boardState, TileState currentPlayer, GameResult result, TileState mark) {
    GameStateSnapshot snapshot;
    snapshot.boardState = boardState;
    snapshot.currentPlayer = currentPlayer;
    snapshot.result = result;
    snapshot.isMyTurn = (currentPlayer == myMark);

    currentRenderState = snapshot; // Update render state immediately

    gameStateQueue.enqueue(snapshot);

    return snapshot;
}
