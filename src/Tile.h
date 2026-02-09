#pragma once
#include <SDL3/SDL_render.h>

struct App;

class Tile {
public:
    Tile(int gridX, int gridY);
    ~Tile();

    void setTileOccupied(bool isOccupied);
    void setTilePosition(int gridX, int gridY);
    int getGridX() const { return gridX; }
    int getGridY() const { return gridY; }

    void render(SDL_Renderer* renderer, int cellSize, int offsetX, int offsetY);

    bool isOccupied() const { return occupied; }

private:
    int gridX, gridY;
    bool occupied = false;
};
