#ifndef RAGA_GAME_H
#define RAGA_GAME_H

/* A full-screen mini game launched from the pet's play menu. Time keeps
 * running for the pet while a game is open. */
struct game {
    const char *const *icon; /* 8x8 menu icon */
    void (*start)(unsigned now);
    /* Returns non-zero when the player leaves the game. */
    int (*update)(unsigned now, unsigned pressed, unsigned held);
    void (*render)(unsigned now);
    /* Happiness earned by the session that just ended. */
    int (*reward)(void);
};

extern const struct game flappy_game, snake_game, catch_game, runner_game;

#endif
