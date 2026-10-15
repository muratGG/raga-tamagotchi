/*
 * Runner: the pet runs to the right and jumps over cacti and rocks; every
 * obstacle cleared is a point, and the run keeps getting faster.
 *   PLAY / UP     jump (also starts a round)
 *   BACK          leave, at any time
 */
#include "buttons.h"
#include "fb.h"
#include "game.h"
#include "gameutil.h"
#include "sprites.h"

#define STEP_MS   20
#define FP        16
#define GROUND_Y  56
#define PET_X     14
#define PET_SIZE  16
#define GRAVITY   7
#define JUMP_VY   (-82)
#define HIT_INSET 3

#define MAX_OBST  3
#define SPEED_START (FP + FP / 2)      /* 1.5 px per step */
#define SPEED_MAX   (3 * FP)
#define GAP_MIN   48
#define GAP_RANDOM 50

#define REWARD_PER_POINT 3

#define C_SKY    RGB(255, 230, 180)
#define C_SAND   RGB(230, 190, 120)
#define C_LINE   RGB(170, 130, 70)

enum rstate { R_READY, R_RUN, R_DEAD };

static const char *const cactus[12] = {
    "..GG..",
    ".GggG.",
    ".GggG.",
    "GGggGG",
    "GgggGg",
    "GgggGg",
    "GggggG",
    ".GggG.",
    ".GggG.",
    ".GggG.",
    ".GggG.",
    ".GggG.",
};

static const char *const stone[6] = {
    "..ssss..",
    ".ssssss.",
    "ssssbsss",
    "ssssssss",
    "sbssssss",
    "ssssssss",
};

static const char *const icon[8] = {
    "....GG..",
    "...GggG.",
    "GG.GggG.",
    "GgGGggGG",
    ".GGGggG.",
    "...GggG.",
    "...GggG.",
    "nnnnnnnn",
};

static struct {
    enum rstate state;
    int y, vy;                           /* pet's top edge, fixed point */
    struct { int x, kind; } obst[MAX_OBST]; /* x fixed point; kind 0 cactus 1 stone */
    int passed[MAX_OBST];
    int speed;
    unsigned step_t, state_t0;
    unsigned score, best, session_best, rng;
} r;

static int obst_w(int k) { return r.obst[k].kind ? 8 : 6; }
static int obst_h(int k) { return r.obst[k].kind ? 6 : 12; }

static int rightmost_x(void)
{
    int m = r.obst[0].x;
    for (int i = 1; i < MAX_OBST; i++)
        if (r.obst[i].x > m)
            m = r.obst[i].x;
    return m;
}

static void place(int i, int after_x)
{
    r.obst[i].x = after_x + (GAP_MIN + game_rnd(&r.rng, GAP_RANDOM)) * FP;
    r.obst[i].kind = game_rnd(&r.rng, 3) == 0;
    r.passed[i] = 0;
}

static void reset_round(unsigned now)
{
    r.state = R_READY;
    r.state_t0 = now;
    r.y = (GROUND_Y - PET_SIZE) * FP;
    r.vy = 0;
    r.speed = SPEED_START;
    r.score = 0;
    for (int i = 0; i < MAX_OBST; i++)
        place(i, i == 0 ? LCD_W * FP : r.obst[i - 1].x);
}

static void start(unsigned now)
{
    r.rng ^= now * 2654435761u + 5;
    r.session_best = 0;
    reset_round(now);
}

static int on_ground(void) { return r.y >= (GROUND_Y - PET_SIZE) * FP; }

static int collides(void)
{
    int top = r.y / FP + HIT_INSET, bottom = r.y / FP + PET_SIZE - 1 - HIT_INSET;
    int left = PET_X + HIT_INSET, right = PET_X + PET_SIZE - 1 - HIT_INSET;

    for (int i = 0; i < MAX_OBST; i++) {
        int ox = r.obst[i].x / FP, oy = GROUND_Y - obst_h(i);
        if (right >= ox && left <= ox + obst_w(i) - 1 && bottom >= oy && top <= GROUND_Y - 1)
            return 1;
    }
    return 0;
}

static void physics_step(void)
{
    r.vy += GRAVITY;
    r.y += r.vy;
    if (on_ground()) {
        r.y = (GROUND_Y - PET_SIZE) * FP;
        r.vy = 0;
    }

    for (int i = 0; i < MAX_OBST; i++) {
        r.obst[i].x -= r.speed;
        if (!r.passed[i] && r.obst[i].x / FP + obst_w(i) < PET_X) {
            r.passed[i] = 1;
            r.score++;
            if (r.speed < SPEED_MAX)
                r.speed++;
        }
        if (r.obst[i].x / FP + obst_w(i) < 0)
            place(i, rightmost_x());
    }
}

static void die(unsigned now)
{
    r.state = R_DEAD;
    r.state_t0 = now;
    if (r.score > r.best)
        r.best = r.score;
    if (r.score > r.session_best)
        r.session_best = r.score;
}

static int update(unsigned now, unsigned pressed, unsigned held)
{
    (void)held;
    int jump = pressed & (BTN_PLAY | BTN_UP);

    switch (r.state) {
    case R_READY:
        if (pressed & BTN_BACK)
            return 1;
        if (jump) {
            r.state = R_RUN;
            r.step_t = now;
            r.vy = JUMP_VY;
        }
        break;
    case R_RUN:
        if (pressed & BTN_BACK) { /* leave mid-round; the score still counts */
            die(now);
            return 1;
        }
        if (jump && on_ground())
            r.vy = JUMP_VY;
        while (now - r.step_t >= STEP_MS) {
            r.step_t += STEP_MS;
            physics_step();
            if (collides()) {
                die(now);
                break;
            }
        }
        break;
    case R_DEAD:
        switch (game_over_input(now, r.state_t0, pressed)) {
        case GO_EXIT:    return 1;
        case GO_RESTART: reset_round(now); break;
        case GO_NONE:    break;
        }
        break;
    }
    return 0;
}

static void render(unsigned now)
{
    int py = r.y / FP;

    fb_rect(0, 0, LCD_W, GROUND_Y, C_SKY);
    fb_rect(0, GROUND_Y, LCD_W, LCD_H - GROUND_Y, C_SAND);
    /* ground speckles scroll with the run */
    for (int x = -(int)((now / 20) % 16); x < LCD_W; x += 16)
        fb_rect(x, GROUND_Y + 3, 3, 1, C_LINE);
    fb_rect(0, GROUND_Y, LCD_W, 1, C_LINE);

    for (int i = 0; i < MAX_OBST; i++) {
        int ox = r.obst[i].x / FP;
        if (ox < LCD_W)
            fb_sprite(ox, GROUND_Y - obst_h(i), r.obst[i].kind ? stone : cactus,
                      obst_w(i), obst_h(i), 1, palette);
    }

    game_draw_pet(PET_X, py, r.state == R_DEAD);
    /* running feet */
    if (r.state == R_RUN && on_ground() && (now / 100) % 2)
        fb_rect(PET_X + 4, py + 14, 8, 1, C_SKY);

    if (r.state == R_DEAD)
        game_draw_over(r.score, r.best);
    else
        game_draw_score(r.score);
}

static int reward(void) { return game_reward(r.session_best, REWARD_PER_POINT); }

const struct game runner_game = {icon, start, update, render, reward};
