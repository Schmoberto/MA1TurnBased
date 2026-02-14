#include "NetworkManager.h"

// Global pointers for callbacks
static GameServer* g_GameServerCallback = nullptr;
static GameClient* g_GameClientCallback = nullptr;

// ============================================================================
//                     GameServer implementation
// ============================================================================

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

bool GameServer::startServer(uint16_t port) {
    SteamDatagramErrMsg errorMessage;
    if (!GameNetworkingSockets_Init(nullptr, errorMessage)) {
        std::cerr << "Failed to initialize SteamNetworkingSockets: " << errorMessage << std::endl;
        return false;
    }

    interface = SteamNetworkingSockets();
    g_GameServerCallback = this; // Set global pointer for callbacks

    SteamNetworkingIPAddr serverAddress{};
    serverAddress.Clear();
    serverAddress.m_port = port;

    SteamNetworkingConfigValue_t config{};
    config.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)SteamNetConnectionStatusChangedCallback);

    listenSocket = interface->CreateListenSocketIP(serverAddress, 1, &config);
    if (listenSocket == k_HSteamListenSocket_Invalid) {
        std::cerr << "[SERVER] Failed to create listen socket. " << std::endl;
        return false;
    }

    pollGroup = interface->CreatePollGroup();
    if (pollGroup == k_HSteamNetPollGroup_Invalid) {
        std::cerr << "[SERVER] Failed to create poll group. " << std::endl;
        return false;
    }
    running = true;
    std::cout << "[SERVER] Server started on port " << port << std::endl;
    return true;
}

void GameServer::stopServer() {
    if (!running) return;

    running = false;

    for (auto connection : clients) {
        interface->CloseConnection(connection, 0, "Server shutting down", false);
    }
    clients.clear();

    if (listenSocket != k_HSteamListenSocket_Invalid) {
        interface->CloseListenSocket(listenSocket);
        listenSocket = k_HSteamListenSocket_Invalid;
    }

    if (pollGroup != k_HSteamNetPollGroup_Invalid) {
        interface->DestroyPollGroup(pollGroup);
        pollGroup = k_HSteamNetPollGroup_Invalid;
    }

    GameNetworkingSockets_Kill();
    std::cout << "[SERVER] Server stopped." << std::endl;
}

void GameServer::updateServer() {
    if (!running) return;

    interface->RunCallbacks();
    receiveMessages();
}

void GameServer::receiveMessages() {


    ISteamNetworkingMessage* incomingMessage = nullptr;

    int numberOfMessagesReceived = interface->ReceiveMessagesOnPollGroup(pollGroup, &incomingMessage, 1);

    if (numberOfMessagesReceived > 0) {
        processMessage(incomingMessage->GetConnection(), incomingMessage->GetData(), static_cast<uint32_t>(incomingMessage->GetSize()));
        incomingMessage->Release();
    }
}

void GameServer::processMessage(HSteamNetConnection connection, const void *data, uint32_t size) {
    std::string message(static_cast<const char*>(data), size);

    try {
        NetworkPacket packet = NetworkPacket::deserialize(message);
        incomingPackets.enqueue(packet);
    } catch (const std::exception &e) {
        std::cerr << "[SERVER] Failed to process message: " << e.what() << std::endl;
    }
}

void GameServer::broadcastPacket(const NetworkPacket &packet) {
    std::string serialized = packet.serialize();

    for (auto conn : clients) {
        interface->SendMessageToConnection(
            conn, serialized.c_str(), serialized.size(),
            k_nSteamNetworkingSend_Reliable, nullptr);
    }
}

void GameServer::sendPacketToClient(HSteamNetConnection connection, const NetworkPacket &packet) {
    std::string serialized = packet.serialize();

    interface->SendMessageToConnection(
        connection, serialized.c_str(), serialized.size(),
        k_nSteamNetworkingSend_Reliable, nullptr);
}

void GameServer::SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t *info) {
    if (g_GameServerCallback) {
        g_GameServerCallback->onConnectionStatusChanged(info);
    }
}

void GameServer::onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *info) {
    switch (info->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connecting:
            if (clients.size() >= 2) {
                interface->CloseConnection(info->m_hConn, 0, "Server full", false);
                std::cout << "[SERVER] Rejected connection: Server full." << std::endl;
            } else if (interface->AcceptConnection(info->m_hConn) != k_EResultOK) {
                interface->CloseConnection(info->m_hConn, 0, "Failed to accept", false);
                std::cout << "[SERVER] Failed to accept connection." << std::endl;
            }
            break;

        case k_ESteamNetworkingConnectionState_Connected:
            std::cout << "[SERVER] Client connected (" << clients.size() + 1 << "/2, " << info->m_hConn << ")" << std::endl;
            clients.push_back(info->m_hConn);
            interface->SetConnectionPollGroup(info->m_hConn, pollGroup);
            break;

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            std::cout << "[SERVER] Client disconnected (" << info->m_hConn << ")." << std::endl;
            clients.erase(std::remove(clients.begin(), clients.end(), info->m_hConn), clients.end());
            interface->CloseConnection(info->m_hConn, 0, nullptr, false);
            break;

        default:
            break;
    }
}

// ============================================================================
//                     GameClient implementation
// ============================================================================

GameClient::GameClient()
    : serverConnection(k_HSteamNetConnection_Invalid)
    , interface(nullptr)
    , running(false)
    , connected(false) {
}

GameClient::~GameClient() {
    disconnectFromServer();
}

bool GameClient::connectToServer(const std::string &serverAddress, uint16_t port) {

    SteamDatagramErrMsg errorMessage;
    if (!GameNetworkingSockets_Init(nullptr, errorMessage)) {
        std::cerr << "[CLIENT] Failed to initialize GNS: " << errorMessage << std::endl;
        return false;
    }
    interface = SteamNetworkingSockets();
    g_GameClientCallback = this; // Set global pointer for callbacks


    SteamNetworkingIPAddr serverAddr;
    serverAddr.Clear();

    // Parse IP manually for localhost
    if (serverAddress == "127.0.0.1" || serverAddress == "localhost") {
        serverAddr.SetIPv4(0x7f000001, port);  // 127.0.0.1
    } else {
        // For other IPs, parse manually
        unsigned char ip[4];
        if (sscanf(serverAddress.c_str(), "%hhu.%hhu.%hhu.%hhu",
                   &ip[0], &ip[1], &ip[2], &ip[3]) == 4) {
            uint32_t ipv4 = (ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | ip[3];
            serverAddr.SetIPv4(ipv4, port);
        } else {
            std::cerr << "[CLIENT] Invalid IP address format: " << serverAddress << std::endl;
            return false;
        }
    }

    SteamNetworkingConfigValue_t config{};
    config.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
        (void*)SteamNetConnectionStatusChangedCallback);

    serverConnection = interface->ConnectByIPAddress(serverAddr, 1, &config);
    if (serverConnection == k_HSteamNetConnection_Invalid) {
        std::cerr << "[CLIENT] Failed to create connection" << std::endl;
        return false;
    }

    running = true;
    std::cout << "[CLIENT] Connecting to server at " << serverAddress << ":" << port << "..." << std::endl;


    /*
    SteamNetworkingIPAddr serverAddr{};
    if (!serverAddr.ParseString(serverAddress.c_str())) {
        std::cerr << "[CLIENT] Invalid server address: " << serverAddress << std::endl;
        return false;
    }

    serverAddr.m_port = port;

    SteamNetworkingConfigValue_t config{};
    config.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
        (void*)SteamNetConnectionStatusChangedCallback);

    serverConnection = interface->ConnectByIPAddress(serverAddr, 1, &config);
    if (serverConnection == k_HSteamNetConnection_Invalid) {
        std::cerr << "[CLIENT] Failed to create connection" << std::endl;
        return false;
    }

    running = true;
    std::cout << "[CLIENT] Connecting to server at " << serverAddress << ":" << port << "..." << std::endl;
    */
    return true;
}

void GameClient::disconnectFromServer() {
    running = false;
    connected = false;

    if (serverConnection != k_HSteamNetConnection_Invalid) {
        interface->CloseConnection(serverConnection, 0, "Client disconnected", false);
        serverConnection = k_HSteamNetConnection_Invalid;
    }

    GameNetworkingSockets_Kill();
    std::cout << "[CLIENT] Disconnected from server..." << std::endl;
}

void GameClient::updateClient() {
    if (!running) return;
    interface->RunCallbacks();
    receiveMessages();
}

void GameClient::receiveMessages() {
    ISteamNetworkingMessage* incomingMessage = nullptr;
    int numberMessages = interface->ReceiveMessagesOnConnection(serverConnection, &incomingMessage, 1);

    if (numberMessages > 0) {
        processMessage(incomingMessage->m_pData, incomingMessage->m_cbSize);
        incomingMessage->Release();
    }
}

void GameClient::processMessage(const void *data, uint32_t size) {
    std::string message(static_cast<const char*>(data), size);

    try {
        NetworkPacket packet = NetworkPacket::deserialize(message);
        incomingPackets.enqueue(packet);
    } catch (const std::exception &e) {
        std::cerr << "[CLIENT] Parse error: " << e.what() << std::endl;
    }
}

void GameClient::sendPacketToServer(const NetworkPacket &packet) const {
    if (!isConnected()) return;

    std::string serialized = packet.serialize();
    interface->SendMessageToConnection(
        serverConnection, serialized.c_str(), serialized.size(),
        k_nSteamNetworkingSend_Reliable, nullptr);
}

void GameClient::SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t *info) {
    if (g_GameClientCallback) {
        g_GameClientCallback->onConnectionStatusChanged(info);
    }
}

void GameClient::onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *info) {
    switch (info->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connected:
            std::cout << "[CLIENT] Connected to server." << std::endl;
            connected = true;
            break;

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            std::cout << "[CLIENT] Disconnected from server: " << info->m_info.m_szEndDebug << std::endl;
            connected = false;
            break;

        default:
            break;
    }
}

