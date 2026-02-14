#pragma once

#include <SDL3/SDL.h>
#include <imgui.h>
#include <string>
#include <functional>

enum class MenuChoice {
    NONE,
    HOST_SERVER,
    JOIN_SERVER,
    QUIT
};

class MainMenu {
public:
    MainMenu();

    void render();
    MenuChoice getChoice() const { return choice; }
    void resetChoice() { choice = MenuChoice::NONE; }

    std::string getServerIP() const { return serverIP; }
    uint16_t getServerPort() const { return serverPort; }

private:
    MenuChoice choice;

    // UI state
    char serverIPBuffer[256];
    char serverPortBuffer[16];
    std::string serverIP;
    uint16_t serverPort;

    bool showError;
    std::string errorMessage;
};
