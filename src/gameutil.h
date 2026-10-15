#ifndef RAGA_GAMEUTIL_H
#define RAGA_GAMEUTIL_H

/* Pieces every mini game shares: random numbers, the score at the top, the
 * game-over panel and its buttons, the pet drawn at 16x16, the reward. */

unsigned game_rnd(unsigned *seed, unsigned n);

void game_draw_score(unsigned score);
void game_draw_over(unsigned score, unsigned best);

enum game_over_choice { GO_NONE, GO_RESTART, GO_EXIT };
/* BACK leaves at once; PLAY restarts, ignored for a moment so a late press
 * of the game's own button does not restart instantly. */
enum game_over_choice game_over_input(unsigned now, unsigned over_t0, unsigned pressed);

/* The chosen pet at scale 1, facing right; dead: x eyes. */
void game_draw_pet(int x, int y, int dead);

/* Happiness for a session: playing at all counts, the best round adds more. */
int game_reward(unsigned session_best, int per_point);

#endif
