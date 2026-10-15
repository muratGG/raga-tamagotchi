#ifndef RAGA_FB_H
#define RAGA_FB_H

#include "lcd.h"

/* Off-screen frame. Draw the whole scene here every tick, then fb_present()
 * sends only the parts that differ from what the panel already shows. */
extern u16 fb[LCD_H][LCD_W];

void fb_invalidate(void); /* next present redraws everything */
void fb_present(void);

void fb_rect(int x, int y, int w, int h, u16 c);
/* Sprite rows are strings; palette maps a char to a colour, 0 = transparent. */
typedef u16 (*fb_palette)(char ch);
void fb_sprite(int x, int y, const char *const *rows, int w, int h, int scale, fb_palette pal);
void fb_set(int x, int y, u16 c);

/* Decimal number in the 3x5 font, transparent background; returns its width. */
int fb_number_width(unsigned v, int scale);
void fb_number(int x, int y, unsigned v, int scale, u16 c);

#endif
