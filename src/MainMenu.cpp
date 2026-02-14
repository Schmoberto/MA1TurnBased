#include "MainMenu.h"

MainMenu::MainMenu()
        : choice(MenuChoice::NONE)
        , serverIPBuffer("127.0.0.1")
        , serverPort(27015)
        , showError(false) {

    strcpy_s(serverIPBuffer, "127.0.0.1");
    strcpy_s(serverPortBuffer, "27015");
}

void MainMenu::render() {
    // Center the menu window
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2 - 150, io.DisplaySize.y / 2 - 100), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_Always);

    ImGui::Begin("Tic Tac Toe Main Menu", nullptr,
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove);

    ImGui::TextWrapped("Welcome to Multithreaded Networked Tic Tac Toe!");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Host server
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.4f, 1.0f));
    if (ImGui::Button("Host Server", ImVec2(-1, 50))) {
        choice = MenuChoice::HOST_SERVER;

        // Parse port
        try {
            serverPort = static_cast<uint16_t>(std::stoi(serverPortBuffer));
        } catch (...) {
            serverPort = 27015; // Default port
        }
        printf("[MainMenu] Host server selected on port %d.\n", serverPort);
    }

    ImGui::PopStyleColor(2);
    ImGui::Spacing();

    // Join server section
    ImGui::Text("Join server:");
    ImGui::InputText("IP Address", serverIPBuffer, sizeof(serverIPBuffer));
    ImGui::InputText("Port", serverPortBuffer, sizeof(serverPortBuffer));

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.9f, 1.0f));
    if (ImGui::Button("Connect", ImVec2(-1, 50))) {
        serverIP = std::string(serverIPBuffer);

        try {
            serverPort = static_cast<uint16_t>(std::stoi(serverPortBuffer));
        } catch (...) {
            serverPort = 27015;
        }

        if (serverIP.empty()) {
            showError = true;
            errorMessage = "Please enter a server IP address!";
        } else {
            choice = MenuChoice::JOIN_SERVER;
            printf("[MainMenu] Join server selected: %s:%d\n", serverIP.c_str(), serverPort);
        }
    }
    ImGui::PopStyleColor(2);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Quit button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
    if (ImGui::Button("Quit", ImVec2(-1, 30))) {
        choice = MenuChoice::QUIT;
    }
    ImGui::PopStyleColor(2);

    // Show error if any
    if (showError) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", errorMessage.c_str());
        ImGui::PopStyleColor();

        if (ImGui::Button("OK")) {
            showError = false;
        }
    }

    ImGui::End();
}
