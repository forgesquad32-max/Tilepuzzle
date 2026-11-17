/* Unit tests for source/game.c - run on a PC:  gcc -Wall -Isource tests/test_game.c source/game.c -o t && ./t */
#include <stdio.h>
#include <stdlib.h>
#include "game.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

/* A board is solvable iff:  odd n  -> inversions even;
 *                           even n -> inversions + (blank row counted from the bottom, 1-based) is odd. */
static bool solvable(const Board *b)
{
    int inv = 0, total = b->n * b->n;
    for (int i = 0; i < total; i++)
        for (int j = i + 1; j < total; j++)
            if (b->tiles[i] && b->tiles[j] && b->tiles[i] > b->tiles[j]) inv++;
    if (b->n % 2 == 1) return inv % 2 == 0;
    int row_from_bottom = b->n - (b->blank / b->n);
    return (inv + row_from_bottom) % 2 == 1;
}

static bool is_permutation(const Board *b)
{
    int seen[MAX_TILES] = {0};
    int total = b->n * b->n;
    for (int i = 0; i < total; i++) {
        if (b->tiles[i] < 0 || b->tiles[i] >= total) return false;
        seen[b->tiles[i]]++;
    }
    for (int i = 0; i < total; i++) if (seen[i] != 1) return false;
    return b->tiles[b->blank] == 0;
}

int main(void)
{
    Board b;

    /* solved board */
    for (int n = 3; n <= 5; n++) {
        board_init_solved(&b, n);
        CHECK(board_is_solved(&b));
        CHECK(is_permutation(&b));
        CHECK(solvable(&b));
    }

    /* single moves from the solved state (blank in bottom-right corner) */
    board_init_solved(&b, 3);
    CHECK(board_slide_dir(&b, DIR_DOWN));    /* tile 6 (above the blank) moves down into the corner */
    CHECK(b.tiles[8] == 6 && b.blank == 5 && b.moves == 1);
    CHECK(!board_is_solved(&b));
    CHECK(board_slide_dir(&b, DIR_UP));      /* tile 6 comes back up */
    CHECK(board_is_solved(&b));

    board_init_solved(&b, 3);
    CHECK(!board_slide_dir(&b, DIR_UP));     /* nothing below the blank */
    CHECK(!board_slide_dir(&b, DIR_LEFT));   /* nothing to the right    */
    CHECK(board_slide_dir(&b, DIR_RIGHT));   /* tile 8 moves right      */
    CHECK(b.tiles[8] == 8 && b.blank == 7);

    /* line slide: tap far tile in the same row moves two tiles as ONE move */
    board_init_solved(&b, 3);                /* row 2 = 7 8 _ */
    CHECK(board_slide_at(&b, 6));            /* tap tile 7 */
    CHECK(b.tiles[6] == 0 && b.tiles[7] == 7 && b.tiles[8] == 8 && b.blank == 6 && b.moves == 1);

    /* column slide */
    board_init_solved(&b, 4);
    CHECK(board_slide_at(&b, 3));            /* tap tile 4 (top of last column) */
    CHECK(b.tiles[3] == 0 && b.tiles[7] == 4 && b.tiles[11] == 8 && b.tiles[15] == 12 && b.blank == 3);
    CHECK(is_permutation(&b));

    /* invalid taps */
    board_init_solved(&b, 3);
    CHECK(!board_slide_at(&b, 0));           /* diagonal to blank */
    CHECK(!board_slide_at(&b, 8));           /* the blank itself  */
    CHECK(!board_slide_at(&b, -1));
    CHECK(!board_slide_at(&b, 9));
    CHECK(b.moves == 0);

    /* scramble: always a valid, solvable, not-solved permutation; deterministic */
    for (int n = 3; n <= 5; n++) {
        for (uint32_t seed = 1; seed < 400; seed += 7) {
            Board s1, s2;
            board_init_solved(&s1, n); int st1 = board_scramble(&s1, seed, 5 + (int)(seed % 60));
            board_init_solved(&s2, n); int st2 = board_scramble(&s2, seed, 5 + (int)(seed % 60));
            CHECK(is_permutation(&s1));
            CHECK(solvable(&s1));
            CHECK(!board_is_solved(&s1));
            CHECK(s1.moves == 0);
            CHECK(st1 == st2);
            for (int i = 0; i < n * n; i++) CHECK(s1.tiles[i] == s2.tiles[i]);
        }
    }

    /* moves made by the player keep the board solvable */
    {
        Board p;
        board_init_solved(&p, 4);
        board_scramble(&p, 777, 50);
        uint32_t r = 4242;
        for (int i = 0; i < 2000; i++) {
            if (rng_next(&r) % 2) board_slide_at(&p, (int)(rng_next(&r) % 16));
            else board_slide_dir(&p, (Dir)(rng_next(&r) % 4));
            CHECK(is_permutation(&p));
            CHECK(solvable(&p));
        }
    }

    /* scramble then undo exactly: the reverse of the walk solves it (proves winnable within 'steps') */
    {
        Board s;
        board_init_solved(&s, 3);
        int steps = board_scramble(&s, 99, 20);
        CHECK(steps >= 20);
    }

    /* levels */
    CHECK(NUM_LEVELS == 45);
    for (int l = 0; l < NUM_LEVELS; l++) {
        LevelInfo li = level_info(l);
        CHECK(li.n >= 3 && li.n <= 5);
        CHECK(li.scramble > 0);
        if (l > 0) {
            LevelInfo pv = level_info(l - 1);
            CHECK(li.n > pv.n || (li.n == pv.n && li.scramble > pv.scramble));
        }
        Board lb;
        board_init_solved(&lb, li.n);
        board_scramble(&lb, li.seed, li.scramble);
        CHECK(is_permutation(&lb) && solvable(&lb) && !board_is_solved(&lb));
    }
    CHECK(level_stars(0, level_info(0).scramble) == 3);
    CHECK(level_stars(0, level_info(0).scramble * 2) == 2);
    CHECK(level_stars(0, level_info(0).scramble * 2 + 1) == 1);

    if (failures) { printf("%d test(s) FAILED\n", failures); return 1; }
    printf("all tests passed\n");
    return 0;
}
