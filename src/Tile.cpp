#include "Tile.h"

Tile::Tile(int gridX, int gridY) : gridX(gridX), gridY(gridY) {
}

Tile::~Tile() = default;

void Tile::setTileOccupied(bool isOccupied) {
    this->occupied = isOccupied;
}

void Tile::setTilePosition(int gridX, int gridY) {
    this->gridX = gridX;
    this->gridY = gridY;
}

void Tile::render(SDL_Renderer *renderer, int cellSize, int offsetX, int offsetY) {
    SDL_FRect rect;
    rect.x = offsetX + gridX * cellSize + 5;
    rect.y = offsetY + gridY * cellSize + 5;
    rect.w = cellSize - 10;
    rect.h = cellSize - 10;

    SDL_Color color = occupied ? SDL_Color{255, 0, 0, 255} : SDL_Color{200, 200, 200, 255};
    SDL_RenderFillRect(renderer, &rect);

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
    SDL_RenderRect(renderer, &rect);
}