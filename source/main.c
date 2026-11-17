/* Tile Puzzle Deluxe for Nintendo 3DS  (libctru + citro2d)
 * Modes: Classic (45 levels), Time Attack, Move Limit, Zen.  Settings + save data on the SD card. */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "game.h"

#define TOP_W 400.0f
#define BOT_W 320.0f
#define SCR_H 240.0f

#define RGB(r, g, b) ((u32)(((r) & 0xFF) | (((g) & 0xFF) << 8) | (((b) & 0xFF) << 16) | (0xFFu << 24)))
#define RGBA(r, g, b, a) ((u32)(((r) & 0xFF) | (((g) & 0xFF) << 8) | (((b) & 0xFF) << 16) | (((a) & 0xFF) << 24)))

#define SAVE_DIR  "sdmc:/3ds/tilepuzzle"
#define SAVE_PATH SAVE_DIR "/save.dat"

#define TA_START_MS   120000   /* Time Attack: starting time        */
#define TA_BONUS_MS    20000   /* Time Attack: bonus per solved board */

/* ------------------------------------------------------------------ themes */
typedef struct {
    const char *name;
    u32 bg, bg2, panel, tile, tile_ok, text, accent, dim;
} Theme;

#define NUM_THEMES 5
static const Theme THEMES[NUM_THEMES] = {
    { "Classic", RGB(22, 30, 62),  RGB(40, 56, 108), RGB(14, 20, 44), RGB(70, 110, 200), RGB(52, 170, 112), RGB(255, 255, 255), RGB(255, 196, 64),  RGB(150, 162, 196) },
    { "Ocean",   RGB(8, 44, 62),   RGB(16, 90, 116), RGB(5, 28, 40),  RGB(30, 150, 172), RGB(112, 208, 120), RGB(255, 255, 255), RGB(255, 214, 90),  RGB(140, 186, 198) },
    { "Sunset",  RGB(58, 20, 52),  RGB(116, 42, 62), RGB(38, 12, 34), RGB(220, 100, 72), RGB(238, 176, 60), RGB(255, 255, 255), RGB(255, 232, 140), RGB(206, 160, 170) },
    { "Forest",  RGB(18, 46, 28),  RGB(36, 88, 52),  RGB(12, 30, 18), RGB(70, 150, 82),  RGB(206, 202, 84), RGB(255, 255, 255), RGB(255, 210, 90),  RGB(150, 190, 158) },
    { "Mono",    RGB(24, 24, 28),  RGB(48, 48, 56),  RGB(12, 12, 14), RGB(112, 112, 128), RGB(70, 172, 232), RGB(255, 255, 255), RGB(255, 200, 60),  RGB(160, 160, 170) },
};

/* --------------------------------------------------------------- save data */
typedef struct {
    char     magic[4];
    uint8_t  theme, anim, timer, dpad_blank;
    uint8_t  stars[NUM_LEVELS];
    uint16_t best_moves[NUM_LEVELS];
    uint16_t best_secs[NUM_LEVELS];
    uint16_t best_time_attack[3];
    uint16_t best_streak[3];
} SaveData;

static SaveData g_save;
#define TH (THEMES[g_save.theme])

static void save_defaults(void)
{
    memset(&g_save, 0, sizeof g_save);
    memcpy(g_save.magic, "TPZ1", 4);
    g_save.anim = 1;
    g_save.timer = 1;
}

static void save_load(void)
{
    save_defaults();
    FILE *f = fopen(SAVE_PATH, "rb");
    if (!f) return;
    SaveData tmp;
    if (fread(&tmp, sizeof tmp, 1, f) == 1 && memcmp(tmp.magic, "TPZ1", 4) == 0 && tmp.theme < NUM_THEMES)
        g_save = tmp;
    fclose(f);
}

static void save_write(void)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(SAVE_DIR, 0777);
    FILE *f = fopen(SAVE_PATH, "wb");
    if (!f) return;
    fwrite(&g_save, sizeof g_save, 1, f);
    fclose(f);
}

/* ------------------------------------------------------------------- state */
typedef enum { S_MENU, S_SIZE, S_LEVELS, S_PLAY, S_SETTINGS, S_HOWTO } Screen;
typedef enum { M_CLASSIC, M_TIME, M_LIMIT, M_ZEN } Mode;
typedef enum { P_PLAY, P_PAUSE, P_WON, P_LOST } Phase;
typedef enum { A_RESUME, A_RESTART, A_NEW, A_NEXT, A_LEVELS, A_MENU } Act;

static C3D_RenderTarget *g_top, *g_bot;
static C2D_TextBuf g_buf;
static float g_z;
static bool g_running = true;
static u64 g_last_tick;

static Screen g_screen = S_MENU;
static Mode g_mode = M_CLASSIC;
static Phase g_phase = P_PLAY;

static int g_menu_sel;
static int g_size_sel;              /* 0..2  => 3x3..5x5            */
static int g_world, g_lsel;         /* level select                 */
static int g_set_sel;               /* settings row                 */
static bool g_confirm_reset;

static Board g_board;
static int g_level;                 /* classic: current level       */
static int g_size_n = 3;            /* other modes: board size      */
static uint32_t g_cur_seed;
static int g_cur_scramble;
static uint32_t g_seed_counter;
static int g_limit;                 /* Move Limit: max moves        */
static int g_score;                 /* TA: solved, Limit: streak, Zen: solved */
static int64_t g_time_ms;           /* count-up, or remaining (TA)  */
static bool g_started;
static int g_stars_earned;
static bool g_new_best;
static int g_toast_ms;
static char g_toast[24];
static float g_dx[MAX_TILES + 1], g_dy[MAX_TILES + 1];

static Act g_ov_act[4];
static const char *g_ov_lbl[4];
static int g_ov_count, g_ov_sel;

static const char *MODE_NAMES[] = { "CLASSIC", "TIME ATTACK", "MOVE LIMIT", "ZEN" };

/* ----------------------------------------------------------- draw helpers */
typedef struct { float x, y, w, h; } Rect;
static Rect R(float x, float y, float w, float h) { Rect r = { x, y, w, h }; return r; }
static bool hit(Rect r, int px, int py) { return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h; }

static float nz(void) { g_z += 0.0001f; return g_z; }

static void rect(float x, float y, float w, float h, u32 c) { C2D_DrawRectSolid(x, y, nz(), w, h, c); }
static void rect_grad(float x, float y, float w, float h, u32 top, u32 bottom)
{
    C2D_DrawRectangle(x, y, nz(), w, h, top, top, bottom, bottom);
}
static void tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c)
{
    C2D_DrawTriangle(x0, y0, c, x1, y1, c, x2, y2, c, nz());
}

enum { AL_L, AL_C, AL_R };

static void txt(const char *s, float x, float y, float sc, u32 col, int al)
{
    C2D_Text t;
    float w, h;
    C2D_TextParse(&t, g_buf, s);
    C2D_TextOptimize(&t);
    C2D_TextGetDimensions(&t, sc, sc, &w, &h);
    if (al == AL_C) x -= w * 0.5f;
    else if (al == AL_R) x -= w;
    C2D_DrawText(&t, C2D_WithColor, x, y, nz(), sc, sc, col);
}

static void txt_mid(const char *s, float cx, float cy, float sc, u32 col)
{
    C2D_Text t;
    float w, h;
    C2D_TextParse(&t, g_buf, s);
    C2D_TextOptimize(&t);
    C2D_TextGetDimensions(&t, sc, sc, &w, &h);
    C2D_DrawText(&t, C2D_WithColor, cx - w * 0.5f, cy - h * 0.5f, nz(), sc, sc, col);
}

static void star(float cx, float cy, float r, u32 c)
{
    float h = r * 0.866f;
    tri(cx, cy - r, cx - h, cy + r * 0.5f, cx + h, cy + r * 0.5f, c);
    tri(cx, cy + r, cx - h, cy - r * 0.5f, cx + h, cy - r * 0.5f, c);
}

static void stars_row(float cx, float cy, float r, int filled)
{
    for (int i = 0; i < 3; i++)
        star(cx + (i - 1) * (r * 2.3f), cy, r, i < filled ? TH.accent : RGBA(255, 255, 255, 50));
}

static void draw_bg(bool top)
{
    rect_grad(0, 0, top ? TOP_W : BOT_W, SCR_H, TH.bg, TH.bg2);
}

static void draw_button(Rect r, const char *label, bool sel, bool enabled)
{
    float sc = r.h >= 44 ? 0.9f : (r.h >= 28 ? 0.7f : 0.55f);
    rect(r.x + 2, r.y + 3, r.w, r.h, RGBA(0, 0, 0, 90));
    rect(r.x, r.y, r.w, r.h, !enabled ? TH.panel : (sel ? TH.accent : TH.tile));
    if (enabled) rect(r.x, r.y, r.w, 2, RGBA(255, 255, 255, 60));
    txt_mid(label, r.x + r.w * 0.5f, r.y + r.h * 0.5f, sc,
            !enabled ? TH.dim : (sel ? TH.panel : TH.text));
}

static void fmt_time(char *out, size_t n, int64_t ms)
{
    if (ms < 0) ms = 0;
    int s = (int)(ms / 1000);
    snprintf(out, n, "%02d:%02d", s / 60, s % 60);
}

/* --------------------------------------------------------------- puzzle */
static int unlocked(int level) { return level == 0 || g_save.stars[level - 1] > 0; }

static int scramble_for_mode(int n)
{
    static const int ta[3]   = { 20, 30, 45 };
    static const int lim[3]  = { 16, 28, 40 };
    static const int zen[3]  = { 30, 60, 100 };
    int i = n - 3;
    if (g_mode == M_TIME) return ta[i];
    if (g_mode == M_LIMIT) return lim[i];
    return zen[i];
}

static void snap_tiles(void)
{
    int n = g_board.n, ts = 216 / n;
    for (int i = 0; i < n * n; i++) {
        int v = g_board.tiles[i];
        if (v) { g_dx[v] = 6.0f + (i % n) * ts; g_dy[v] = 12.0f + (i / n) * ts; }
    }
}

static void build_overlay(void);

static void make_puzzle(bool same)
{
    int n;
    if (g_mode == M_CLASSIC) {
        LevelInfo li = level_info(g_level);
        n = li.n; g_cur_seed = li.seed; g_cur_scramble = li.scramble;
    } else if (!same) {
        n = g_size_n;
        g_cur_seed = (uint32_t)osGetTime() * 2654435761u + (++g_seed_counter) * 40503u;
        g_cur_scramble = scramble_for_mode(n);
    } else {
        n = g_size_n;
    }
    board_init_solved(&g_board, n);
    int steps = board_scramble(&g_board, g_cur_seed, g_cur_scramble);
    g_limit = steps * 2;
    snap_tiles();
    g_started = false;
    g_phase = P_PLAY;
    g_ov_count = 0;
    if (g_mode == M_CLASSIC) g_time_ms = 0;
    else if (g_mode != M_TIME) g_time_ms = 0;
}

static void start_game(void)
{
    g_screen = S_PLAY;
    g_score = 0;
    g_toast_ms = 0;
    if (g_mode == M_CLASSIC) g_level = g_lsel;
    else g_size_n = 3 + g_size_sel;
    if (g_mode == M_TIME) g_time_ms = TA_START_MS;
    make_puzzle(false);
    g_last_tick = osGetTime();
}

/* ------------------------------------------------------------- overlays */
static void ov_add(Act a, const char *label)
{
    if (g_ov_count < 4) { g_ov_act[g_ov_count] = a; g_ov_lbl[g_ov_count] = label; g_ov_count++; }
}

static void build_overlay(void)
{
    g_ov_count = 0;
    g_ov_sel = 0;
    switch (g_phase) {
    case P_PAUSE:
        ov_add(A_RESUME, "Resume");
        if (g_mode == M_CLASSIC) {
            ov_add(A_RESTART, "Restart Level");
            ov_add(A_LEVELS, "Level Select");
        } else if (g_mode == M_TIME) {
            ov_add(A_RESTART, "Restart Run");
            ov_add(A_MENU, "Quit to Menu");
        } else {
            ov_add(A_RESTART, "Restart Puzzle");
            ov_add(A_NEW, "New Puzzle");
            ov_add(A_MENU, "Quit to Menu");
        }
        break;
    case P_WON:
        if (g_mode == M_CLASSIC) {
            if (g_level + 1 < NUM_LEVELS) ov_add(A_NEXT, "Next Level");
            ov_add(A_RESTART, "Retry");
            ov_add(A_LEVELS, "Level Select");
        } else {
            ov_add(A_NEW, "Next Puzzle");
            ov_add(A_MENU, "Quit to Menu");
        }
        break;
    case P_LOST:
        if (g_mode == M_TIME) {
            ov_add(A_RESTART, "Play Again");
        } else {
            ov_add(A_RESTART, "Try Again");
            ov_add(A_NEW, "New Puzzle");
        }
        ov_add(A_MENU, "Quit to Menu");
        break;
    default:
        break;
    }
}

static Rect ov_rect(int i)
{
    float total = g_ov_count * 40.0f + (g_ov_count - 1) * 8.0f;
    return R(50, (SCR_H - total) * 0.5f + i * 48.0f, 220, 40);
}

static void do_action(Act a)
{
    switch (a) {
    case A_RESUME:
        g_phase = P_PLAY;
        g_ov_count = 0;
        g_last_tick = osGetTime();
        break;
    case A_RESTART:
        if (g_mode == M_TIME) { g_score = 0; g_time_ms = TA_START_MS; make_puzzle(false); }
        else { if (g_mode == M_LIMIT) g_score = 0; make_puzzle(true); }
        g_last_tick = osGetTime();
        break;
    case A_NEW:
        if (g_mode == M_LIMIT && g_phase == P_PAUSE) g_score = 0;
        make_puzzle(false);
        g_last_tick = osGetTime();
        break;
    case A_NEXT:
        if (g_level + 1 < NUM_LEVELS) g_level++;
        g_lsel = g_level;
        make_puzzle(false);
        g_last_tick = osGetTime();
        break;
    case A_LEVELS:
        save_write();
        g_world = g_level / LEVELS_PER_WORLD;
        g_lsel = g_level;
        g_screen = S_LEVELS;
        break;
    case A_MENU:
        save_write();
        g_screen = S_MENU;
        break;
    }
}

static void on_solved(void)
{
    switch (g_mode) {
    case M_CLASSIC: {
        int secs = (int)(g_time_ms / 1000);
        g_stars_earned = level_stars(g_level, g_board.moves);
        g_new_best = (g_save.best_moves[g_level] == 0 || g_board.moves < g_save.best_moves[g_level]);
        if (g_stars_earned > g_save.stars[g_level]) g_save.stars[g_level] = (uint8_t)g_stars_earned;
        if (g_new_best) g_save.best_moves[g_level] = (uint16_t)g_board.moves;
        if (g_save.best_secs[g_level] == 0 || secs < g_save.best_secs[g_level]) g_save.best_secs[g_level] = (uint16_t)(secs ? secs : 1);
        save_write();
        g_phase = P_WON;
        break;
    }
    case M_TIME:
        g_score++;
        g_time_ms += TA_BONUS_MS;
        if (g_score > g_save.best_time_attack[g_size_n - 3]) g_save.best_time_attack[g_size_n - 3] = (uint16_t)g_score;
        snprintf(g_toast, sizeof g_toast, "+%ds", TA_BONUS_MS / 1000);
        g_toast_ms = 1200;
        make_puzzle(false);
        g_started = true;
        return;
    case M_LIMIT:
        g_score++;
        g_new_best = g_score > g_save.best_streak[g_size_n - 3];
        if (g_new_best) g_save.best_streak[g_size_n - 3] = (uint16_t)g_score;
        save_write();
        g_phase = P_WON;
        break;
    case M_ZEN:
        g_score++;
        g_phase = P_WON;
        break;
    }
    build_overlay();
}

static void on_lost(void)
{
    if (g_mode == M_TIME) {
        g_time_ms = 0;
        if (g_score > g_save.best_time_attack[g_size_n - 3]) g_save.best_time_attack[g_size_n - 3] = (uint16_t)g_score;
    } else if (g_mode == M_LIMIT) {
        g_score = 0;
    }
    save_write();
    g_phase = P_LOST;
    build_overlay();
}

static void after_move(void)
{
    g_started = true;
    if (board_is_solved(&g_board)) on_solved();
    else if (g_mode == M_LIMIT && g_board.moves >= g_limit) on_lost();
}

static Dir opposite(Dir d)
{
    switch (d) {
    case DIR_UP: return DIR_DOWN;
    case DIR_DOWN: return DIR_UP;
    case DIR_LEFT: return DIR_RIGHT;
    default: return DIR_LEFT;
    }
}

/* ------------------------------------------------------------ board draw */
static void board_geometry(float *ox, float *oy, int *ts)
{
    *ts = 216 / g_board.n;
    *ox = 6.0f;
    *oy = 12.0f;
}

static int hit_tile(int px, int py)
{
    float ox, oy; int ts;
    board_geometry(&ox, &oy, &ts);
    int n = g_board.n;
    if (px < ox || py < oy || px >= ox + ts * n || py >= oy + ts * n) return -1;
    return ((py - (int)oy) / ts) * n + ((px - (int)ox) / ts);
}

static void draw_board(void)
{
    float ox, oy; int ts;
    board_geometry(&ox, &oy, &ts);
    int n = g_board.n;
    int pos[MAX_TILES + 1];
    for (int i = 0; i < n * n; i++) pos[g_board.tiles[i]] = i;

    rect(ox - 5, oy - 5, ts * n + 10, ts * n + 10, TH.panel);
    rect(ox, oy, ts * n, ts * n, RGBA(0, 0, 0, 70));

    float sc = (n == 3) ? 1.1f : (n == 4 ? 0.9f : 0.72f);
    for (int v = 1; v < n * n; v++) {
        int idx = pos[v];
        float tx = ox + (idx % n) * ts, ty = oy + (idx / n) * ts;
        if (g_save.anim) {
            g_dx[v] += (tx - g_dx[v]) * 0.45f;
            g_dy[v] += (ty - g_dy[v]) * 0.45f;
            if (fabsf(tx - g_dx[v]) < 0.6f) g_dx[v] = tx;
            if (fabsf(ty - g_dy[v]) < 0.6f) g_dy[v] = ty;
        } else {
            g_dx[v] = tx; g_dy[v] = ty;
        }
        bool ok = (idx == v - 1);
        float x = g_dx[v] + 2, y = g_dy[v] + 2, s = ts - 4;
        rect(x + 1, y + 2, s, s, RGBA(0, 0, 0, 100));
        rect(x, y, s, s, ok ? TH.tile_ok : TH.tile);
        rect(x, y, s, 3, RGBA(255, 255, 255, 70));
        char b[8];
        snprintf(b, sizeof b, "%d", v);
        txt_mid(b, x + s * 0.5f, y + s * 0.5f, sc, TH.text);
    }
}

static void draw_mini_goal(float cx, float cy, float size)
{
    int n = g_board.n;
    float ts = size / n;
    float x0 = cx - size * 0.5f, y0 = cy - size * 0.5f;
    rect(x0 - 4, y0 - 4, size + 8, size + 8, TH.panel);
    for (int i = 0; i < n * n - 1; i++) {
        float x = x0 + (i % n) * ts, y = y0 + (i / n) * ts;
        rect(x + 1, y + 1, ts - 2, ts - 2, TH.tile);
        char b[8];
        snprintf(b, sizeof b, "%d", i + 1);
        txt_mid(b, x + ts * 0.5f, y + ts * 0.5f, n == 3 ? 0.55f : (n == 4 ? 0.45f : 0.36f), TH.text);
    }
}

/* ------------------------------------------------------- screen: top draw */
static void draw_play_top(void)
{
    char b[64], t[16];
    rect(0, 0, TOP_W, 34, TH.panel);

    if (g_mode == M_CLASSIC) snprintf(b, sizeof b, "CLASSIC  -  LEVEL %d", g_level + 1);
    else snprintf(b, sizeof b, "%s", MODE_NAMES[g_mode]);
    txt(b, 14, 4, 0.8f, TH.text, AL_L);
    snprintf(b, sizeof b, "%dx%d", g_board.n, g_board.n);
    txt(b, TOP_W - 14, 4, 0.8f, TH.accent, AL_R);

    /* moves */
    txt("MOVES", 28, 50, 0.5f, TH.dim, AL_L);
    if (g_mode == M_LIMIT) snprintf(b, sizeof b, "%d / %d", g_board.moves, g_limit);
    else snprintf(b, sizeof b, "%d", g_board.moves);
    bool danger = (g_mode == M_LIMIT && g_limit - g_board.moves <= 5);
    txt(b, 28, 64, 1.3f, danger ? RGB(255, 110, 100) : TH.text, AL_L);

    /* time */
    if (g_mode == M_TIME) {
        txt("TIME LEFT", 28, 124, 0.5f, TH.dim, AL_L);
        fmt_time(t, sizeof t, g_time_ms);
        txt(t, 28, 138, 1.3f, g_time_ms < 10000 ? RGB(255, 110, 100) : TH.text, AL_L);
    } else if (g_save.timer) {
        txt("TIME", 28, 124, 0.5f, TH.dim, AL_L);
        fmt_time(t, sizeof t, g_time_ms);
        txt(t, 28, 138, 1.3f, TH.text, AL_L);
    }

    /* goal preview */
    txt("GOAL", 300, 44, 0.5f, TH.dim, AL_C);
    draw_mini_goal(300, 112, 104);

    /* bottom info line */
    rect(0, 196, TOP_W, 44, TH.panel);
    if (g_mode == M_CLASSIC) {
        int par = level_info(g_level).scramble;
        star(26, 212, 7, TH.accent); star(40, 212, 7, TH.accent); star(54, 212, 7, TH.accent);
        snprintf(b, sizeof b, "<= %d", par);
        txt(b, 66, 202, 0.55f, TH.text, AL_L);
        star(130, 212, 7, TH.accent); star(144, 212, 7, TH.accent);
        snprintf(b, sizeof b, "<= %d", par * 2);
        txt(b, 156, 202, 0.55f, TH.text, AL_L);
        star(226, 212, 7, TH.accent);
        txt("more", 238, 202, 0.55f, TH.text, AL_L);
        if (g_save.best_moves[g_level]) {
            fmt_time(t, sizeof t, (int64_t)g_save.best_secs[g_level] * 1000);
            snprintf(b, sizeof b, "Best: %d moves  /  %s", g_save.best_moves[g_level], t);
            txt(b, 14, 220, 0.5f, TH.dim, AL_L);
        } else {
            txt("Not completed yet", 14, 220, 0.5f, TH.dim, AL_L);
        }
    } else if (g_mode == M_TIME) {
        snprintf(b, sizeof b, "Solved: %d     Best: %d     +%ds per board", g_score, g_save.best_time_attack[g_size_n - 3], TA_BONUS_MS / 1000);
        txt(b, 14, 206, 0.6f, TH.text, AL_L);
    } else if (g_mode == M_LIMIT) {
        snprintf(b, sizeof b, "Streak: %d     Best streak: %d", g_score, g_save.best_streak[g_size_n - 3]);
        txt(b, 14, 206, 0.6f, TH.text, AL_L);
    } else {
        snprintf(b, sizeof b, "Puzzles solved this session: %d", g_score);
        txt(b, 14, 206, 0.6f, TH.text, AL_L);
    }

    if (g_toast_ms > 0) txt_mid(g_toast, TOP_W * 0.5f, 100, 1.6f, TH.accent);

    if (g_phase == P_WON || g_phase == P_LOST || g_phase == P_PAUSE) {
        rect(0, 34, TOP_W, 162, RGBA(0, 0, 0, 150));
        if (g_phase == P_PAUSE) {
            txt_mid("PAUSED", TOP_W * 0.5f, 100, 1.5f, TH.text);
        } else if (g_phase == P_WON) {
            txt_mid("SOLVED!", TOP_W * 0.5f, 78, 1.5f, TH.accent);
            if (g_mode == M_CLASSIC) {
                stars_row(TOP_W * 0.5f, 122, 17, g_stars_earned);
                snprintf(b, sizeof b, "%d moves   %s", g_board.moves, g_new_best ? "NEW BEST!" : "");
                txt_mid(b, TOP_W * 0.5f, 160, 0.7f, TH.text);
            } else if (g_mode == M_LIMIT) {
                snprintf(b, sizeof b, "Streak: %d  %s", g_score, g_new_best ? "NEW BEST!" : "");
                txt_mid(b, TOP_W * 0.5f, 130, 0.8f, TH.text);
            } else {
                snprintf(b, sizeof b, "%d moves", g_board.moves);
                txt_mid(b, TOP_W * 0.5f, 130, 0.8f, TH.text);
            }
        } else {
            if (g_mode == M_TIME) {
                txt_mid("TIME'S UP!", TOP_W * 0.5f, 80, 1.5f, RGB(255, 110, 100));
                snprintf(b, sizeof b, "Boards solved: %d   (best %d)", g_score, g_save.best_time_attack[g_size_n - 3]);
                txt_mid(b, TOP_W * 0.5f, 130, 0.7f, TH.text);
            } else {
                txt_mid("OUT OF MOVES", TOP_W * 0.5f, 80, 1.4f, RGB(255, 110, 100));
                snprintf(b, sizeof b, "Best streak: %d", g_save.best_streak[g_size_n - 3]);
                txt_mid(b, TOP_W * 0.5f, 130, 0.7f, TH.text);
            }
        }
    }
}

static void draw_menu_top(void)
{
    txt_mid("TILE PUZZLE", TOP_W * 0.5f, 48, 1.7f, TH.text);
    txt_mid("DELUXE EDITION", TOP_W * 0.5f, 84, 0.65f, TH.accent);
    /* decorative 3x3 */
    const float ts = 38;
    float x0 = TOP_W * 0.5f - ts * 1.5f, y0 = 106;
    rect(x0 - 5, y0 - 5, ts * 3 + 10, ts * 3 + 10, TH.panel);
    for (int i = 0; i < 8; i++) {
        float x = x0 + (i % 3) * ts, y = y0 + (i / 3) * ts;
        rect(x + 2, y + 2, ts - 4, ts - 4, (i == 2 || i == 4) ? TH.tile_ok : TH.tile);
        char b[4];
        snprintf(b, sizeof b, "%d", i + 1);
        txt_mid(b, x + ts * 0.5f, y + ts * 0.5f, 0.7f, TH.text);
    }
    txt_mid("by fragahh", TOP_W * 0.5f, 232, 0.45f, TH.dim);
}

static void draw_info_top(const char *title, const char *l1, const char *l2, const char *l3)
{
    rect(0, 0, TOP_W, 34, TH.panel);
    txt(title, 14, 4, 0.8f, TH.text, AL_L);
    txt_mid(l1, TOP_W * 0.5f, 90, 0.9f, TH.text);
    txt_mid(l2, TOP_W * 0.5f, 130, 0.65f, TH.dim);
    txt_mid(l3, TOP_W * 0.5f, 160, 0.65f, TH.dim);
}

static void draw_howto_top(void)
{
    rect(0, 0, TOP_W, 34, TH.panel);
    txt("HOW TO PLAY", 14, 4, 0.8f, TH.text, AL_L);
    static const char *L[] = {
        "Slide the tiles until the numbers go in order:",
        "1, 2, 3 ... with the empty space in the bottom-right.",
        "",
        "Tap a tile in the same row or column as the empty",
        "space to slide it (a whole line counts as ONE move).",
        "The D-Pad slides tiles too.  START or B pauses.",
        "",
        "Classic: 45 levels, earn up to 3 stars per level.",
        "Time Attack: solve boards before time runs out.",
        "Move Limit: finish inside the move limit - build a streak!",
        "Zen: relax, no timer pressure, no limits.",
    };
    for (int i = 0; i < (int)(sizeof L / sizeof L[0]); i++)
        txt(L[i], 16, 46 + i * 17.0f, 0.5f, i < 6 ? TH.text : TH.dim, AL_L);
}

/* -------------------------------------------------- bottom screen: menus */
static const char *MENU_LABELS[] = { "Classic", "Time Attack", "Move Limit", "Zen", "How to Play", "Settings", "Exit" };
#define MENU_N 7
static Rect menu_rect(int i) { return R(60, 8 + i * 32.0f, 200, 28); }

static Rect size_rect(int i) { return R(40, 34 + i * 58.0f, 240, 50); }
static Rect back_rect(void) { return R(8, 214, 84, 22); }
static Rect cell_rect(int i) { return R(10 + (i % 5) * 60.0f, 50 + (i / 5) * 56.0f, 56, 52); }
static Rect arrow_rect(bool right) { return R(right ? 272.0f : 8.0f, 8, 40, 30); }
static Rect set_rect(int i) { return R(20, 10 + i * 34.0f, 280, 30); }
static Rect pause_rect(void) { return R(232, 12, 82, 34); }

static void draw_menu_bottom(void)
{
    for (int i = 0; i < MENU_N; i++) draw_button(menu_rect(i), MENU_LABELS[i], i == g_menu_sel, true);
}

static void draw_size_bottom(void)
{
    static const char *D[] = { "Easy", "Medium", "Hard" };
    txt_mid("Choose a board size", BOT_W * 0.5f, 18, 0.65f, TH.text);
    for (int i = 0; i < 3; i++) {
        Rect r = size_rect(i);
        char b[48];
        snprintf(b, sizeof b, "%dx%d  -  %s", 3 + i, 3 + i, D[i]);
        draw_button(r, b, i == g_size_sel, true);
        if (g_mode == M_TIME) snprintf(b, sizeof b, "Best: %d boards", g_save.best_time_attack[i]);
        else if (g_mode == M_LIMIT) snprintf(b, sizeof b, "Best streak: %d", g_save.best_streak[i]);
        else b[0] = 0;
        if (b[0]) txt_mid(b, r.x + r.w * 0.5f, r.y + r.h - 9, 0.4f, i == g_size_sel ? TH.panel : TH.dim);
    }
    draw_button(back_rect(), "Back", false, true);
}

static void draw_levels_bottom(void)
{
    char b[48];
    snprintf(b, sizeof b, "World %d  -  %dx%d", g_world + 1, 3 + g_world, 3 + g_world);
    txt_mid(b, BOT_W * 0.5f, 23, 0.7f, TH.text);
    draw_button(arrow_rect(false), "<", false, g_world > 0);
    draw_button(arrow_rect(true), ">", false, g_world < NUM_WORLDS - 1);

    for (int i = 0; i < LEVELS_PER_WORLD; i++) {
        int lvl = g_world * LEVELS_PER_WORLD + i;
        Rect r = cell_rect(i);
        bool open = unlocked(lvl);
        bool sel = (lvl == g_lsel);
        rect(r.x + 2, r.y + 3, r.w, r.h, RGBA(0, 0, 0, 90));
        rect(r.x, r.y, r.w, r.h, !open ? TH.panel : (g_save.stars[lvl] ? TH.tile_ok : TH.tile));
        if (sel) {
            rect(r.x - 2, r.y - 2, r.w + 4, 3, TH.accent);
            rect(r.x - 2, r.y + r.h - 1, r.w + 4, 3, TH.accent);
            rect(r.x - 2, r.y - 2, 3, r.h + 4, TH.accent);
            rect(r.x + r.w - 1, r.y - 2, 3, r.h + 4, TH.accent);
        }
        snprintf(b, sizeof b, "%d", lvl + 1);
        txt_mid(b, r.x + r.w * 0.5f, r.y + 20, 0.8f, open ? TH.text : TH.dim);
        if (open) {
            for (int s = 0; s < 3; s++)
                star(r.x + r.w * 0.5f + (s - 1) * 14.0f, r.y + r.h - 12, 5.5f,
                     s < g_save.stars[lvl] ? TH.accent : RGBA(255, 255, 255, 50));
        } else {
            txt_mid("locked", r.x + r.w * 0.5f, r.y + r.h - 12, 0.4f, TH.dim);
        }
    }
    draw_button(back_rect(), "Back", false, true);
    txt("L/R: world   A: play", BOT_W - 8, 220, 0.45f, TH.dim, AL_R);
}

static void set_label(int i, char *b, size_t n)
{
    switch (i) {
    case 0: snprintf(b, n, "Theme: %s", TH.name); break;
    case 1: snprintf(b, n, "Animations: %s", g_save.anim ? "On" : "Off"); break;
    case 2: snprintf(b, n, "Timer: %s", g_save.timer ? "Shown" : "Hidden"); break;
    case 3: snprintf(b, n, "D-Pad moves: %s", g_save.dpad_blank ? "Empty space" : "Tile"); break;
    case 4: snprintf(b, n, "%s", g_confirm_reset ? "Tap again to ERASE all progress" : "Reset Progress"); break;
    default: snprintf(b, n, "Back"); break;
    }
}

static void draw_settings_bottom(void)
{
    for (int i = 0; i < 6; i++) {
        char b[48];
        set_label(i, b, sizeof b);
        draw_button(set_rect(i), b, i == g_set_sel, true);
    }
}

static void draw_settings_top(void)
{
    rect(0, 0, TOP_W, 34, TH.panel);
    txt("SETTINGS", 14, 4, 0.8f, TH.text, AL_L);
    /* live theme preview */
    const float ts = 44;
    float x0 = TOP_W * 0.5f - ts * 1.5f, y0 = 62;
    rect(x0 - 5, y0 - 5, ts * 3 + 10, ts * 3 + 10, TH.panel);
    for (int i = 0; i < 8; i++) {
        float x = x0 + (i % 3) * ts, y = y0 + (i / 3) * ts;
        rect(x + 2, y + 2, ts - 4, ts - 4, (i % 3 == i / 3) ? TH.tile_ok : TH.tile);
        char b[4];
        snprintf(b, sizeof b, "%d", i + 1);
        txt_mid(b, x + ts * 0.5f, y + ts * 0.5f, 0.8f, TH.text);
    }
    static const char *HINT[] = {
        "Left/Right or tap: change the color theme",
        "Smooth tile sliding on or off",
        "Show or hide the timer while playing",
        "Choose what the D-Pad moves",
        "Erases stars, bests and records",
        "Return to the main menu",
    };
    txt_mid(HINT[g_set_sel], TOP_W * 0.5f, 218, 0.55f, TH.dim);
}

static void draw_play_bottom(void)
{
    draw_board();

    /* side column */
    draw_button(pause_rect(), "Pause", false, g_phase == P_PLAY);
    txt("START / B", 273, 52, 0.4f, TH.dim, AL_C);
    txt("Tap a tile or", 273, 86, 0.42f, TH.dim, AL_C);
    txt("use the D-Pad", 273, 99, 0.42f, TH.dim, AL_C);
    if (g_mode == M_LIMIT) {
        char b[24];
        snprintf(b, sizeof b, "%d left", g_limit - g_board.moves);
        txt(b, 273, 150, 0.6f, TH.text, AL_C);
    }

    if (g_phase != P_PLAY) {
        rect(0, 0, BOT_W, SCR_H, RGBA(0, 0, 0, 170));
        for (int i = 0; i < g_ov_count; i++) draw_button(ov_rect(i), g_ov_lbl[i], i == g_ov_sel, true);
    }
}

/* --------------------------------------------------------------- updates */
static void list_nav(int *sel, int count, u32 kd)
{
    if (kd & KEY_DOWN) *sel = (*sel + 1) % count;
    if (kd & KEY_UP) *sel = (*sel + count - 1) % count;
}

static void goto_levels(void)
{
    int first = 0;
    while (first < NUM_LEVELS - 1 && g_save.stars[first] > 0) first++;
    g_lsel = first;
    g_world = first / LEVELS_PER_WORLD;
    g_screen = S_LEVELS;
}

static void menu_activate(int i)
{
    switch (i) {
    case 0: g_mode = M_CLASSIC; goto_levels(); break;
    case 1: g_mode = M_TIME;  g_size_sel = 0; g_screen = S_SIZE; break;
    case 2: g_mode = M_LIMIT; g_size_sel = 0; g_screen = S_SIZE; break;
    case 3: g_mode = M_ZEN;   g_size_sel = 0; g_screen = S_SIZE; break;
    case 4: g_screen = S_HOWTO; break;
    case 5: g_screen = S_SETTINGS; g_set_sel = 0; g_confirm_reset = false; break;
    default: g_running = false; break;
    }
}

static void update_menu(u32 kd, bool tap, touchPosition tp)
{
    list_nav(&g_menu_sel, MENU_N, kd);
    if (kd & KEY_A) menu_activate(g_menu_sel);
    if (tap)
        for (int i = 0; i < MENU_N; i++)
            if (hit(menu_rect(i), tp.px, tp.py)) { g_menu_sel = i; menu_activate(i); break; }
}

static void update_size(u32 kd, bool tap, touchPosition tp)
{
    list_nav(&g_size_sel, 3, kd);
    if (kd & KEY_B) g_screen = S_MENU;
    if (kd & KEY_A) start_game();
    if (tap) {
        if (hit(back_rect(), tp.px, tp.py)) { g_screen = S_MENU; return; }
        for (int i = 0; i < 3; i++)
            if (hit(size_rect(i), tp.px, tp.py)) { g_size_sel = i; start_game(); return; }
    }
}

static void set_world(int w)
{
    if (w < 0 || w >= NUM_WORLDS) return;
    g_world = w;
    int idx = g_lsel % LEVELS_PER_WORLD;
    g_lsel = w * LEVELS_PER_WORLD + idx;
}

static void update_levels(u32 kd, bool tap, touchPosition tp)
{
    int idx = g_lsel % LEVELS_PER_WORLD;
    if (kd & KEY_B) { g_screen = S_MENU; return; }
    if (kd & KEY_L) set_world(g_world - 1);
    if (kd & KEY_R) set_world(g_world + 1);
    idx = g_lsel % LEVELS_PER_WORLD;
    if ((kd & KEY_LEFT) && idx % 5 > 0) g_lsel--;
    if ((kd & KEY_RIGHT) && idx % 5 < 4) g_lsel++;
    if ((kd & KEY_UP) && idx >= 5) g_lsel -= 5;
    if ((kd & KEY_DOWN) && idx < 10) g_lsel += 5;
    if ((kd & KEY_A) && unlocked(g_lsel)) { start_game(); return; }
    if (tap) {
        if (hit(back_rect(), tp.px, tp.py)) { g_screen = S_MENU; return; }
        if (hit(arrow_rect(false), tp.px, tp.py)) set_world(g_world - 1);
        if (hit(arrow_rect(true), tp.px, tp.py)) set_world(g_world + 1);
        for (int i = 0; i < LEVELS_PER_WORLD; i++) {
            if (hit(cell_rect(i), tp.px, tp.py)) {
                int lvl = g_world * LEVELS_PER_WORLD + i;
                g_lsel = lvl;
                if (unlocked(lvl)) { start_game(); return; }
            }
        }
    }
}

static void settings_change(int i, int dir)
{
    switch (i) {
    case 0: g_save.theme = (uint8_t)((g_save.theme + NUM_THEMES + dir) % NUM_THEMES); break;
    case 1: g_save.anim ^= 1; break;
    case 2: g_save.timer ^= 1; break;
    case 3: g_save.dpad_blank ^= 1; break;
    case 4:
        if (!g_confirm_reset) { g_confirm_reset = true; return; }
        save_defaults();
        g_confirm_reset = false;
        break;
    default:
        save_write();
        g_screen = S_MENU;
        return;
    }
    if (i != 4) g_confirm_reset = false;
    save_write();
}

static void update_settings(u32 kd, bool tap, touchPosition tp)
{
    int before = g_set_sel;
    list_nav(&g_set_sel, 6, kd);
    if (g_set_sel != before) g_confirm_reset = false;
    if (kd & KEY_B) { save_write(); g_screen = S_MENU; return; }
    if (kd & KEY_LEFT) settings_change(g_set_sel, -1);
    if (kd & (KEY_RIGHT | KEY_A)) settings_change(g_set_sel, +1);
    if (tap)
        for (int i = 0; i < 6; i++)
            if (hit(set_rect(i), tp.px, tp.py)) { g_set_sel = i; settings_change(i, +1); break; }
}

static void update_howto(u32 kd, bool tap, touchPosition tp)
{
    if ((kd & (KEY_A | KEY_B)) || (tap && hit(back_rect(), tp.px, tp.py))) g_screen = S_MENU;
}

static bool try_dpad(Dir d)
{
    Dir tile_dir = g_save.dpad_blank ? opposite(d) : d;
    return board_slide_dir(&g_board, tile_dir);
}

static void update_play(u32 kd, bool tap, touchPosition tp, u32 dt)
{
    if (g_toast_ms > 0) g_toast_ms -= (int)dt;

    if (g_phase == P_PLAY) {
        if (kd & (KEY_START | KEY_B)) { g_phase = P_PAUSE; build_overlay(); return; }
        if (tap && hit(pause_rect(), tp.px, tp.py)) { g_phase = P_PAUSE; build_overlay(); return; }

        bool moved = false;
        if (kd & KEY_DUP) moved |= try_dpad(DIR_UP);
        if (kd & KEY_DDOWN) moved |= try_dpad(DIR_DOWN);
        if (kd & KEY_DLEFT) moved |= try_dpad(DIR_LEFT);
        if (kd & KEY_DRIGHT) moved |= try_dpad(DIR_RIGHT);
        if (tap) {
            int idx = hit_tile(tp.px, tp.py);
            if (idx >= 0) moved |= board_slide_at(&g_board, idx);
        }
        if (moved) after_move();

        if (g_phase == P_PLAY && g_started) {
            if (g_mode == M_TIME) {
                g_time_ms -= dt;
                if (g_time_ms <= 0) on_lost();
            } else {
                g_time_ms += dt;
            }
        }
        return;
    }

    /* overlays (pause / won / lost) */
    if (kd & KEY_DOWN) g_ov_sel = (g_ov_sel + 1) % g_ov_count;
    if (kd & KEY_UP) g_ov_sel = (g_ov_sel + g_ov_count - 1) % g_ov_count;
    if ((kd & (KEY_START | KEY_B)) && g_phase == P_PAUSE) { do_action(A_RESUME); return; }
    if (kd & KEY_A) { do_action(g_ov_act[g_ov_sel]); return; }
    if (tap)
        for (int i = 0; i < g_ov_count; i++)
            if (hit(ov_rect(i), tp.px, tp.py)) { g_ov_sel = i; do_action(g_ov_act[i]); return; }
}

/* ------------------------------------------------------------------ main */
int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_buf = C2D_TextBufNew(4096);

    save_load();
    g_last_tick = osGetTime();

    while (aptMainLoop() && g_running) {
        hidScanInput();
        u32 kd = hidKeysDown();
        touchPosition tp;
        memset(&tp, 0, sizeof tp);
        bool tap = (kd & KEY_TOUCH) != 0;
        if (tap) hidTouchRead(&tp);

        u64 now = osGetTime();
        u32 dt = (u32)(now - g_last_tick);
        if (dt > 100) dt = 100;
        g_last_tick = now;

        switch (g_screen) {
        case S_MENU:     update_menu(kd, tap, tp); break;
        case S_SIZE:     update_size(kd, tap, tp); break;
        case S_LEVELS:   update_levels(kd, tap, tp); break;
        case S_PLAY:     update_play(kd, tap, tp, dt); break;
        case S_SETTINGS: update_settings(kd, tap, tp); break;
        case S_HOWTO:    update_howto(kd, tap, tp); break;
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TextBufClear(g_buf);

        /* top screen */
        C2D_TargetClear(g_top, TH.bg);
        C2D_SceneBegin(g_top);
        g_z = 0.0f;
        draw_bg(true);
        switch (g_screen) {
        case S_MENU:     draw_menu_top(); break;
        case S_SIZE: {
            static const char *DESC[] = {
                "Solve as many boards as you can!", "Finish each board inside the move limit!", "Relax and solve at your own pace." };
            int mi = (g_mode == M_TIME) ? 0 : (g_mode == M_LIMIT ? 1 : 2);
            draw_info_top(MODE_NAMES[g_mode], DESC[mi], "Pick a size on the touch screen,", "or use the D-Pad and press A.");
            break;
        }
        case S_LEVELS: {
            char a[48], b[48], c[48];
            int total = 0;
            for (int i = 0; i < NUM_LEVELS; i++) total += g_save.stars[i];
            snprintf(a, sizeof a, "LEVEL %d", g_lsel + 1);
            LevelInfo li = level_info(g_lsel);
            snprintf(b, sizeof b, "%dx%d board  -  3 stars: %d moves or fewer", li.n, li.n, li.scramble);
            snprintf(c, sizeof c, "Total stars: %d / %d", total, NUM_LEVELS * 3);
            draw_info_top("CLASSIC", a, b, c);
            break;
        }
        case S_PLAY:     draw_play_top(); break;
        case S_SETTINGS: draw_settings_top(); break;
        case S_HOWTO:    draw_howto_top(); break;
        }

        /* bottom screen */
        C2D_TargetClear(g_bot, TH.bg);
        C2D_SceneBegin(g_bot);
        g_z = 0.0f;
        draw_bg(false);
        switch (g_screen) {
        case S_MENU:     draw_menu_bottom(); break;
        case S_SIZE:     draw_size_bottom(); break;
        case S_LEVELS:   draw_levels_bottom(); break;
        case S_PLAY:     draw_play_bottom(); break;
        case S_SETTINGS: draw_settings_bottom(); break;
        case S_HOWTO:
            txt_mid("Good luck!", BOT_W * 0.5f, 110, 0.9f, TH.accent);
            draw_button(back_rect(), "Back", false, true);
            break;
        }

        C3D_FrameEnd(0);
    }

    save_write();
    C2D_TextBufDelete(g_buf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
