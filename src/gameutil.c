#include "gameutil.h"
#include "buttons.h"
#include "fb.h"
#include "sprites.h"

#define C_TEXT   RGB(255, 255, 255)
#define C_SHADOW RGB(30, 30, 44)
#define OVER_GRACE_MS 600
#define REWARD_BASE 5
#define REWARD_MAX  40

unsigned game_rnd(unsigned *seed, unsigned n)
{
    *seed = *seed * 1103515245u + 12345u;
    return (*seed >> 16) % n;
}

static void centered_number(unsigned v, int y, int scale)
{
    int x = (LCD_W - fb_number_width(v, scale)) / 2;
    fb_number(x + 1, y + 1, v, scale, C_SHADOW);
    fb_number(x, y, v, scale, C_TEXT);
}

void game_draw_score(unsigned score) { centered_number(score, 2, 2); }

void game_draw_over(unsigned score, unsigned best)
{
    fb_rect(34, 14, 60, 34, C_SHADOW);
    centered_number(score, 18, 3);
    fb_sprite(46, 38, heart, 7, 7, 1, palette); /* best */
    fb_number(56, 39, best, 1, C_TEXT);
}

enum game_over_choice game_over_input(unsigned now, unsigned over_t0, unsigned pressed)
{
    if (pressed & BTN_BACK)
        return GO_EXIT;
    if (now - over_t0 < OVER_GRACE_MS)
        return GO_NONE;
    if (pressed & BTN_PLAY)
        return GO_RESTART;
    return GO_NONE;
}

void game_draw_pet(int x, int y, int dead)
{
    u16 black = palette('b');

    pet_colors(pet, 0);
    fb_sprite(x, y, pet->body, 16, 16, 1, palette);
    if (dead) {
        fb_set(x + 4, y + 5, black);
        fb_set(x + 5, y + 6, black);
        fb_set(x + 10, y + 5, black);
        fb_set(x + 11, y + 6, black);
    } else {
        fb_rect(x + 4, y + 5, 2, 2, black);
        fb_rect(x + 10, y + 5, 2, 2, black);
        fb_set(x + 4, y + 5, palette('w'));
        fb_set(x + 10, y + 5, palette('w'));
    }
    fb_rect(x + 7, y + 10, 2, 1, palette('#'));
}

int game_reward(unsigned session_best, int per_point)
{
    int r = REWARD_BASE + (int)session_best * per_point;
    return r > REWARD_MAX ? REWARD_MAX : r;
}
