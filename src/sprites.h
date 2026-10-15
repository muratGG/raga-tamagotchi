#ifndef RAGA_SPRITES_H
#define RAGA_SPRITES_H

#include "lcd.h"

/* Shared by the pet screen and the games. Sprites are rows of palette
 * chars; '.' is transparent. */

#define C_SICK RGB(180, 210, 130)

/* A pet species. All bodies are 16x16 and share one face layout (eyes at
 * columns 4-5 and 10-11, rows 5-6; mouth rows 9-10, columns 6-9), so faces
 * and animations work for every species. Palette: 'o' body, 'a' accent,
 * 'l' belly. */
struct species {
    const char *const *body;
    u16 body_color, accent_color, belly_color;
};

extern const struct species species[];
extern const int nspecies;
extern const struct species *pet; /* the one chosen at boot */

extern const char *const apple[8]; /* 8x8 */
extern const char *const ball[8];
extern const char *const heart[7]; /* 7x7 */

/* Colours the palette uses for 'o', 'a' and 'l'. pet_colors() sets them
 * for a species (sick: a pale body). */
extern u16 body_color, accent_color, belly_color;
void pet_colors(const struct species *s, int sick);

u16 palette(char ch);

#endif
