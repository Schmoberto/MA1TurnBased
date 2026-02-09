#include "Board.h"

Board::Board(int size) : size(size) {
    initializeTiles();
}

Board::~Board() = default;

void Board::render(SDL_Renderer *renderer, int cellSize, int offsetX, int offsetY) {
    SDL_SetRenderDrawColor(renderer, 187, 173, 160, 255);
    SDL_FRect background = {
        static_cast<float>(offsetX),
        static_cast<float>(offsetY),
        static_cast<float>(size * cellSize),
        static_cast<float>(size * cellSize)
    };
    SDL_RenderFillRect(renderer, &background);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            tiles[y][x]->render(renderer, cellSize, offsetX, offsetY);
        }
    }
}

void Board::setTile(int x, int y, bool isEmpty) {
    if (isValidPosition(x, y)) {
        tiles[y][x]->setTileOccupied(!isEmpty);
    }
}

int Board::getTile(int x, int y) const {
    if (isValidPosition(x, y)) {
        return tiles[y][x]->isOccupied() ? 1 : 0;
    }
    return -1; // Invalid position
}

bool Board::isValidPosition(int x, int y) const {
    return x >= 0 && x < size && y >= 0 && y < size;
}

void Board::resetBoard() {
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            tiles[y][x]->setTileOccupied(false);
        }
    }
}

void Board::initializeTiles() {
    tiles.resize(size);
    for (int y = 0; y < size; y++) {
        tiles[y].resize(size);
        for (int x = 0; x < size; x++) {
            tiles[y][x] = std::make_unique<Tile>(x, y);
        }
    }
}

int Board::countEmptyTiles() const {
    int count = 0;
    for (const auto& row : tiles) {
        for (const auto& tile : row) {
            if (!tile->isOccupied()) {
                count++;
            }
        }
    }
    return count;
}
