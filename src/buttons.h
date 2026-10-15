#ifndef RAGA_BUTTONS_H
#define RAGA_BUTTONS_H

enum {
    BTN_UP    = 1 << 0,
    BTN_DOWN  = 1 << 1,
    BTN_LEFT  = 1 << 2,
    BTN_RIGHT = 1 << 3,
    BTN_PLAY  = 1 << 4,
    BTN_MENU  = 1 << 5,
    BTN_BACK  = 1 << 6,
    BTN_POWER = 1 << 7,
};

void buttons_init(void);
/* Debounced state: bitmask of the buttons held right now. */
unsigned buttons_read(void);

#endif
