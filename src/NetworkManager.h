#pragma once

#include <steam/steamnetworkingsockets.h>
#include <steam/steamnetworkingtypes.h>
#include <string>
#include <atomic>
#include <iostream>
#include <vector>
#include <nlohmann/json.hpp>
#include <moodycamel/concurrentqueue.h>

using json = nlohmann::json;

enum class PacketType {
    PLAYER_MOVE,        // Client sends a move to the server
    GAME_STATE_UPDATE,  // Server sends updated game state to clients
    GAME_RESET,         // Signal to reset the game state
    PLAYER_JOINED,      // New player joined the lobby
    CHAT_MESSAGE
};

struct NetworkPacket {
    PacketType type;
    json data;

    std::string serialize() const {
        json packetJson;
        packetJson["type"] = static_cast<int>(type);
        packetJson["data"] = data;
        return packetJson.dump();
    }

    static NetworkPacket deserialize(const std::string& packetStr) {
        NetworkPacket packet;

        try {
            auto packetJson = json::parse(packetStr);
            packet.type = static_cast<PacketType>(packetJson["type"].get<int>());
            packet.data = packetJson["data"];
        } catch (const json::parse_error& e) {
            // Handle JSON parsing error
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            // You may want to set packet.type to an invalid value or throw an exception
        }
        return packet;
    }
};

class GameServer {
public:
    GameServer(uint16_t port);
    ~GameServer();

    bool startServer(uint16_t port);
    void stopServer();
    void updateServer();

    void broadcastPacket(const NetworkPacket& packet);
    void sendPacketToClient(HSteamNetConnection connection, const NetworkPacket& packet);

    moodycamel::ConcurrentQueue<NetworkPacket> incomingPackets;

    int getClientCount() const { return static_cast<int>(clients.size()); }

private:
    HSteamListenSocket listenSocket;
    HSteamNetPollGroup pollGroup;
    ISteamNetworkingSockets* interface;

    std::vector<HSteamNetConnection> clients;
    uint16_t port;
    std::atomic<bool> running;

    void receiveMessages();
    void processMessage(HSteamNetConnection connection, const void* data, uint32_t size);

    void onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);
    static void SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* info);
};

// Client
class GameClient {
public:
    GameClient();
    ~GameClient();

    bool connectToServer(const std::string& serverAddress, uint16_t port);
    void disconnectFromServer();
    void updateClient();

    void sendPacketToServer(const NetworkPacket& packet) const;
    bool isConnected() const { return connected; }

    moodycamel::ConcurrentQueue<NetworkPacket> incomingPackets;

private:
    HSteamNetConnection serverConnection;
    ISteamNetworkingSockets* interface;
    std::atomic<bool> running;
    std::atomic<bool> connected;

    void receiveMessages();
    void processMessage(const void* data, uint32_t size);

    void onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);
    static void SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* info);
};