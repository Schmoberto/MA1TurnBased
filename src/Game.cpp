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
#include <iostream>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <ctime>

/*-----------------------------------------------------------------------------
 *                          CONSTRUCTOR / DESTRUCTOR
*---------------------------------------------------------------------------*/

Game::Game()
    : window(nullptr), renderer(nullptr), board(nullptr), imguiContext(nullptr)
    , gameState(GameState::MAIN_MENU)
    , isServer(false), port(27015), serverAddress("127.0.0.1")
    , running(false)
    , myMark(TileState::EMPTY)
    , currentTurn(TileState::X) {
    std::cout << "[GAME] Constructor called" << std::endl;
}

Game::~Game() {
    cleanup();
}

/*-----------------------------------------------------------------------------
 *                          SDL CALLBACK FUNCTIONS
*---------------------------------------------------------------------------*/


/**
 *  SDL_AppInit: Initializes SDL, creates the main game instance, and sets up the initial state.
 *  - Initializes SDL video subsystem
 *  - Creates the main Game instance and initializes it
 *  - Sets the appstate pointer to the Game instance for use in other callbacks
 */
SDL_AppResult Game::AppInit(void** appstate, int argc, char* argv[]) {
    printf("[AppInit] Initializing SDL...\n");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        return SDL_APP_FAILURE;
    }

    // Create game instance (no command-line args, menu-based)
    Game* game = new Game();

    if (!game->initialize()) {
        delete game;
        return SDL_APP_FAILURE;
    }

    *appstate = game;
    return SDL_APP_CONTINUE;
}


/**
 *  SDL_AppIterate: Main loop iteration for rendering and game state updates.
 *  - Handles menu interactions and transitions to game state
 *  - Updates game state from logic thread via queue
 *  - Renders the current game state using SDL and ImGui
 *
 *  @param appstate pointer to the Game instance
 *  @return SDL_APP_SUCCESS to quit, SDL_APP_CONTINUE to keep running
 */
SDL_AppResult Game::AppIterate(void* appstate) {
    Game* game = static_cast<Game*>(appstate);

    if (!game) {
        std::cerr << "[AppIterate] Game pointer is null!" << std::endl;
        return SDL_APP_FAILURE;
    }

    // Handle minimized window (pause rendering)
    if (SDL_GetWindowFlags(game->window) & SDL_WINDOW_MINIMIZED) {
        SDL_WaitEvent(nullptr);
        return SDL_APP_CONTINUE;
    }

    // Process menu choices
    if (game->gameState == GameState::MAIN_MENU && game->mainMenu) {
        MenuChoice choice = game->mainMenu->getChoice();

        if (choice == MenuChoice::HOST_SERVER) {
            game->mainMenu->resetChoice();
            if (!game->startGame(true, "", game->mainMenu->getServerPort())) {
                std::cerr << "[MAIN MENU] Failed to start server game." << std::endl;
            }
        } else if (choice == MenuChoice::JOIN_SERVER) {
            game->mainMenu->resetChoice();
            if (!game->startGame(false, game->mainMenu->getServerIP(),
                                 game->mainMenu->getServerPort())) {
                std::cerr << "[MAIN MENU] Failed to join server." << std::endl;
            }
        } else if (choice == MenuChoice::QUIT) {
            return SDL_APP_SUCCESS;
        }
    }

    // Update game state from logic thread
    if (game->gameState == GameState::IN_GAME) {
        GameStateSnapshot newState;

        // Read latest state from queue
        while (game->gameStateQueue.try_dequeue(newState)) {
            game->currentRenderState = newState;
        }

        game->updateMessages();
    }

    game->render();
    return SDL_APP_CONTINUE;
}


/**
 *  SDL_AppEvent: Handles SDL events such as user input and window events.
 *  - Forwards events to ImGui for UI interaction
 *  - Processes game input events when in-game
 *
 * @param appstate pointer to the Game instance
 * @param event SDL_Event to process
 * @return SDL_APP_SUCCESS to quit, SDL_APP_CONTINUE to keep running
 */
SDL_AppResult Game::AppEvent(void* appstate, SDL_Event* event) {
    Game* game = static_cast<Game*>(appstate);

    if (!game) {
        std::cerr << "[AppEvent] Game pointer is null!" << std::endl;
        return SDL_APP_FAILURE;
    }

    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    if (game->imguiContext) {
        // Let ImGui process event first
        ImGui_ImplSDL3_ProcessEvent(event);

        // Don't process game input if ImGui wants it
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse && (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                                    event->type == SDL_EVENT_MOUSE_MOTION)) {
            return SDL_APP_CONTINUE;
        }
        if (io.WantCaptureKeyboard && event->type == SDL_EVENT_KEY_DOWN) {
            return SDL_APP_CONTINUE;
        }
    }

    // Process game events
    if (game->gameState == GameState::IN_GAME) {
        game->handleEvent(event);
    }

    return SDL_APP_CONTINUE;
}

/**
 * SDL_AppQuit: Cleans up resources and shuts down SDL when the application is quitting.
 *  - Deletes the Game instance
 *  - Calls SDL_Quit to clean up SDL subsystems
 *
 * @param appstate pointer to the Game instance
 * @param result the result code from the main loop (success or failure)
 */
void Game::AppQuit(void* appstate, SDL_AppResult result) {
    Game* game = static_cast<Game*>(appstate);

    if (game) {
        printf("[AppQuit] Cleaning up...\n");
        delete game;
    }

    SDL_Quit();
    printf("[AppQuit] Shutdown complete\n");
}

/*-----------------------------------------------------------------------------
 *                          INITIALIZATION
*---------------------------------------------------------------------------*/


/**
 * Initializes the game by creating the SDL window and renderer, and setting up ImGui.
 *  - Creates the main application window and renderer
 *  - Initializes ImGui context and sets up SDL3 bindings
 *  - Prepares the main menu for user interaction
 *
 * @return true if initialization succeeded, false otherwise
 */
bool Game::initialize() {
    printf("[AppInit] Creating window and renderer...\n");

    // Create window
    window = SDL_CreateWindow(
        "Multithreaded Networked Tic-Tac-Toe",
        WINDOW_WIDTH, WINDOW_HEIGHT, 0);

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Create renderer
    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Center window
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    // Initialize ImGui
    printf("[AppInit] Initializing ImGui...\n");

    try {
        IMGUI_CHECKVERSION();
        printf("-- [AppInit:ImGui] Creating ImGui context...\n");
        imguiContext = ImGui::CreateContext();

        if (!imguiContext) {
            std::cerr << "-- [AppInit:ImGui] Failed to create ImGui context." << std::endl;
            return false;
        }

        ImGui::SetCurrentContext(imguiContext);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

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
        std::cerr << "[AppInit:ImGui] Exception: " << e.what() << std::endl;
        return false;
    }

    printf("Game initialized successfully.\n");
    return true;
}

/*-----------------------------------------------------------------------------
 *                      GAME START / STOP
*---------------------------------------------------------------------------*/


/**
 * Starts the game by setting up the board, initializing network connections, and launching threads.
 *  - Configures game mode (server/client) and network parameters
 *  - Initializes the game board and render state
 *  - Starts the appropriate network server or client
 *  - Launches logic and network threads for game processing
 *
 * @param asServer true to start as server, false to start as client
 * @param serverAddr the server address to connect to (ignored if asServer is true)
 * @param port the port number for server or client connection
 * @return true if the game started successfully, false otherwise
 */
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
    currentRenderState.isMyTurn = isServer;  // Server goes first

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

    // Start game threads
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

/**
 * Stops the game by signaling threads to finish, cleaning up network connections, and resetting state.
 * - Sets running flag to false to signal threads to exit
 * - Waits for logic and network threads to finish
 * - Cleans up network resources and game board
 * - Clears message queues and resets game state to main menu
 */
void Game::stopGame() {
    printf("[GAME] Stopping game...\n");

    running = false;

    // Wait for threads to finish
    if (logicThread.joinable()) {
        logicThread.join();
    }

    if (networkThread.joinable()) {
        networkThread.join();
    }

    // Clean up network
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
    while (messageQueue.try_dequeue(msg)) {}

    gameState = GameState::MAIN_MENU;
    printf("[GAME] Game stopped. Returning to menu.\n");
}

/*-----------------------------------------------------------------------------
 *                      MESSAGE SYSTEM (UI Feedback)
*---------------------------------------------------------------------------*/
// -

/**
 * Adds a message to the UI message queue with a timestamp and type for color-coding.
 *  - Creates a UIMessage struct with the provided text and type
 *  - Enqueues the message into the thread-safe message queue for processing by the render thread
 *  - Logs the message to the console with a timestamp for debugging
 *
 * @param text the message text to display
 * @param type the type of message (INFO, SUCCESS, WARNING, ERROR) for color-coding
 */
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


/**
 *  Updates the active messages by adding new messages from the queue and removing old ones.
 *  - Dequeues new messages from the message queue and adds them to the activeMessages vector
 *  - Ensures that only the last MAX_MESSAGES are kept in the activeMessages vector
 *  - Removes messages that have been displayed for longer than MESSAGE_DURATION_MS to create a fade-out effect
 */
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

    // Remove old messages (fade out after MESSAGE_DURATION_MS)
    auto now = std::chrono::steady_clock::now();
    activeMessages.erase(
        std::remove_if(activeMessages.begin(), activeMessages.end(),
            [now, this](const UIMessage& m) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m.timestamp);
                return elapsed.count() > MESSAGE_DURATION_MS;
            }),
        activeMessages.end()
    );
}


/**
 * Renders the active messages in the ImGui interface with color-coding based on message type.
 *  - Displays a separator and "Messages:" header if there are active messages
 *  - Iterates through activeMessages and renders each message with a timestamp and color based on its type
 *  - Uses ImGui::TextWrapped to ensure messages fit within the UI layout
 */
void Game::renderMessages() {
    if (activeMessages.empty()) return;

    ImGui::Separator();
    ImGui::Text("Messages:");

    for (const auto& msg : activeMessages) {
        ImVec4 color;
        const char* prefix = "";

        // Color-code messages by type
        switch (msg.type) {
            case MessageType::INFO:
                color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                prefix = "[INFO] ";
                break;
            case MessageType::SUCCESS:
                color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
                prefix = "[✓] ";
                break;
            case MessageType::WARNING:
                color = ImVec4(0.9f, 0.7f, 0.2f, 1.0f);
                prefix = "[!] ";
                break;
            case MessageType::ERROR:
                color = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
                prefix = "[✗] ";
                break;
        }

        // Format timestamp HH:MM:SS
        auto time_c = std::chrono::system_clock::to_time_t(msg.systemTime);
        std::tm time_tm;
#ifdef _WIN32
        localtime_s(&time_tm, &time_c);
#else
        localtime_r(&time_c, &time_tm);
#endif

        char timeStr[32];
        std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &time_tm);

        // Render message with timestamp and color
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("[%s] %s%s", timeStr, prefix, msg.text.c_str());
        ImGui::PopStyleColor();
    }
}

/*-----------------------------------------------------------------------------
 *                      CONNECTION ERROR HANDLING
*---------------------------------------------------------------------------*/


/**
 * Handles disconnection events by updating connection state and providing user feedback.
 *  - If running as server, marks client as disconnected and shows a warning message
 *  - If running as client, initiates reconnection attempts and shows error messages
 *  - Implements an auto-disconnect after a timeout if reconnection fails to prevent hanging
 */
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

            // Auto-disconnect after timeout
            std::thread([this]() {
                std::this_thread::sleep_for(std::chrono::seconds(10));

                if (!connectionState.isConnected && running) {
                    addMessage("Could not reconnect. Returning to menu...", MessageType::ERROR);
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    stopGame();
                }
            }).detach();
        }
    }
}

/*-----------------------------------------------------------------------------
 *                      RENDER THREAD - INPUT HANDLING
*---------------------------------------------------------------------------*/


/**
 * Handles SDL events for user input during the game.
 *
 * @param event the SDL_Event to process
 */
void Game::handleEvent(SDL_Event* event) {
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event->button.button == SDL_BUTTON_LEFT) {
        float mouseX = event->button.x;
        float mouseY = event->button.y;
        handleMouseClick(static_cast<int>(mouseX), static_cast<int>(mouseY));
    }

    if (event->type == SDL_EVENT_KEY_DOWN) {
        handleKeyPress(event->key.key);
    }
}

/**
 * Handles mouse click events by validating the click position, checking game state, and enqueuing valid moves.
 *
 * @param mouseX
 * @param mouseY
 */
void Game::handleMouseClick(int mouseX, int mouseY) {
    printf("[RENDER] Mouse click at (%d, %d)\n", mouseX, mouseY);

    // Validate game state
    if (currentRenderState.result != GameResult::IN_PROGRESS) {
        printf("[RENDER] Game is over\n");
        addMessage("Game is over! Press Reset.", MessageType::INFO);
        return;
    }

    if (!currentRenderState.isMyTurn) {
        printf("[RENDER] Not your turn\n");
        addMessage("Not your turn!", MessageType::WARNING);
        return;
    }

    // Convert screen coordinates to grid position
    auto pos = board->screenToGrid(mouseX, mouseY, CELL_SIZE, GRID_OFFSET_X, GRID_OFFSET_Y);

    printf("[RENDER] Grid pos: (%d, %d) valid=%d\n", pos.x, pos.y, pos.valid);

    if (!pos.valid) {
        printf("[RENDER] Outside grid\n");
        addMessage("Click inside the grid!", MessageType::WARNING);
        return;
    }

    // Check if cell is empty
    if (!board) {
        std::cerr << "[RENDER] Board is null!" << std::endl;
        return;
    }

    TileState cellState = board->getTile(pos.x, pos.y);
    printf("[RENDER] Cell (%d, %d) state: %d\n", pos.x, pos.y, static_cast<int>(cellState));

    if (cellState != TileState::EMPTY) {
        printf("[RENDER] ✗ Cell occupied!\n");
        addMessage("Cell already occupied!", MessageType::ERROR);
        return;
    }

    // Valid move - send to logic thread
    Command cmd;
    cmd.type = CommandType::PLACE_MARK;
    cmd.x = pos.x;
    cmd.y = pos.y;
    cmd.mark = myMark;

    commandInputQueue.enqueue(cmd);
    printf("[RENDER] ✓ Enqueued valid move\n");
}

/**
 * Handles key press events for game controls such as resetting the game.
 *
 * @param key the SDL_Keycode of the pressed key
 */
void Game::handleKeyPress(SDL_Keycode key) {
    if (key == SDLK_R) {
        // Reset game
        Command cmd;
        cmd.type = CommandType::RESET_GAME;
        commandInputQueue.enqueue(cmd);
    }
}

/*-----------------------------------------------------------------------------
 *                      RENDER THREAD - DRAWING
*---------------------------------------------------------------------------*/


/**
 * Renders the current game state using SDL and ImGui.
 *  - Clears the screen and sets background color
 *  - Renders the game board and marks based on the current state
 *  - Displays game status and connection information using ImGui
 *  - Presents the rendered frame to the screen
 */
void Game::render() {
    if (!renderer || !imguiContext) {
        return;
    }

    ImGui::SetCurrentContext(imguiContext);

    // Start ImGui frame
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Clear screen
    SDL_SetRenderDrawColor(renderer, 50, 50, 60, 255);
    SDL_RenderClear(renderer);

    // Render based on game state
    if (gameState == GameState::MAIN_MENU) {
        renderMenu();
    } else if (gameState == GameState::IN_GAME) {
        renderGame();
    }

    // Render ImGui
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

    SDL_RenderPresent(renderer);
}

/**
 * Renders the main menu using ImGui.
 */
void Game::renderMenu() {
    if (mainMenu) {
        mainMenu->render();
    }
}

/**
 * Renders the game board and UI overlay during gameplay.
 */
void Game::renderGame() {
    // Draw game board
    if (board) {
        board->render(renderer, CELL_SIZE, GRID_OFFSET_X, GRID_OFFSET_Y);
    }

    // Draw UI overlay
    renderImGui();
}

/**
 * Renders the ImGui overlay for game status, connection information, and control buttons.
 */
void Game::renderImGui() {
    // Game Status Window
    ImGui::SetNextWindowPos(ImVec2(500, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(50, 100), ImGuiCond_FirstUseEver);

    ImGui::Begin("Game Status", nullptr, ImGuiWindowFlags_NoCollapse);

    // Display game result or current turn
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
        // Game in progress - show whose turn
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

        if (clientDisconnected) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.2f, 1.0f));
            ImGui::Text("Client disconnected");
            ImGui::PopStyleColor();
        } else if (clientCount == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::Text("Waiting... (0/2)");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
            ImGui::Text("Connected (%d/2)", clientCount + 1);
            ImGui::PopStyleColor();
        }
    } else {
        if (connectionState.isReconnecting) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.2f, 1.0f));
            ImGui::Text("Reconnecting...");
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

    ImGui::Separator();

    // Control buttons
    if (ImGui::Button("Reset Game (R)")) {
        Command cmd;
        cmd.type = CommandType::RESET_GAME;
        commandInputQueue.enqueue(cmd);
    }

    ImGui::SameLine();

    if (ImGui::Button("Disconnect")) {
        stopGame();
    }

    // Render timestamped messages
    renderMessages();

    ImGui::End();
}

/*-----------------------------------------------------------------------------
 *                      LOGIC THREAD - Game Rules
*---------------------------------------------------------------------------*/


/**
 * Main function for the logic thread that processes game commands, updates game state, and checks for win conditions.
 *  - Maintains a local copy of the current player and game result for processing
 *  - Processes commands from the commandInputQueue to handle player moves and network updates
 *  - Validates moves, updates the board, checks for winners, and switches turns as needed
 *  - Updates the currentRenderState and enqueues it for the render thread to display changes immediately
 */
void Game::logicThreadFunc() {
    std::cout << "[LOGIC] Thread started (ID: " << std::this_thread::get_id() << ")" << std::endl;

    TileState localCurrentPlayer = TileState::X;  // X always goes first
    GameResult localResult = GameResult::IN_PROGRESS;

    auto lastUpdateTime = std::chrono::steady_clock::now();

    while (running) {
        // Process incoming commands from render/network threads
        Command cmd;
        while (commandInputQueue.try_dequeue(cmd)) {
            printf("[LOGIC] Processing command: Type=%d, X=%d, Y=%d, Mark=%c\n",
                   static_cast<int>(cmd.type), cmd.x, cmd.y,
                   cmd.mark == TileState::X ? 'X' : 'O');

            // PLACE_MARK: Player makes a move (local)
            if (cmd.type == CommandType::PLACE_MARK) {
                // Validate move
                if (localResult == GameResult::IN_PROGRESS &&
                    cmd.mark == localCurrentPlayer &&
                    board->setTile(cmd.x, cmd.y, cmd.mark)) {

                    printf("[LOGIC] Placed %c at (%d, %d)\n",
                           cmd.mark == TileState::X ? 'X' : 'O', cmd.x, cmd.y);
                    addMessage("Move placed!", MessageType::SUCCESS);

                    // Send move to opponent via network
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

                    // Check for winner
                    localResult = board->checkWinner();

                    // Display win/draw messages
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

                    // Switch turn (if game continues)
                    if (localResult == GameResult::IN_PROGRESS) {
                        localCurrentPlayer = (localCurrentPlayer == TileState::X) ?
                                            TileState::O : TileState::X;
                        printf("[LOGIC] Turn switched to %c\n",
                               localCurrentPlayer == TileState::X ? 'X' : 'O');
                    }

                    // Update render state immediately
                    GameStateSnapshot snapshot;
                    snapshot.boardState = board->getGrid();
                    snapshot.currentPlayer = localCurrentPlayer;
                    snapshot.result = localResult;
                    snapshot.isMyTurn = (localCurrentPlayer == myMark);

                    currentRenderState = snapshot;
                    gameStateQueue.enqueue(snapshot);

                } else {
                    printf("[LOGIC] Invalid move\n");
                }

            // NETWORK_MOVE: Opponent made a move (received from network)
            } else if (cmd.type == CommandType::NETWORK_MOVE) {
                if (board->setTile(cmd.x, cmd.y, cmd.mark)) {
                    std::cout << "[LOGIC] Applied network move: "
                              << (cmd.mark == TileState::X ? "X" : "O")
                              << " at (" << cmd.x << ", " << cmd.y << ")" << std::endl;

                    addMessage("Opponent moved!", MessageType::INFO);

                    // Check winner
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

                    // Switch turn
                    if (localResult == GameResult::IN_PROGRESS) {
                        localCurrentPlayer = (localCurrentPlayer == TileState::X) ?
                                            TileState::O : TileState::X;
                    }

                    // Update render state immediately
                    GameStateSnapshot snapshot;
                    snapshot.boardState = board->getGrid();
                    snapshot.currentPlayer = localCurrentPlayer;
                    snapshot.result = localResult;
                    snapshot.isMyTurn = (localCurrentPlayer == myMark);

                    currentRenderState = snapshot;
                    gameStateQueue.enqueue(snapshot);

                } else {
                    printf("[LOGIC] Failed to apply network move\n");
                }

            // RESET_GAME: Player pressed Reset (local)
            } else if (cmd.type == CommandType::RESET_GAME) {
                printf("[LOGIC] Local reset, sending to network...\n");
                board->resetBoard();
                localCurrentPlayer = TileState::X;
                localResult = GameResult::IN_PROGRESS;

                addMessage("Game reset!", MessageType::INFO);

                // Send reset to network (if not already from network)
                if (!cmd.fromNetwork) {
                    NetworkPacket packet;
                    packet.type = PacketType::GAME_RESET;
                    if (isServer && gameServer) {
                        gameServer->broadcastPacket(packet);
                    } else if (!isServer && gameClient) {
                        gameClient->sendPacketToServer(packet);
                    }
                }

                // Update render state immediately
                GameStateSnapshot resetSnapshot;
                resetSnapshot.boardState = board->getGrid();
                resetSnapshot.currentPlayer = TileState::X;
                resetSnapshot.result = GameResult::IN_PROGRESS;
                resetSnapshot.isMyTurn = (TileState::X == myMark);

                currentRenderState = resetSnapshot;
                gameStateQueue.enqueue(resetSnapshot);

                printf("[LOGIC] Game reset\n");

            // NETWORK_RESET: Opponent reset the game
            } else if (cmd.type == CommandType::NETWORK_RESET) {
                printf("[LOGIC] Received network reset\n");
                board->resetBoard();
                localCurrentPlayer = TileState::X;
                localResult = GameResult::IN_PROGRESS;

                addMessage("Game reset by opponent!", MessageType::INFO);

                // Update render state immediately
                GameStateSnapshot resetSnapshot;
                resetSnapshot.boardState = board->getGrid();
                resetSnapshot.currentPlayer = TileState::X;
                resetSnapshot.result = GameResult::IN_PROGRESS;
                resetSnapshot.isMyTurn = (TileState::X == myMark);

                currentRenderState = resetSnapshot;
                gameStateQueue.enqueue(resetSnapshot);

            // SYNC_STATE_REQUEST: Server needs to send full state to new client
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
                    syncPacket.data["currentPlayer"] = static_cast<int>(localCurrentPlayer);
                    syncPacket.data["result"] = static_cast<int>(localResult);

                    gameServer->broadcastPacket(syncPacket);
                    printf("[LOGIC] State sync packet sent!\n");
                }

            // SYNC_STATE_RECEIVED: Client received full state from server
            } else if (cmd.type == CommandType::SYNC_STATE_RECEIVED) {
                printf("[LOGIC] Received sync from network thread\n");

                // Update local state from sync
                localCurrentPlayer = cmd.mark;  // Passed via mark field
                localResult = board->checkWinner();

                printf("[LOGIC] Updated local state: currentPlayer=%c\n",
                       localCurrentPlayer == TileState::X ? 'X' : 'O');

                // Update render state immediately
                GameStateSnapshot snapshot;
                snapshot.boardState = board->getGrid();
                snapshot.currentPlayer = localCurrentPlayer;
                snapshot.result = localResult;
                snapshot.isMyTurn = (localCurrentPlayer == myMark);

                currentRenderState = snapshot;
                gameStateQueue.enqueue(snapshot);

                printf("[LOGIC] Sent updated state: isMyTurn=%s\n",
                       snapshot.isMyTurn ? "YES" : "NO");
            }
        }

        // Small sleep to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    printf("[LOGIC] Thread exiting...\n");
}

/*-----------------------------------------------------------------------------
 *                      NETWORK THREAD - Communication
 *---------------------------------------------------------------------------*/

/**
 * Main function for the network thread that handles server/client communication, processes incoming packets, and manages connection state.
 *  - For server: Handles client connections, broadcasts moves, and processes incoming packets from clients
 *  - For client: Manages connection to server, processes incoming packets for game state updates, and tracks connection status
 *  - Implements connection tracking and user feedback for disconnections and reconnections
 */
void Game::networkThreadFunc() {
    std::cout << "[NETWORK] Thread started (ID: " << std::this_thread::get_id() << ")" << std::endl;
    std::cout << "[NETWORK] Mode: " << (isServer ? "SERVER" : "CLIENT") << std::endl;

    int previousClientCount = 0;
    bool wasConnected = false;
    bool hasShownDisconnect = false;

    while (running) {
        // SERVER: Handle client connections and broadcasts
        if (isServer && gameServer) {
            gameServer->updateServer();

            // Track client connections
            int currentClientCount = gameServer->getClientCount();
            if (currentClientCount != previousClientCount) {
                if (currentClientCount > previousClientCount) {
                    addMessage("Player connected!", MessageType::SUCCESS);
                    clientDisconnected = false;
                    hasShownDisconnect = false;

                    // Request state sync after short delay (let connection stabilize)
                    std::thread([this]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));

                        Command syncCmd;
                        syncCmd.type = CommandType::SYNC_STATE_REQUEST;
                        commandInputQueue.enqueue(syncCmd);
                        printf("[NETWORK] Requested state sync (delayed)\n");
                    }).detach();

                } else if (previousClientCount > 0 && currentClientCount == 0) {
                    if (!hasShownDisconnect) {
                        addMessage("Player disconnected!", MessageType::WARNING);
                        clientDisconnected = true;
                        hasShownDisconnect = true;
                    }
                }

                previousClientCount = currentClientCount;
            }

            // Process incoming packets from clients
            NetworkPacket packet;
            while (gameServer->incomingPackets.try_dequeue(packet)) {
                printf("[NETWORK] Server received packet type %d\n", static_cast<int>(packet.type));

                if (packet.type == PacketType::PLAYER_MOVE) {
                    int x = static_cast<int>(packet.data["x"]);
                    int y = static_cast<int>(packet.data["y"]);
                    auto mark = static_cast<TileState>(packet.data["mark"].get<int>());

                    printf("[NETWORK] Server processing client move: %c at (%d, %d)\n",
                           mark == TileState::X ? 'X' : 'O', x, y);

                    // Forward to logic thread
                    Command cmd{};
                    cmd.type = CommandType::NETWORK_MOVE;
                    cmd.x = x;
                    cmd.y = y;
                    cmd.mark = mark;
                    commandInputQueue.enqueue(cmd);

                } else if (packet.type == PacketType::GAME_RESET) {
                    printf("[NETWORK] Server received reset (not echoing)\n");
                    Command cmd;
                    cmd.type = CommandType::NETWORK_RESET;
                    commandInputQueue.enqueue(cmd);
                }
            }

        // CLIENT: Handle server connection and messages
        } else if (!isServer && gameClient) {
            gameClient->updateClient();

            bool currentlyConnected = gameClient->isConnected();

            // Track connection state changes
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

                    // Auto-disconnect after timeout
                    std::thread([this]() {
                        std::this_thread::sleep_for(std::chrono::seconds(5));
                        if (running) {
                            stopGame();
                        }
                    }).detach();
                }
            }

            wasConnected = currentlyConnected;

            // Process incoming packets from server
            NetworkPacket packet;
            while (gameClient->incomingPackets.try_dequeue(packet)) {
                printf("[NETWORK] Client received packet type %d\n", static_cast<int>(packet.type));

                // GAME_STATE: Full board sync (for late joiners)
                if (packet.type == PacketType::GAME_STATE) {
                    printf("[NETWORK] RECEIVED GAME STATE SYNC\n");

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

                        // Create snapshot for render thread
                        if (packet.data.contains("currentPlayer")) {
                            GameStateSnapshot snapshot;
                            snapshot.boardState = board->getGrid();
                            snapshot.currentPlayer = static_cast<TileState>(
                                packet.data["currentPlayer"].get<int>());
                            snapshot.result = packet.data.contains("result") ?
                                static_cast<GameResult>(packet.data["result"].get<int>()) :
                                GameResult::IN_PROGRESS;
                            snapshot.isMyTurn = (snapshot.currentPlayer == myMark);

                            printf("[NETWORK] Synced state:\n");
                            printf("[NETWORK]   currentPlayer: %c\n",
                                   snapshot.currentPlayer == TileState::X ? 'X' : 'O');
                            printf("[NETWORK]   myMark: %c\n",
                                   myMark == TileState::X ? 'X' : 'O');
                            printf("[NETWORK]   isMyTurn: %s\n",
                                   snapshot.isMyTurn ? "YES" : "NO");

                            // Update render state immediately (no queue delay)
                            currentRenderState = snapshot;
                            printf("[NETWORK] Updated currentRenderState directly!\n");

                            gameStateQueue.enqueue(snapshot);

                            // Also notify logic thread to update its local state
                            Command syncToLogic;
                            syncToLogic.type = CommandType::SYNC_STATE_RECEIVED;
                            syncToLogic.mark = snapshot.currentPlayer;
                            commandInputQueue.enqueue(syncToLogic);
                            printf("[NETWORK] Sent sync to logic thread\n");

                            addMessage("Board and turn synchronized!", MessageType::SUCCESS);
                        }
                    }

                // PLAYER_MOVE: Opponent made a move
                } else if (packet.type == PacketType::PLAYER_MOVE) {
                    int x = static_cast<int>(packet.data["x"]);
                    int y = static_cast<int>(packet.data["y"]);
                    auto mark = static_cast<TileState>(packet.data["mark"].get<int>());

                    printf("[NETWORK] Client processing server move: %c at (%d, %d)\n",
                           mark == TileState::X ? 'X' : 'O', x, y);

                    Command cmd{};
                    cmd.type = CommandType::NETWORK_MOVE;
                    cmd.x = x;
                    cmd.y = y;
                    cmd.mark = mark;
                    commandInputQueue.enqueue(cmd);

                // GAME_RESET: Opponent reset the game
                } else if (packet.type == PacketType::GAME_RESET) {
                    printf("[NETWORK] Client received reset (not echoing)\n");
                    Command cmd;
                    cmd.type = CommandType::RESET_GAME;
                    cmd.fromNetwork = true;
                    commandInputQueue.enqueue(cmd);
                }
            }
        }

        // Sleep to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    printf("[NETWORK] Thread exiting...\n");
}

/*-----------------------------------------------------------------------------
 *                          CLEANUP
*---------------------------------------------------------------------------*/


/**
 * Cleans up resources and stops the game.
 */
void Game::cleanup() {
    running = false;

    // Stop game threads if running
    if (logicThread.joinable()) {
        logicThread.join();
    }
    if (networkThread.joinable()) {
        networkThread.join();
    }

    // Clean up ImGui (skip to avoid double-free issues)
    imguiContext = nullptr;

    // Clean up SDL
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }

    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    std::cout << "[GAME] Cleanup complete" << std::endl;
}