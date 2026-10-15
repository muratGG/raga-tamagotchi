/*
 * Catch: apples and rocks fall; the pet runs along the ground catching
 * apples (a point each) and dodging rocks (three lives). Everything speeds
 * up as the score grows.
 *   LEFT / RIGHT  run (PLAY starts a round)
 *   BACK          leave, at any time
 */
#include "buttons.h"
#include "fb.h"
#include "game.h"
#include "gameutil.h"
#include "sprites.h"

#define STEP_MS   20
#define FP        16
#define GROUND_Y  62
#define PET_SIZE  16
#define PET_Y     (GROUND_Y - PET_SIZE)
#define RUN_PX    2                     /* per step */
#define MAX_ITEMS 5
#define ITEM_SIZE 8
#define LIVES     3

#define SPAWN_START_MS 1100
#define SPAWN_MIN_MS   450
#define FALL_START     10               /* fixed point px per step */
#define FALL_MAX       32
#define ROCK_CHANCE    30               /* percent */

#define REWARD_PER_POINT 2

#define C_SKY    RGB(150, 210, 255)
#define C_GROUND RGB(70, 180, 80)

enum cstate { C_READY, C_RUN, C_DEAD };

static const char *const rock[8] = {
    "..ssss..",
    ".ssssss.",
    "ssbsssss",
    "ssssssss",
    "sssssbss",
    "ssssssss",
    ".ssssss.",
    "..ssss..",
};

static const char *const icon[8] = {
    "...n....",
    ".rrrr...",
    ".rrrr...",
    "..rr....",
    "........",
    "y......y",
    "yyyyyyyy",
    ".yyyyyy.",
};

static struct {
    enum cstate state;
    int x;                               /* pet, px */
    struct { int active, rock, x, y; } item[MAX_ITEMS]; /* y fixed point */
    int lives;
    unsigned spawn_t, step_t, state_t0, hurt_until;
    unsigned score, best, session_best, rng;
} c;

static void reset_round(unsigned now)
{
    c.state = C_READY;
    c.state_t0 = now;
    c.x = (LCD_W - PET_SIZE) / 2;
    c.lives = LIVES;
    c.score = 0;
    c.hurt_until = 0;
    for (int i = 0; i < MAX_ITEMS; i++)
        c.item[i].active = 0;
}

static void start(unsigned now)
{
    c.rng ^= now * 2654435761u + 3;
    c.session_best = 0;
    reset_round(now);
}

static unsigned spawn_ms(void)
{
    unsigned faster = c.score * 25;
    return faster >= SPAWN_START_MS - SPAWN_MIN_MS ? SPAWN_MIN_MS : SPAWN_START_MS - faster;
}

static int fall_speed(void)
{
    int v = FALL_START + (int)c.score / 2;
    return v > FALL_MAX ? FALL_MAX : v;
}

static void spawn(void)
{
    for (int i = 0; i < MAX_ITEMS; i++)
        if (!c.item[i].active) {
            c.item[i].active = 1;
            c.item[i].rock = game_rnd(&c.rng, 100) < ROCK_CHANCE;
            c.item[i].x = game_rnd(&c.rng, LCD_W - ITEM_SIZE);
            c.item[i].y = -ITEM_SIZE * FP;
            return;
        }
}

static void die(unsigned now)
{
    c.state = C_DEAD;
    c.state_t0 = now;
    if (c.score > c.best)
        c.best = c.score;
    if (c.score > c.session_best)
        c.session_best = c.score;
}

static void physics_step(unsigned now, unsigned held)
{
    if (held & BTN_LEFT)
        c.x -= RUN_PX;
    if (held & BTN_RIGHT)
        c.x += RUN_PX;
    if (c.x < 0)
        c.x = 0;
    if (c.x > LCD_W - PET_SIZE)
        c.x = LCD_W - PET_SIZE;

    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!c.item[i].active)
            continue;
        c.item[i].y += fall_speed();
        int iy = c.item[i].y / FP, ix = c.item[i].x;

        /* caught: overlaps the pet's upper body (inset hitbox) */
        if (iy + ITEM_SIZE > PET_Y + 3 && iy < PET_Y + 10 &&
            ix + ITEM_SIZE > c.x + 2 && ix < c.x + PET_SIZE - 2) {
            c.item[i].active = 0;
            if (c.item[i].rock) {
                c.hurt_until = now + 600;
                if (--c.lives == 0)
                    die(now);
            } else {
                c.score++;
            }
        } else if (iy > GROUND_Y) {
            c.item[i].active = 0; /* missed apples cost nothing */
        }
    }
}

static int update(unsigned now, unsigned pressed, unsigned held)
{
    switch (c.state) {
    case C_READY:
        if (pressed & BTN_BACK)
            return 1;
        if (pressed & (BTN_PLAY | BTN_LEFT | BTN_RIGHT)) {
            c.state = C_RUN;
            c.step_t = c.spawn_t = now;
        }
        break;
    case C_RUN:
        if (pressed & BTN_BACK) { /* leave mid-round; the score still counts */
            die(now);
            return 1;
        }
        while (now - c.step_t >= STEP_MS && c.state == C_RUN) {
            c.step_t += STEP_MS;
            physics_step(c.step_t, held);
        }
        if (c.state == C_RUN && now - c.spawn_t >= spawn_ms()) {
            c.spawn_t = now;
            spawn();
        }
        break;
    case C_DEAD:
        switch (game_over_input(now, c.state_t0, pressed)) {
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
    fb_rect(0, 0, LCD_W, GROUND_Y, C_SKY);
    fb_rect(0, GROUND_Y, LCD_W, LCD_H - GROUND_Y, C_GROUND);

    for (int i = 0; i < MAX_ITEMS; i++)
        if (c.item[i].active)
            fb_sprite(c.item[i].x, c.item[i].y / FP, c.item[i].rock ? rock : apple,
                      ITEM_SIZE, ITEM_SIZE, 1, palette);

    /* blink while hurt */
    if (now >= c.hurt_until || (now / 100) % 2)
        game_draw_pet(c.x, PET_Y, c.state == C_DEAD);

    for (int i = 0; i < c.lives; i++)
        fb_sprite(2 + i * 9, 2, heart, 7, 7, 1, palette);

    if (c.state == C_DEAD)
        game_draw_over(c.score, c.best);
    else
        game_draw_score(c.score);
}

static int reward(void) { return game_reward(c.session_best, REWARD_PER_POINT); }

const struct game catch_game = {icon, start, update, render, reward};
