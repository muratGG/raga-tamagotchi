#ifndef RAGA_LCD_H
#define RAGA_LCD_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define REG32(addr) (*(volatile u32 *)(addr))

#define LCD_W 128
#define LCD_H 64

#define RGB(r, g, b) ((u16)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3)))

/* Philips' init stage (____.3) arms the RTC watchdog; the stock OS kicks it,
 * we don't, so it would reboot us into the NAND firmware. */
void watchdog_disable(void);

/* Panel must already be initialised by Philips' play0 stage. */
void lcd_enable(void);
void lcd_disable(void);
void lcd_window(int x0, int y0, int x1, int y1);
void lcd_pixel_out(u16 c);
void lcd_fill_rect(int x, int y, int w, int h, u16 c);

/* Fast path: stream pixels straight into the LCDIF FIFO after lcd_window().
 * lcd_stream_begin() returns 0 if the bus is not in 8-bit mode; callers
 * then fall back to lcd_pixel_out(). */
int lcd_stream_begin(unsigned npixels);
void lcd_stream_pixel(u16 c);
void lcd_stream_end(void);

/* 3x5 hex digits: 5 rows of 3 bits, MSB = leftmost pixel. */
extern const u8 font3x5[16][5];

/* 3x5 hex digit, drawn at scale 1 inside a 4x6 cell (cell includes spacing). */
void lcd_hex_digit(int x, int y, unsigned d, u16 fg, u16 bg);
void lcd_hex(int x, int y, u32 v, int digits, u16 fg, u16 bg);

/* Same glyphs scaled up: a digit occupies a (4*scale)x(6*scale) cell. */
void lcd_digit_scaled(int x, int y, unsigned d, int scale, u16 fg, u16 bg);
void lcd_dec(int x, int y, u32 v, int digits, int scale, u16 fg, u16 bg);

#endif
