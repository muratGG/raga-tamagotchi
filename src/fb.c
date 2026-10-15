#include "fb.h"

#define BAND_H 8 /* dirty tracking granularity: one window per 8-row band */

u16 fb[LCD_H][LCD_W];
static u16 shown[LCD_H][LCD_W];

void fb_invalidate(void)
{
    for (int y = 0; y < LCD_H; y++)
        for (int x = 0; x < LCD_W; x++)
            shown[y][x] = ~fb[y][x];
}

static void present_band(int y0)
{
    int x0 = LCD_W, x1 = -1, ya = -1, yb = -1;

    for (int y = y0; y < y0 + BAND_H; y++)
        for (int x = 0; x < LCD_W; x++)
            if (fb[y][x] != shown[y][x]) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (ya < 0) ya = y;
                yb = y;
            }
    if (x1 < 0)
        return;

    lcd_window(x0, ya, x1, yb);
    int fast = lcd_stream_begin((x1 - x0 + 1) * (yb - ya + 1));
    for (int y = ya; y <= yb; y++)
        for (int x = x0; x <= x1; x++) {
            if (fast)
                lcd_stream_pixel(fb[y][x]);
            else
                lcd_pixel_out(fb[y][x]);
            shown[y][x] = fb[y][x];
        }
    if (fast)
        lcd_stream_end();
}

void fb_present(void)
{
    for (int y = 0; y < LCD_H; y += BAND_H)
        present_band(y);
}

void fb_set(int x, int y, u16 c)
{
    if (x >= 0 && x < LCD_W && y >= 0 && y < LCD_H)
        fb[y][x] = c;
}

void fb_rect(int x, int y, int w, int h, u16 c)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            fb_set(i, j, c);
}

int fb_number_width(unsigned v, int scale)
{
    int digits = 1;
    while (v >= 10) {
        v /= 10;
        digits++;
    }
    return digits * 4 * scale - scale;
}

static void fb_digit(int x, int y, unsigned d, int scale, u16 c)
{
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 3; col++)
            if ((font3x5[d][row] >> (2 - col)) & 1)
                fb_rect(x + col * scale, y + row * scale, scale, scale, c);
}

void fb_number(int x, int y, unsigned v, int scale, u16 c)
{
    int cx = x + fb_number_width(v, scale) - 3 * scale;
    do {
        fb_digit(cx, y, v % 10, scale, c);
        cx -= 4 * scale;
        v /= 10;
    } while (v);
}

void fb_sprite(int x, int y, const char *const *rows, int w, int h, int scale, fb_palette pal)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            u16 c = pal(rows[j][i]);
            if (c)
                fb_rect(x + i * scale, y + j * scale, scale, scale, c);
        }
}
