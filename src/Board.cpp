/*******************************************************************************
 * Board.cpp
 *
 * Implements the Board class which manages the game state of the Tic-Tac-Toe grid,
 * handles rendering of the grid and marks, and provides utility functions for game logic.
 *
 * Architecture:
 * - Board class encapsulates the 3x3 grid and its state
 * - Provides methods to set/get tile states, check for winners, and render the board
 * - Uses SDL for rendering the grid and marks
 ******************************************************************************/

#include "Board.h"
#include <cstdio>

Board::Board()
    : gridThickness(10)
    , backgroundPadding(10)
    , gridColor({0, 0, 0, 255})
    , backgroundColor({187, 173, 160, 255})
{
    resetBoard();
}

Board::~Board() = default;

// -----------------------------------------------------------------------------
//                          Game Logic Methods
// -----------------------------------------------------------------------------

/**
 * Sets the tile at the specified grid coordinates to the given mark (X or O).
 * Validates the position and checks if the tile is already occupied before setting.
 *
 * @param x The x-coordinate of the tile (0-2)
 * @param y The y-coordinate of the tile (0-2)
 * @param mark The TileState to set (X or O)
 * @return true if the tile was successfully set, false if invalid position or occupied
 */
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

/**
 * Retrieves the state of the tile at the specified grid coordinates.
 *
 * @param x The x-coordinate of the tile (0-2)
 * @param y The y-coordinate of the tile (0-2)
 * @return The TileState at the given coordinates, or EMPTY if invalid position
 */
TileState Board::getTile(int x, int y) const {
    if (isValidPosition(x, y)) {
        return TileState::EMPTY;
    }
    return tiles[y][x];
}


/**
 * Checks the current state of the board to determine if there is a winner, a draw, or if the game is still in progress.
 * Evaluates all rows, columns, and diagonals for three identical non-empty marks.
 *
 * @return GameResult indicating the outcome of the game (X_WINS, O_WINS, DRAW, IN_PROGRESS)
 */
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

/**
 * Checks if the board is completely filled with marks (no empty tiles).
 *
 * @return true if the board is full, false if there are any empty tiles
 */
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

/**
 * Validates if the given grid coordinates are within the bounds of the board.
 *
 * @param x The x-coordinate to validate
 * @param y The y-coordinate to validate
 * @return true if the position is valid (0-2 for both x and y), false otherwise
 */
bool Board::isValidPosition(int x, int y) const {
    return x >= 0 && x < SIZE && y >= 0 && y < SIZE;
}

/**
 * Resets the board to its initial empty state by setting all tiles to EMPTY.
 */
void Board::resetBoard() {
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            tiles[y][x] = TileState::EMPTY;
        }
    }
}

/**
 * Renders the board on the given SDL_Renderer by drawing the background, grid lines, and any X/O marks.
 *
 * @param renderer The SDL_Renderer to draw on
 * @param tileSize The size of each tile in pixels
 * @param offsetX The x-coordinate offset for rendering the grid
 * @param offsetY The y-coordinate offset for rendering the grid
 */
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
}

/**
 * Converts screen coordinates (mouseX, mouseY) to grid coordinates based on the tile size and grid offset.
 * Validates if the resulting grid position is within the bounds of the board.
 *
 * @param mouseX The x-coordinate of the mouse click in screen pixels
 * @param mouseY The y-coordinate of the mouse click in screen pixels
 * @param tileSize The size of each tile in pixels
 * @param offsetX The x-coordinate offset for the grid rendering
 * @param offsetY The y-coordinate offset for the grid rendering
 * @return GridPosition struct containing the grid coordinates and validity flag
 */
Board::GridPosition Board::screenToGrid(int mouseX, int mouseY, int tileSize, int offsetX, int offsetY) const {
    GridPosition position;

    int gridX = (mouseX - offsetX) / tileSize;
    int gridY = (mouseY - offsetY) / tileSize;

    position.valid = isValidPosition(gridX, gridY);
    position.x = gridX ;//* SIZE;
    position.y = gridY ;//* SIZE;

    return position;
}

/**
 * Draws the background rectangle for the board with padding and a border.
 *
 * @param renderer The SDL_Renderer to draw on
 * @param tileSize The size of each tile in pixels
 * @param offsetX The x-coordinate offset for rendering the grid
 * @param offsetY The y-coordinate offset for rendering the grid
 */
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

/**
 * Draws the grid lines for the board using thick rectangles to create visible lines.
 *
 * @param renderer The SDL_Renderer to draw on
 * @param tileSize The size of each tile in pixels
 * @param offsetX The x-coordinate offset for rendering the grid
 * @param offsetY The y-coordinate offset for rendering the grid
 */
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

/**
 * Draws an X or O mark at the specified grid coordinates based on the TileState.
 * Calculates the center position for the mark and calls the appropriate drawing function.
 *
 * @param renderer The SDL_Renderer to draw on
 * @param gridX The x-coordinate of the grid tile (0-2)
 * @param gridY The y-coordinate of the grid tile (0-2)
 * @param mark The TileState indicating whether to draw X or O
 * @param tileSize The size of each tile in pixels
 * @param offsetX The x-coordinate offset for rendering the grid
 * @param offsetY The y-coordinate offset for rendering the grid
 */
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

/**
 * Draws an X mark at the specified center position with the given size.
 * Uses multiple lines to create a thicker X for better visibility.
 *
 * @param renderer The SDL_Renderer to draw on
 * @param x The x-coordinate of the center of the X
 * @param y The y-coordinate of the center of the X
 * @param size The overall size of the X (length of each arm)
 */
void Board::drawX(SDL_Renderer *renderer, int x, int y, int size) {
        SDL_SetRenderDrawColor(renderer, 84, 84, 84, 255);
        int halfSize = size / 2;
        int thickness = 12;

        // Draw X as thick lines
        for (int i = -thickness/2; i <= thickness/2; i++) {
            // Top-left to bottom-right diagonal
            SDL_RenderLine(renderer,
                x - halfSize + i, y - halfSize,
                x + halfSize + i, y + halfSize);

            // Top-right to bottom-left diagonal
            SDL_RenderLine(renderer,
                x + halfSize + i, y - halfSize,
                x - halfSize + i, y + halfSize);
        }
}

/**
 * Draws an O mark at the specified center position with the given size.
 * Uses a modified Bresenham's circle algorithm to draw multiple concentric circles for thickness.
 *
 * @param renderer The SDL_Renderer to draw on
 * @param x The x-coordinate of the center of the O
 * @param y The y-coordinate of the center of the O
 * @param size The overall diameter of the O
 */
void Board::drawO(SDL_Renderer *renderer, int x, int y, int size) {
    SDL_SetRenderDrawColor(renderer, 84, 84, 84, 255);
    int radius = size / 2;
    int thickness = 12;

    // Draw multiple circles for thickness
    for (int t = -thickness/2; t <= thickness/2; t++) {
        int r = radius + t;

        // Only draw if radius is positive
        if (r <= 0) continue;

        int cx = 0;
        int cy = r;
        int d = 3 - 2 * r;

        while (cy >= cx) {
            // Draw 8 octants
            SDL_RenderPoint(renderer, x + cx, y + cy);
            SDL_RenderPoint(renderer, x - cx, y + cy);
            SDL_RenderPoint(renderer, x + cx, y - cy);
            SDL_RenderPoint(renderer, x - cx, y - cy);
            SDL_RenderPoint(renderer, x + cy, y + cx);
            SDL_RenderPoint(renderer, x - cy, y + cx);
            SDL_RenderPoint(renderer, x + cy, y - cx);
            SDL_RenderPoint(renderer, x - cy, y - cx);

            cx++;
            if (d > 0) {
                cy--;
                d = d + 4 * (cx - cy) + 10;
            } else {
                d = d + 4 * cx + 6;
            }
        }
    }
}
