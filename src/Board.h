#pragma once

#include <array>

#include <SDL3/SDL.h>

enum class TileState {
    EMPTY = 0,
    X = 1,
    O = 2
};

enum class GameResult {
    IN_PROGRESS = 0,
    X_WINS = 1,
    O_WINS = 2,
    DRAW = 3
};

class Board {
public:
    Board();
    ~Board();

    void render(SDL_Renderer* renderer, int tileSize, int offsetX, int offsetY);

    // Game logic
    bool setTile(int x, int y, TileState mark);
    TileState getTile(int x, int y) const;
    GameResult checkWinner() const;
    bool isFull() const;
    void resetBoard();

    // Grid conversion
    struct GridPosition {
        int x, y;
        bool valid;
    };
    GridPosition screenToGrid(int mouseX, int mouseY, int tileSize, int offsetX, int offsetY) const;

    // Getters
    const std::array<std::array<TileState, 3>, 3>& getGrid() const { return tiles; }

    bool isValidPosition(int x, int y) const;

    int getSize() const { return SIZE; }

    // Setters
    void setGridColor(SDL_Color color) { gridColor = color; }
    void setBackgroundColor(SDL_Color color) { backgroundColor = color; }
    void setGridThickness(int thickness) { gridThickness = thickness; }
    void setBackgroundPadding(int padding) { backgroundPadding = padding; }

private:
    static const int SIZE = 3;
    std::array<std::array<TileState, 3>, 3> tiles;

    // Rendering properties
    int gridThickness;
    int backgroundPadding;
    SDL_Color gridColor;
    SDL_Color backgroundColor;

    void drawBackground(SDL_Renderer* renderer, int tileSize, int offsetX, int offsetY);
    void drawGrid(SDL_Renderer* renderer, int tileSize, int offsetX, int offsetY);
    void drawMark(SDL_Renderer* renderer, int gridX, int gridY, TileState mark,
                  int tileSize, int offsetX, int offsetY);
    void drawX(SDL_Renderer* renderer, int x, int y, int size);
    void drawO(SDL_Renderer* renderer, int x, int y, int size);
};
