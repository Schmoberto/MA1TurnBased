#include "Board.h"

Board::Board()
    : gridThickness(10)
    , backgroundPadding(10)
    , gridColor({0, 0, 0, 255})
    , backgroundColor({187, 173, 160, 255})
{
    resetBoard();
}

Board::~Board() = default;


bool Board::setTile(int x, int y, TileState mark) {
    if (!isValidPosition(x, y)) {
        printf("[BOARD] Invalid position (%d, %d)\n", x, y);
        return false;
    }

    if (tiles[y][x] != TileState::EMPTY) {
        printf("[BOARD] Tile at (%d, %d) is already occupied by %c\n", x, y, tiles[y][x] == TileState::X ? 'X' : 'O');
        return false; // Tile already occupied
    }

    tiles[y][x] = mark;
    return true;
}

TileState Board::getTile(int x, int y) const {
    if (isValidPosition(x, y)) {
        return TileState::EMPTY;
    }
    return tiles[y][x];
}

GameResult Board::checkWinner() const {
    // Check rows
    for (int y = 0; y < SIZE; y++) {
        if (tiles[y][0] != TileState::EMPTY &&
            tiles[y][0] == tiles[y][1] &&
            tiles[y][1] == tiles[y][2]) {
            return tiles[y][0] == TileState::X ? GameResult::X_WINS : GameResult::O_WINS;
        }
    }

    // Check columns
    for (int x = 0; x < SIZE; x++) {
        if (tiles[0][x] != TileState::EMPTY &&
            tiles[0][x] == tiles[1][x] &&
            tiles[1][x] == tiles[2][x]) {
            return tiles[0][x] == TileState::X ? GameResult::X_WINS : GameResult::O_WINS;
        }
    }

    // Check diagonals
    if (tiles[0][0] != TileState::EMPTY &&
        tiles[0][0] == tiles[1][1] &&
        tiles[1][1] == tiles[2][2]) {
        return tiles[0][0] == TileState::X ? GameResult::X_WINS : GameResult::O_WINS;
    }

    if (tiles[0][2] != TileState::EMPTY &&
        tiles[0][2] == tiles[1][1] &&
        tiles[1][1] == tiles[2][0]) {
        return tiles[0][2] == TileState::X ? GameResult::X_WINS : GameResult::O_WINS;
    }

    // Check for draw
    if (isFull()) {
        return GameResult::DRAW;
    }

    return GameResult::IN_PROGRESS;
}

bool Board::isFull() const {
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            if (tiles[y][x] == TileState::EMPTY) {
                return false;
            }
        }
    }
    return true;
}

bool Board::isValidPosition(int x, int y) const {
    return x >= 0 && x < SIZE && y >= 0 && y < SIZE;
}

void Board::resetBoard() {
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            tiles[y][x] = TileState::EMPTY;
        }
    }
}

void Board::render(SDL_Renderer *renderer, int tileSize, int offsetX, int offsetY) {
    drawBackground(renderer, tileSize, offsetX, offsetY);
    drawGrid(renderer, tileSize, offsetX, offsetY);

    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            if (tiles[y][x] != TileState::EMPTY) {
                drawMark(renderer, x, y, tiles[y][x], tileSize, offsetX, offsetY);
            }
        }
    }
    /*
    SDL_SetRenderDrawColor(renderer, 187, 173, 160, 255);
    SDL_FRect background = {
        static_cast<float>(offsetX),
        static_cast<float>(offsetY),
        static_cast<float>(SIZE * tileSize),
        static_cast<float>(SIZE * tileSize)
    };
    SDL_RenderFillRect(renderer, &background);

    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            tiles[y][x]->render(renderer, tileSize, offsetX, offsetY);
        }
    }
    */
}

Board::GridPosition Board::screenToGrid(int mouseX, int mouseY, int tileSize, int offsetX, int offsetY) const {
    GridPosition position;

    int gridX = (mouseX - offsetX) / tileSize;
    int gridY = (mouseY - offsetY) / tileSize;

    position.valid = isValidPosition(gridX, gridY);
    position.x = gridX ;//* SIZE;
    position.y = gridY ;//* SIZE;

    return position;
}

void Board::drawBackground(SDL_Renderer *renderer, int tileSize, int offsetX, int offsetY) {
    // Draw background rectangle with padding
    SDL_FRect bgRect;
    bgRect.x = offsetX - backgroundPadding;
    bgRect.y = offsetY - backgroundPadding;
    bgRect.w = SIZE * tileSize + 2 * backgroundPadding;
    bgRect.h = SIZE * tileSize + 2 * backgroundPadding;

    SDL_SetRenderDrawColor(renderer,
        backgroundColor.r,
        backgroundColor.g,
        backgroundColor.b,
        backgroundColor.a);
    SDL_RenderFillRect(renderer, &bgRect);

    // Optional: Draw border around background
    SDL_SetRenderDrawColor(renderer,
        gridColor.r - 30,  // Slightly darker than grid
        gridColor.g - 30,
        gridColor.b - 30,
        255);
    SDL_RenderRect(renderer, &bgRect);
}

void Board::drawGrid(SDL_Renderer *renderer, int tileSize, int offsetX, int offsetY) {
    SDL_SetRenderDrawColor(renderer, gridColor.r, gridColor.g, gridColor.b, gridColor.a);

    int totalSize = SIZE * tileSize;
    int halfThickness = gridThickness / 2;

    // Draw vertical lines (thick rectangles)
    for (int i = 0; i <= SIZE; i++) {
        int x = offsetX + i * tileSize;

        SDL_FRect rect;
        rect.x = x - halfThickness;
        rect.y = offsetY;
        rect.w = gridThickness;
        rect.h = totalSize;

        SDL_RenderFillRect(renderer, &rect);
    }

    // Draw horizontal lines (thick rectangles)
    for (int i = 0; i <= SIZE; i++) {
        int y = offsetY + i * tileSize;

        SDL_FRect rect;
        rect.x = offsetX;
        rect.y = y - halfThickness;
        rect.w = totalSize;
        rect.h = gridThickness;

        SDL_RenderFillRect(renderer, &rect);
    }
}

void Board::drawMark(SDL_Renderer *renderer, int gridX, int gridY, TileState mark, int tileSize, int offsetX,
    int offsetY) {

    int centerX = offsetX + gridX * tileSize + tileSize / 2;
    int centerY = offsetY + gridY * tileSize + tileSize / 2;
    int markSize = tileSize * 0.7;

    if (mark == TileState::X) {
        drawX(renderer, centerX, centerY, markSize);
    } else if (mark == TileState::O) {
        drawO(renderer, centerX, centerY, markSize);
    }
}

void Board::drawX(SDL_Renderer *renderer, int x, int y, int size) {
        SDL_SetRenderDrawColor(renderer, 84, 84, 84, 255);
        int halfSize = size / 2;
        SDL_RenderLine(renderer,
            x - halfSize, y - halfSize,
            x + halfSize, y + halfSize);
        SDL_RenderLine(renderer,
            x - halfSize, y + halfSize,
            x + halfSize, y - halfSize);
}

void Board::drawO(SDL_Renderer *renderer, int x, int y, int size) {
    SDL_SetRenderDrawColor(renderer, 84, 84, 84, 255);
    int radius = size / 2;
    for (int w = 0; w < radius * 2; w++) {
        for (int h = 0; h < radius * 2; h++) {
            int dx = radius - w; // horizontal offset
            int dy = radius - h; // vertical offset
            if ((dx*dx + dy*dy) >= (radius - 5) * (radius - 5) && (dx*dx + dy*dy) <= (radius + 5) * (radius + 5)) {
                SDL_RenderPoint(renderer, x + dx, y + dy);
            }
        }
    }
}
