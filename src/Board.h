#pragma once

#include "Tile.h"
#include <SDL3/SDL.h>
#include <vector>
#include <memory>

class Board {
public:
    Board(int size = 20);
    ~Board();

    void render(SDL_Renderer* renderer, int cellSize, int offsetX, int offsetY);

    void setTile(int x, int y, bool isEmpty);
    int getTile(int x, int y) const;
    bool isValidPosition(int x, int y) const;

    int getSize() const { return size; }

    void resetBoard();

private:
    int size;
    std::vector<std::vector<std::unique_ptr<Tile>>> tiles;

    void initializeTiles();
    int countEmptyTiles() const;
};
