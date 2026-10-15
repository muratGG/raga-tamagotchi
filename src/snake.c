/*
 * Snake: the arrows steer, apples make the snake longer and faster.
 * Hitting a wall or itself ends the round.
 *   arrows        steer (any arrow or PLAY starts a round)
 *   BACK          leave, at any time
 */
#include "buttons.h"
#include "fb.h"
#include "game.h"
#include "gameutil.h"
#include "sprites.h"

#define CELL     4
#define TOP      8                     /* score strip above the field */
#define COLS     (LCD_W / CELL)        /* 32 */
#define ROWS     ((LCD_H - TOP) / CELL) /* 14 */
#define MAX_LEN  (COLS * ROWS)
#define START_LEN 4

#define STEP_START_MS 170
#define STEP_MIN_MS   70
#define STEP_FASTER_MS 5               /* per apple */

#define REWARD_PER_POINT 3

#define C_FIELD  RGB(25, 60, 35)
#define C_STRIP  RGB(30, 30, 44)
#define C_SNAKE  RGB(110, 210, 90)

enum sstate { S_READY, S_RUN, S_DEAD };
enum dir { D_UP, D_RIGHT, D_DOWN, D_LEFT };

static const char *const icon[8] = {
    "........",
    ".gggg.r.",
    ".g......",
    ".g......",
    ".ggggg..",
    ".....g..",
    "..gggg..",
    "........",
};

static struct {
    enum sstate state;
    unsigned char x[MAX_LEN], y[MAX_LEN]; /* ring buffer, head at index head */
    int head, len;
    enum dir dir, next_dir;
    int food_x, food_y;
    unsigned step_ms, step_t, state_t0;
    unsigned score, best, session_best, rng;
} s;

static int body_at(int x, int y)
{
    for (int i = 0; i < s.len; i++) {
        int k = (s.head - i + MAX_LEN) % MAX_LEN;
        if (s.x[k] == x && s.y[k] == y)
            return 1;
    }
    return 0;
}

static void place_food(void)
{
    do {
        s.food_x = game_rnd(&s.rng, COLS);
        s.food_y = game_rnd(&s.rng, ROWS);
    } while (body_at(s.food_x, s.food_y));
}

static void reset_round(unsigned now)
{
    s.state = S_READY;
    s.state_t0 = now;
    s.len = START_LEN;
    s.head = START_LEN - 1;
    for (int i = 0; i < START_LEN; i++) {
        s.x[i] = COLS / 2 - START_LEN + 1 + i;
        s.y[i] = ROWS / 2;
    }
    s.dir = s.next_dir = D_RIGHT;
    s.step_ms = STEP_START_MS;
    s.score = 0;
    place_food();
}

static void start(unsigned now)
{
    s.rng ^= now * 2654435761u + 7;
    s.session_best = 0;
    reset_round(now);
}

static void die(unsigned now)
{
    s.state = S_DEAD;
    s.state_t0 = now;
    if (s.score > s.best)
        s.best = s.score;
    if (s.score > s.session_best)
        s.session_best = s.score;
}

/* Returns 0 when the snake crashes. */
static int step(void)
{
    static const int dx[] = {0, 1, 0, -1}, dy[] = {-1, 0, 1, 0};
    int hx, hy, grow;

    s.dir = s.next_dir;
    hx = s.x[s.head] + dx[s.dir];
    hy = s.y[s.head] + dy[s.dir];
    if (hx < 0 || hx >= COLS || hy < 0 || hy >= ROWS)
        return 0;

    grow = hx == s.food_x && hy == s.food_y;
    if (!grow)
        s.len--; /* the tail moves away first, so following it is allowed */
    if (body_at(hx, hy))
        return 0;

    s.head = (s.head + 1) % MAX_LEN;
    s.x[s.head] = hx;
    s.y[s.head] = hy;
    s.len++;
    if (grow) {
        s.score++;
        if (s.step_ms > STEP_MIN_MS)
            s.step_ms -= STEP_FASTER_MS;
        if (s.len < MAX_LEN)
            place_food();
    }
    return 1;
}

static void steer(unsigned pressed)
{
    static const struct { unsigned btn; enum dir d; } keys[] = {
        {BTN_UP, D_UP}, {BTN_RIGHT, D_RIGHT}, {BTN_DOWN, D_DOWN}, {BTN_LEFT, D_LEFT},
    };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        if ((pressed & keys[i].btn) && (keys[i].d + 2) % 4 != s.dir) /* no U-turn */
            s.next_dir = keys[i].d;
}

static int update(unsigned now, unsigned pressed, unsigned held)
{
    (void)held;
    unsigned arrows = BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT;

    switch (s.state) {
    case S_READY:
        if (pressed & BTN_BACK)
            return 1;
        if (pressed & (arrows | BTN_PLAY)) {
            steer(pressed);
            s.state = S_RUN;
            s.step_t = now;
        }
        break;
    case S_RUN:
        if (pressed & BTN_BACK) { /* leave mid-round; the score still counts */
            die(now);
            return 1;
        }
        steer(pressed);
        while (now - s.step_t >= s.step_ms) {
            s.step_t += s.step_ms;
            if (!step()) {
                die(now);
                break;
            }
        }
        break;
    case S_DEAD:
        switch (game_over_input(now, s.state_t0, pressed)) {
        case GO_EXIT:    return 1;
        case GO_RESTART: reset_round(now); break;
        case GO_NONE:    break;
        }
        break;
    }
    return 0;
}

static void cell(int x, int y, u16 c)
{
    fb_rect(x * CELL, TOP + y * CELL, CELL - 1, CELL - 1, c);
}

static void render(unsigned now)
{
    (void)now;
    fb_rect(0, 0, LCD_W, TOP, C_STRIP);
    fb_rect(0, TOP, LCD_W, LCD_H - TOP, C_FIELD);

    fb_rect(s.food_x * CELL, TOP + s.food_y * CELL, CELL - 1, CELL - 1, palette('r'));
    fb_set(s.food_x * CELL + 1, TOP + s.food_y * CELL - 1, palette('g'));

    for (int i = s.len - 1; i >= 0; i--) {
        int k = (s.head - i + MAX_LEN) % MAX_LEN;
        cell(s.x[k], s.y[k], i == 0 ? pet->body_color : C_SNAKE);
    }
    /* eyes on the head */
    fb_set(s.x[s.head] * CELL + 1, TOP + s.y[s.head] * CELL + 1, palette('b'));

    if (s.state == S_DEAD) {
        game_draw_over(s.score, s.best);
    } else {
        int x = (LCD_W - fb_number_width(s.score, 1)) / 2;
        fb_number(x, 1, s.score, 1, RGB(255, 255, 255));
    }
}

static int reward(void) { return game_reward(s.session_best, REWARD_PER_POINT); }

const struct game snake_game = {icon, start, update, render, reward};
