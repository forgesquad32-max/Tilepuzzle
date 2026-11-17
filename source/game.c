#include "game.h"

uint32_t rng_next(uint32_t *state)
{
    uint32_t x = *state ? *state : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

void board_init_solved(Board *b, int n)
{
    int total = n * n;
    b->n = n;
    for (int i = 0; i < MAX_TILES; i++) b->tiles[i] = 0;
    for (int i = 0; i < total - 1; i++) b->tiles[i] = i + 1;
    b->tiles[total - 1] = 0;
    b->blank = total - 1;
    b->moves = 0;
}

bool board_is_solved(const Board *b)
{
    int total = b->n * b->n;
    for (int i = 0; i < total - 1; i++)
        if (b->tiles[i] != i + 1) return false;
    return b->tiles[total - 1] == 0;
}

bool board_slide_at(Board *b, int idx)
{
    int n = b->n;
    if (idx < 0 || idx >= n * n || idx == b->blank) return false;

    int ir = idx / n, ic = idx % n;
    int br = b->blank / n, bc = b->blank % n;

    if (ir == br) {
        int step = (ic < bc) ? 1 : -1;      /* direction the tiles travel */
        int pos = b->blank;
        while (pos != idx) {
            b->tiles[pos] = b->tiles[pos - step];
            pos -= step;
        }
    } else if (ic == bc) {
        int step = (ir < br) ? n : -n;
        int pos = b->blank;
        while (pos != idx) {
            b->tiles[pos] = b->tiles[pos - step];
            pos -= step;
        }
    } else {
        return false;
    }

    b->tiles[idx] = 0;
    b->blank = idx;
    b->moves++;
    return true;
}

bool board_slide_dir(Board *b, Dir tile_dir)
{
    int n = b->n;
    int br = b->blank / n, bc = b->blank % n;
    int idx = -1;

    switch (tile_dir) {
    case DIR_UP:    if (br < n - 1) idx = b->blank + n; break;  /* tile below moves up    */
    case DIR_DOWN:  if (br > 0)     idx = b->blank - n; break;  /* tile above moves down  */
    case DIR_LEFT:  if (bc < n - 1) idx = b->blank + 1; break;  /* tile right moves left  */
    case DIR_RIGHT: if (bc > 0)     idx = b->blank - 1; break;  /* tile left moves right  */
    }
    if (idx < 0) return false;
    return board_slide_at(b, idx);
}

/* one raw step: move a random neighbour of the blank into the blank, never undoing the last step */
static void walk_step(Board *b, uint32_t *rng, int *last_blank)
{
    int n = b->n;
    int br = b->blank / n, bc = b->blank % n;
    int cand[4], count = 0;

    if (br > 0)     cand[count++] = b->blank - n;
    if (br < n - 1) cand[count++] = b->blank + n;
    if (bc > 0)     cand[count++] = b->blank - 1;
    if (bc < n - 1) cand[count++] = b->blank + 1;

    /* drop the cell the blank just came from (avoid instant undo) */
    int kept[4], k = 0;
    for (int i = 0; i < count; i++)
        if (cand[i] != *last_blank) kept[k++] = cand[i];
    if (k == 0) { for (int i = 0; i < count; i++) kept[k++] = cand[i]; }

    int pick = kept[rng_next(rng) % (uint32_t)k];
    b->tiles[b->blank] = b->tiles[pick];
    b->tiles[pick] = 0;
    *last_blank = b->blank;
    b->blank = pick;
}

int board_scramble(Board *b, uint32_t seed, int moves)
{
    uint32_t rng = seed ? seed : 1u;
    int last = -1;
    int steps = 0;

    for (int i = 0; i < moves; i++) { walk_step(b, &rng, &last); steps++; }
    /* never hand out an already solved board */
    int guard = 0;
    while (board_is_solved(b) && guard++ < 16) { walk_step(b, &rng, &last); steps++; }

    b->moves = 0;
    return steps;
}

LevelInfo level_info(int level)
{
    LevelInfo li;
    if (level < 0) level = 0;
    if (level >= NUM_LEVELS) level = NUM_LEVELS - 1;

    int world = level / LEVELS_PER_WORLD;
    int i = level % LEVELS_PER_WORLD;

    li.n = 3 + world;
    switch (world) {
    case 0:  li.scramble = 6 + i * 2;  break;   /* 3x3:  6 .. 34  */
    case 1:  li.scramble = 24 + i * 4; break;   /* 4x4: 24 .. 80  */
    default: li.scramble = 40 + i * 8; break;   /* 5x5: 40 .. 152 */
    }
    li.seed = (uint32_t)(level + 1) * 2654435761u + 12345u;
    return li;
}

int level_stars(int level, int moves)
{
    int par = level_info(level).scramble;
    if (moves <= par) return 3;
    if (moves <= par * 2) return 2;
    return 1;
}
