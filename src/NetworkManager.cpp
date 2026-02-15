/*******************************************************************************
 * NetworkManager.cpp
 * 
 * Handles all network communication for the multiplayer Tic-Tac-Toe game.
 * Uses Valve's GameNetworkingSockets library for reliable TCP-like communication.
 * 
 * Architecture:
 * - GameServer: Accepts up to 2 clients, broadcasts game state
 * - GameClient: Connects to server, sends/receives moves
 * - NetworkPacket: JSON-serialized messages for cross-network communication
 ******************************************************************************/

#include "NetworkManager.h"

// Global callback pointers for GameNetworkingSockets
// (Library requires static callbacks, these point to actual instances)
static GameServer* g_GameServerCallback = nullptr;
static GameClient* g_GameClientCallback = nullptr;

/*******************************************************************************
 *                           SERVER IMPLEMENTATION
 ******************************************************************************/

GameServer::GameServer(uint16_t port)
    : listenSocket(k_HSteamListenSocket_Invalid)
    , pollGroup(k_HSteamNetPollGroup_Invalid)
    , interface(nullptr)
    , port(port)
    , running(false) {
}

GameServer::~GameServer() {
    stopServer();
}

/*-----------------------------------------------------------------------------
 *                          Server Initialization
 *---------------------------------------------------------------------------*/

/**
 * Initializes the game server by setting up the GameNetworkingSockets library, creating a listen socket, and configuring callbacks for connection status changes.
 * Listens on the specified port for incoming client connections and prepares to handle messages.
 *
 * @param port The port number to listen on for incoming connections
 * @return true if the server started successfully, false if there was an error during initialization
 */
bool GameServer::startServer(uint16_t port) {
    // Initialize GameNetworkingSockets library
    SteamDatagramErrMsg errorMessage;
    if (!GameNetworkingSockets_Init(nullptr, errorMessage)) {
        std::cerr << "[SERVER] Failed to initialize: " << errorMessage << std::endl;
        return false;
    }

    interface = SteamNetworkingSockets();
    g_GameServerCallback = this;

    // Configure server address (listen on all interfaces)
    SteamNetworkingIPAddr serverAddress{};
    serverAddress.Clear();
    serverAddress.m_port = port;

    // Set up connection status callback
    SteamNetworkingConfigValue_t config{};
    config.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, 
                  (void*)SteamNetConnectionStatusChangedCallback);

    // Create listen socket
    listenSocket = interface->CreateListenSocketIP(serverAddress, 1, &config);
    if (listenSocket == k_HSteamListenSocket_Invalid) {
        std::cerr << "[SERVER] Failed to create listen socket" << std::endl;
        return false;
    }

    // Create poll group for efficient message receiving
    pollGroup = interface->CreatePollGroup();
    if (pollGroup == k_HSteamNetPollGroup_Invalid) {
        std::cerr << "[SERVER] Failed to create poll group" << std::endl;
        return false;
    }

    running = true;
    std::cout << "[SERVER] Started on port " << port << std::endl;
    return true;
}

/*-----------------------------------------------------------------------------
 *                              Server Shutdown
 *---------------------------------------------------------------------------*/


/**
 * Stops the game server by closing all client connections, cleaning up network resources, and shutting down the GameNetworkingSockets library.
 * Ensures that all connections are closed gracefully and that the server is properly cleaned up to prevent resource leaks.
 */
void GameServer::stopServer() {
    if (!running) return;
    running = false;

    // Close all client connections gracefully
    for (auto connection : clients) {
        interface->CloseConnection(connection, 0, "Server shutting down", false);
    }
    clients.clear();

    // Clean up network resources
    if (listenSocket != k_HSteamListenSocket_Invalid) {
        interface->CloseListenSocket(listenSocket);
        listenSocket = k_HSteamListenSocket_Invalid;
    }

    if (pollGroup != k_HSteamNetPollGroup_Invalid) {
        interface->DestroyPollGroup(pollGroup);
        pollGroup = k_HSteamNetPollGroup_Invalid;
    }

    GameNetworkingSockets_Kill();
    std::cout << "[SERVER] Stopped" << std::endl;
}

/*-----------------------------------------------------------------------------
 *              Server Update Loop (called every frame)
 *---------------------------------------------------------------------------*/

/**
 * Updates the server by processing connection events and receiving incoming messages from clients.
 */
void GameServer::updateServer() {
    if (!running) return;

    // Process connection state changes and events
    interface->RunCallbacks();
    
    // Receive and process incoming messages
    receiveMessages();
}

/*-----------------------------------------------------------------------------
 *                          Message Reception
 *---------------------------------------------------------------------------*/

/**
 * Receives incoming messages from clients by polling the network interface.
 * Processes each message and adds it to a concurrent queue for the game logic thread to handle.
 * Continues to receive messages until there are no more pending messages in the queue.
 */
void GameServer::receiveMessages() {
    while (true) {
        ISteamNetworkingMessage* incomingMessage = nullptr;
        int numberOfMessagesReceived = interface->ReceiveMessagesOnPollGroup(
            pollGroup, &incomingMessage, 1);

        if (numberOfMessagesReceived <= 0) {
            break; // No more messages
        }

        // Process message and add to queue for game logic thread
        processMessage(incomingMessage->GetConnection(),
                      incomingMessage->GetData(),
                      static_cast<uint32_t>(incomingMessage->GetSize()));
        
        incomingMessage->Release();
    }
}

/**
 * Processes an incoming message from a client by deserializing the JSON data into a NetworkPacket and adding it to the incomingPackets queue for the game logic thread to process.
 * Handles any exceptions that may occur during deserialization and logs errors appropriately.
 *
 * @param connection The Steam networking connection handle from which the message was received
 * @param data Pointer to the raw message data
 * @param size Size of the message data in bytes
 */
void GameServer::processMessage(HSteamNetConnection connection, const void *data, uint32_t size) {
    std::string message(static_cast<const char*>(data), size);

    try {
        // Deserialize JSON packet
        NetworkPacket packet = NetworkPacket::deserialize(message);
        incomingPackets.enqueue(packet);
    } catch (const std::exception &e) {
        std::cerr << "[SERVER] Failed to process message: " << e.what() << std::endl;
    }
}

/*-----------------------------------------------------------------------------
 *                              Message Sending
 *---------------------------------------------------------------------------*/

/**
 * Broadcasts a NetworkPacket to all connected clients by serializing the packet to JSON and sending it reliably through the Steam networking interface.
 * Logs any errors that occur during message sending for each client connection.
 *
 * @param packet The NetworkPacket to broadcast to all clients
 */
void GameServer::broadcastPacket(const NetworkPacket &packet) {
    std::string serialized = packet.serialize();

    // Send to all connected clients
    for (auto conn : clients) {
        EResult result = interface->SendMessageToConnection(
            conn, serialized.c_str(), serialized.size(),
            k_nSteamNetworkingSend_Reliable, nullptr);

        if (result != k_EResultOK) {
            std::cerr << "[SERVER] Failed to send to connection " << conn << std::endl;
        }
    }
}

/**
 * Sends a NetworkPacket to a specific client connection by serializing the packet to JSON and sending it reliably through the Steam networking interface.
 * Logs any errors that occur during message sending to the specified client connection.
 *
 * @param connection The Steam networking connection handle to which the packet should be sent
 * @param packet The NetworkPacket to send to the specified client
 */
void GameServer::sendPacketToClient(HSteamNetConnection connection, const NetworkPacket &packet) {
    std::string serialized = packet.serialize();

    interface->SendMessageToConnection(
        connection, serialized.c_str(), serialized.size(),
        k_nSteamNetworkingSend_Reliable, nullptr);
}

/*-----------------------------------------------------------------------------
 *                      Connection Status Callbacks
 *---------------------------------------------------------------------------*/

/**
 * Static callback function for handling connection status changes from the Steam networking library.
 * Forwards the callback to the GameServer instance's onConnectionStatusChanged method for processing.
 *
 * @param info Pointer to the SteamNetConnectionStatusChangedCallback_t structure containing information about the connection status change event
 */
void GameServer::SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t *info) {
    if (g_GameServerCallback) {
        g_GameServerCallback->onConnectionStatusChanged(info);
    }
}


/**
 * Handles connection status changes by logging the new state and managing the list of connected clients accordingly.
 * Accepts new connections if the server is not full, and closes connections that are rejected or encounter problems.
 *
 * @param info Pointer to the SteamNetConnectionStatusChangedCallback_t structure containing information about the connection status change event
 */
void GameServer::onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *info) {
    printf("[SERVER] Connection state=%d, handle=%d\n", 
           info->m_info.m_eState, info->m_hConn);

    switch (info->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_None:
            std::cout << "[SERVER] Connection closed" << std::endl;
            break;

        case k_ESteamNetworkingConnectionState_Connecting:
            std::cout << "[SERVER] Client attempting to connect..." << std::endl;

            // Enforce 2-player limit
            if (clients.size() >= 2) {
                interface->CloseConnection(info->m_hConn, 0, "Server full", false);
                std::cout << "[SERVER] Rejected: Server full" << std::endl;
            } else {
                // Accept connection
                EResult result = interface->AcceptConnection(info->m_hConn);
                if (result != k_EResultOK) {
                    interface->CloseConnection(info->m_hConn, 0, "Failed to accept", false);
                    std::cout << "[SERVER] Failed to accept: " << result << std::endl;
                } else {
                    std::cout << "[SERVER] Connection accepted" << std::endl;
                }
            }
            break;

        case k_ESteamNetworkingConnectionState_FindingRoute:
            std::cout << "[SERVER] Finding route to client..." << std::endl;
            break;

        case k_ESteamNetworkingConnectionState_Connected:
            std::cout << "[SERVER] Client fully connected! (Handle: " << info->m_hConn << ")" << std::endl;

            // Add to clients list (avoid duplicates)
            if (std::find(clients.begin(), clients.end(), info->m_hConn) == clients.end()) {
                clients.push_back(info->m_hConn);
                interface->SetConnectionPollGroup(info->m_hConn, pollGroup);
                std::cout << "[SERVER] Total clients: " << clients.size() << "/2" << std::endl;
            }
            break;

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
            std::cout << "[SERVER] Client disconnected: " << info->m_info.m_szEndDebug << std::endl;
            clients.erase(std::remove(clients.begin(), clients.end(), info->m_hConn), clients.end());
            break;

        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            std::cout << "[SERVER] Connection problem: " << info->m_info.m_szEndDebug << std::endl;
            clients.erase(std::remove(clients.begin(), clients.end(), info->m_hConn), clients.end());
            break;

        default:
            std::cout << "[SERVER] Unknown state: " << info->m_info.m_eState << std::endl;
            break;
    }
}

/*******************************************************************************
 *                           CLIENT IMPLEMENTATION
 ******************************************************************************/

GameClient::GameClient()
    : serverConnection(k_HSteamNetConnection_Invalid)
    , interface(nullptr)
    , running(false)
    , connected(false) {
}

GameClient::~GameClient() {
    disconnectFromServer();
}

/*-----------------------------------------------------------------------------
 *                              Client Connection
 *---------------------------------------------------------------------------*/

/**
 * Connects to the game server at the specified address and port by initializing the GameNetworkingSockets library, configuring connection parameters, and initiating a connection to the server.
 * Handles both localhost and remote IP addresses, and sets up callbacks for connection status changes.
 *
 * @param serverAddress The IP address or hostname of the server to connect to
 * @param port The port number to connect to on the server
 * @return true if the connection was initiated successfully, false if there was an error during initialization or connection setup
 */
bool GameClient::connectToServer(const std::string &serverAddress, uint16_t port) {
    // Initialize GameNetworkingSockets library
    SteamDatagramErrMsg errorMessage;
    if (!GameNetworkingSockets_Init(nullptr, errorMessage)) {
        std::cerr << "[CLIENT] Failed to initialize: " << errorMessage << std::endl;
        return false;
    }

    interface = SteamNetworkingSockets();
    g_GameClientCallback = this;

    // Parse server address
    SteamNetworkingIPAddr serverAddr;
    serverAddr.Clear();

    if (serverAddress == "127.0.0.1" || serverAddress == "localhost") {
        // Localhost connection
        serverAddr.SetIPv4(0x7f000001, port);
        printf("[CLIENT] Connecting to localhost:%d\n", port);
    } else {
        // Parse IP address manually (supports IPv4)
        unsigned char ip[4];
        if (sscanf(serverAddress.c_str(), "%hhu.%hhu.%hhu.%hhu",
                   &ip[0], &ip[1], &ip[2], &ip[3]) == 4) {
            uint32_t ipv4 = (ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | ip[3];
            serverAddr.SetIPv4(ipv4, port);
            printf("[CLIENT] Connecting to %s:%d\n", serverAddress.c_str(), port);
        } else {
            std::cerr << "[CLIENT] Invalid IP format: " << serverAddress << std::endl;
            return false;
        }
    }

    // Set up connection status callback
    SteamNetworkingConfigValue_t config{};
    config.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                  (void*)SteamNetConnectionStatusChangedCallback);

    // Initiate connection
    serverConnection = interface->ConnectByIPAddress(serverAddr, 1, &config);
    if (serverConnection == k_HSteamNetConnection_Invalid) {
        std::cerr << "[CLIENT] Failed to create connection" << std::endl;
        return false;
    }

    running = true;
    std::cout << "[CLIENT] Connection initiated..." << std::endl;
    return true;
}

/*-----------------------------------------------------------------------------
 *                          Client Disconnection
 *---------------------------------------------------------------------------*/

/**
 * Disconnects from the game server by closing the connection, cleaning up network resources, and shutting down the GameNetworkingSockets library.
 * Ensures that the connection is closed gracefully and that all resources are properly released to prevent leaks.
 */
void GameClient::disconnectFromServer() {
    running = false;
    connected = false;

    if (serverConnection != k_HSteamNetConnection_Invalid) {
        interface->CloseConnection(serverConnection, 0, "Client disconnected", false);
        serverConnection = k_HSteamNetConnection_Invalid;
    }

    GameNetworkingSockets_Kill();
    std::cout << "[CLIENT] Disconnected" << std::endl;
}

/*-----------------------------------------------------------------------------
 *                          Client Update Loop
 *---------------------------------------------------------------------------*/

/**
 * Updates the client by processing connection events and receiving incoming messages from the server.
 */
void GameClient::updateClient() {
    if (!running) return;

    // Process connection state changes
    interface->RunCallbacks();
    
    // Receive messages from server
    receiveMessages();
}

/*-----------------------------------------------------------------------------
 *                          Message Reception
 *---------------------------------------------------------------------------*/

/**
 * Receives incoming messages from the server by polling the network interface.
 * Processes each message and adds it to a concurrent queue for the game logic thread to handle.
 */
void GameClient::receiveMessages() {
    while (true) {
        ISteamNetworkingMessage* incomingMessage = nullptr;
        int numberMessages = interface->ReceiveMessagesOnConnection(
            serverConnection, &incomingMessage, 1);

        if (numberMessages <= 0) {
            break; // No more messages
        }

        // Process message and add to queue
        processMessage(incomingMessage->m_pData, incomingMessage->m_cbSize);
        incomingMessage->Release();
    }
}

/**
 * Processes an incoming message from the server by deserializing the JSON data into a NetworkPacket and adding it to the incomingPackets queue for the game logic thread to process.
 * Handles any exceptions that may occur during deserialization and logs errors appropriately.
 *
 * @param data Pointer to the raw message data received from the server
 * @param size Size of the message data in bytes
 */
void GameClient::processMessage(const void *data, uint32_t size) {
    std::string message(static_cast<const char*>(data), size);

    try {
        // Deserialize JSON packet
        NetworkPacket packet = NetworkPacket::deserialize(message);
        incomingPackets.enqueue(packet);
    } catch (const std::exception &e) {
        std::cerr << "[CLIENT] Parse error: " << e.what() << std::endl;
    }
}

/*-----------------------------------------------------------------------------
 *                              Message Sending
 *---------------------------------------------------------------------------*/

/**
 * Sends a NetworkPacket to the server by serializing the packet to JSON and sending it reliably through the Steam networking interface.
 * Logs any errors that occur during message sending.
 *
 * @param packet The NetworkPacket to send to the server
 */
void GameClient::sendPacketToServer(const NetworkPacket &packet) const {
    if (!isConnected()) {
        std::cerr << "[CLIENT] Cannot send - not connected" << std::endl;
        return;
    }

    std::string serialized = packet.serialize();
    EResult result = interface->SendMessageToConnection(
        serverConnection, serialized.c_str(), serialized.size(),
        k_nSteamNetworkingSend_Reliable, nullptr);

    if (result != k_EResultOK) {
        std::cerr << "[CLIENT] Failed to send packet: " << result << std::endl;
    }
}

/*-----------------------------------------------------------------------------
 *                          Connection Status Callbacks
 *---------------------------------------------------------------------------*/

/**
 * Static callback function for handling connection status changes from the Steam networking library.
 * Forwards the callback to the GameClient instance's onConnectionStatusChanged method for processing.
 *
 * @param info Pointer to the SteamNetConnectionStatusChangedCallback_t structure containing information about the connection status change event
 */
void GameClient::SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t *info) {
    if (g_GameClientCallback) {
        g_GameClientCallback->onConnectionStatusChanged(info);
    }
}

/**
 * Handles connection status changes by logging the new state and updating the client's connection status accordingly.
 * Manages connection states such as connecting, connected, disconnected, and connection problems, and provides user feedback through console logs.
 *
 * @param info Pointer to the SteamNetConnectionStatusChangedCallback_t structure containing information about the connection status change event
 */
void GameClient::onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *info) {
    std::cout << "[CLIENT] Connection state=" << info->m_info.m_eState << std::endl;

    switch (info->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_None:
            connected = false;
            break;

        case k_ESteamNetworkingConnectionState_Connecting:
            std::cout << "[CLIENT] Connecting..." << std::endl;
            break;

        case k_ESteamNetworkingConnectionState_FindingRoute:
            std::cout << "[CLIENT] Finding route..." << std::endl;
            break;

        case k_ESteamNetworkingConnectionState_Connected:
            std::cout << "[CLIENT] ✓ Connected to server!" << std::endl;
            connected = true;
            break;

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
            std::cout << "[CLIENT] Server closed connection: " << info->m_info.m_szEndDebug << std::endl;
            connected = false;
            running = false;
            break;

        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            std::cout << "[CLIENT] Connection problem: " << info->m_info.m_szEndDebug << std::endl;
            connected = false;
            running = false;
            break;

        default:
            break;
    }
}