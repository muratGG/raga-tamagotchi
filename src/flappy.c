/*
 * Flappy pet: the pet flies through gaps between pipes.
 *   PLAY / UP     flap (also starts a round)
 *   BACK          leave, at any time
 * Each pipe passed is a point; points become happiness for the pet.
 */
#include "buttons.h"
#include "fb.h"
#include "game.h"
#include "gameutil.h"
#include "sprites.h"

#define STEP_MS     25          /* fixed physics step */
#define FP          16          /* fixed point: 1 px = 16 */
#define GRAVITY     5
#define FLAP_VY     (-46)
#define MAX_VY      56

#define BIRD_X      22
#define BIRD_SIZE   16
#define HIT_INSET   3           /* forgiving hitbox */
#define GROUND_Y    60

#define PIPE_W      12
#define PIPE_GAP    30
#define PIPE_SPACING 52
#define NPIPES      3

#define REWARD_PER_POINT 4

#define C_SKY    RGB(120, 190, 255)
#define C_GROUND RGB(200, 160, 90)
#define C_GRASS  RGB(70, 180, 80)

enum fstate { F_READY, F_RUN, F_DEAD };

static const char *const icon[8] = {
    "GG....GG",
    "GG.##.GG",
    "GG#yy#GG",
    "..#by#..",
    "..#yy#..",
    "GG.##.GG",
    "GG....GG",
    "GG....GG",
};

static struct {
    enum fstate state;
    int y, vy;                  /* bird, fixed point */
    int pipe_x[NPIPES], pipe_gap_y[NPIPES];
    int scored[NPIPES];
    unsigned score, best, session_best;
    unsigned step_t, state_t0, rng;
} f;

static int random_gap_y(void)
{
    /* gap top between 6 and GROUND_Y - PIPE_GAP - 6 */
    return 6 + game_rnd(&f.rng, GROUND_Y - PIPE_GAP - 12);
}

static void reset_round(unsigned now)
{
    f.state = F_READY;
    f.state_t0 = now;
    f.step_t = now;
    f.y = 24 * FP;
    f.vy = 0;
    f.score = 0;
    for (int i = 0; i < NPIPES; i++) {
        f.pipe_x[i] = LCD_W + 20 + i * PIPE_SPACING;
        f.pipe_gap_y[i] = random_gap_y();
        f.scored[i] = 0;
    }
}

static void start(unsigned now)
{
    f.rng ^= now * 2654435761u + 1;
    f.session_best = 0;
    reset_round(now);
}

static int collides(void)
{
    int by = f.y / FP;
    int top = by + HIT_INSET, bottom = by + BIRD_SIZE - 1 - HIT_INSET;
    int left = BIRD_X + HIT_INSET, right = BIRD_X + BIRD_SIZE - 1 - HIT_INSET;

    if (bottom >= GROUND_Y || top < 0)
        return 1;
    for (int i = 0; i < NPIPES; i++) {
        int px = f.pipe_x[i];
        if (right < px || left > px + PIPE_W - 1)
            continue;
        if (top < f.pipe_gap_y[i] || bottom > f.pipe_gap_y[i] + PIPE_GAP - 1)
            return 1;
    }
    return 0;
}

static void physics_step(void)
{
    f.vy += GRAVITY;
    if (f.vy > MAX_VY)
        f.vy = MAX_VY;
    f.y += f.vy;

    for (int i = 0; i < NPIPES; i++) {
        f.pipe_x[i]--;
        if (!f.scored[i] && f.pipe_x[i] + PIPE_W < BIRD_X) {
            f.scored[i] = 1;
            f.score++;
        }
        if (f.pipe_x[i] + PIPE_W < 0) { /* recycle behind the last pipe */
            int last = f.pipe_x[0];
            for (int j = 1; j < NPIPES; j++)
                if (f.pipe_x[j] > last)
                    last = f.pipe_x[j];
            f.pipe_x[i] = last + PIPE_SPACING;
            f.pipe_gap_y[i] = random_gap_y();
            f.scored[i] = 0;
        }
    }
}

static void die(unsigned now)
{
    f.state = F_DEAD;
    f.state_t0 = now;
    if (f.score > f.best)
        f.best = f.score;
    if (f.score > f.session_best)
        f.session_best = f.score;
}

static int update(unsigned now, unsigned pressed, unsigned held)
{
    (void)held;
    int flap = pressed & (BTN_PLAY | BTN_UP);

    switch (f.state) {
    case F_READY:
        if (pressed & BTN_BACK)
            return 1;
        if (flap) {
            f.state = F_RUN;
            f.step_t = now;
            f.vy = FLAP_VY;
        }
        break;
    case F_RUN:
        if (pressed & BTN_BACK) { /* leave mid-round; the score still counts */
            die(now);
            return 1;
        }
        if (flap)
            f.vy = FLAP_VY;
        while (now - f.step_t >= STEP_MS) {
            f.step_t += STEP_MS;
            physics_step();
            if (collides()) {
                die(now);
                break;
            }
        }
        break;
    case F_DEAD:
        switch (game_over_input(now, f.state_t0, pressed)) {
        case GO_EXIT:    return 1;
        case GO_RESTART: reset_round(now); break;
        case GO_NONE:    break;
        }
        break;
    }
    return 0;
}

static void draw_pipe(int x, int gap_y)
{
    u16 body = RGB(80, 190, 70), edge = RGB(30, 110, 40);

    fb_rect(x, 0, PIPE_W, gap_y, body);
    fb_rect(x, gap_y + PIPE_GAP, PIPE_W, GROUND_Y - gap_y - PIPE_GAP, body);
    /* lips and outline */
    fb_rect(x - 1, gap_y - 4, PIPE_W + 2, 4, edge);
    fb_rect(x - 1, gap_y + PIPE_GAP, PIPE_W + 2, 4, edge);
    fb_rect(x, 0, 1, gap_y, edge);
    fb_rect(x + PIPE_W - 1, 0, 1, gap_y, edge);
    fb_rect(x, gap_y + PIPE_GAP, 1, GROUND_Y - gap_y - PIPE_GAP, edge);
    fb_rect(x + PIPE_W - 1, gap_y + PIPE_GAP, 1, GROUND_Y - gap_y - PIPE_GAP, edge);
}

static void render(unsigned now)
{
    int by = f.y / FP;

    if (f.state == F_READY) /* gentle bob while waiting */
        by += ((now - f.state_t0) / 200) % 2 ? 1 : -1;

    fb_rect(0, 0, LCD_W, GROUND_Y, C_SKY);
    for (int i = 0; i < NPIPES; i++)
        draw_pipe(f.pipe_x[i], f.pipe_gap_y[i]);
    fb_rect(0, GROUND_Y, LCD_W, LCD_H - GROUND_Y, C_GROUND);
    fb_rect(0, GROUND_Y, LCD_W, 1, C_GRASS);

    game_draw_pet(BIRD_X, by, f.state == F_DEAD);
    /* wing flaps on the way up */
    fb_rect(BIRD_X - 1, f.vy < 0 ? by + 6 : by + 9, 3, 2, palette('#'));

    if (f.state == F_DEAD)
        game_draw_over(f.score, f.best);
    else
        game_draw_score(f.score);
}

static int reward(void) { return game_reward(f.session_best, REWARD_PER_POINT); }

const struct game flappy_game = {icon, start, update, render, reward};
