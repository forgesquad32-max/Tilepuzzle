/* Tile Puzzle - pure game logic (no 3DS headers, so it can be unit-tested on a PC). */
#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_N 5
#define MAX_TILES (MAX_N * MAX_N)

#define NUM_WORLDS 3
#define LEVELS_PER_WORLD 15
#define NUM_LEVELS (NUM_WORLDS * LEVELS_PER_WORLD)

typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } Dir;

typedef struct {
    int n;                  /* board is n x n (3..5)               */
    int tiles[MAX_TILES];   /* 0 = blank, otherwise 1..n*n-1       */
    int blank;              /* index of the blank cell             */
    int moves;              /* moves made by the player            */
} Board;

typedef struct {
    int n;          /* board size                                  */
    int scramble;   /* random-walk length used to shuffle          */
    uint32_t seed;  /* fixed seed so every level is always the same */
} LevelInfo;

uint32_t rng_next(uint32_t *state);

void board_init_solved(Board *b, int n);
bool board_is_solved(const Board *b);

/* Slide the tile at idx (and every tile between it and the blank) toward the blank.
 * Works only if idx is in the same row or column as the blank. Counts as ONE move. */
bool board_slide_at(Board *b, int idx);

/* Slide a tile in the given direction into the blank
 * (DIR_UP = the tile below the blank moves up). */
bool board_slide_dir(Board *b, Dir tile_dir);

/* Shuffle with a random walk of legal moves (always solvable).
 * Returns the number of steps really performed (>= moves unless the board came out solved). */
int board_scramble(Board *b, uint32_t seed, int moves);

LevelInfo level_info(int level);            /* level is 0-based */
int level_stars(int level, int moves);      /* 1..3 */

#endif
