#include "lcd.h"

/* ARM-mode functions inside ragaplay.0.elf (still resident in RAM).
 * Even addresses: calling them with the Thumb bit set crashes the CPU. */
typedef void (*lcd_write_fn)(int is_data, unsigned value);
typedef void (*lcd_enable_fn)(int on);
#define LCD_WRITE  ((lcd_write_fn)0x1f3c)  /* FUN_00001f3c */
#define LCD_ENABLE ((lcd_enable_fn)0x1df0) /* FUN_00001df0: 0x29 + backlight */

#define ROW_OFFSET 0x20 /* panel rows 0x20..0x5f are visible */

#define HW_RTC_CTRL_CLR 0x8005c008
#define RTC_CTRL_WATCHDOGEN 0x10

void watchdog_disable(void) { REG32(HW_RTC_CTRL_CLR) = RTC_CTRL_WATCHDOGEN; }

static void cmd(u8 c) { LCD_WRITE(0, c); }
static void dat(u8 d) { LCD_WRITE(1, d); }

void lcd_enable(void) { LCD_ENABLE(1); }
void lcd_disable(void) { LCD_ENABLE(0); } /* backlight off, then 0x28 */

void lcd_window(int x0, int y0, int x1, int y1)
{
    cmd(0x2a); dat(x0); dat(x1);
    cmd(0x2b); dat(ROW_OFFSET + y0); dat(ROW_OFFSET + y1);
    cmd(0x2c);
}

void lcd_pixel_out(u16 c) { dat(c >> 8); dat(c & 0xff); }

/* LCDIF (same sequence as Philips' FUN_00002afe, without its per-call overhead) */
#define HW_LCDIF_CTRL     0x80030000
#define HW_LCDIF_CTRL_SET 0x80030004
#define HW_LCDIF_CTRL_CLR 0x80030008
#define HW_LCDIF_DATA     0x800300b0
#define HW_LCDIF_STAT     0x800300c0
#define LCDIF_CTRL_COUNT        0xffff
#define LCDIF_CTRL_RUN          (1u << 16)
#define LCDIF_CTRL_WORD_LENGTH  (1u << 17) /* set: 8-bit bus */
#define LCDIF_CTRL_DATA_SELECT  (1u << 18) /* set: data, clear: command */
#define LCDIF_CTRL_DATA_SWIZZLE (3u << 21)
#define LCDIF_STAT_TXFIFO_FULL  (1u << 27)

static u32 stream_word;
static int stream_bytes;

static void lcdif_wait_idle(void)
{
    while (REG32(HW_LCDIF_CTRL) & LCDIF_CTRL_RUN)
        ;
}

int lcd_stream_begin(unsigned npixels)
{
    unsigned nbytes = npixels * 2;

    if (!(REG32(HW_LCDIF_CTRL) & LCDIF_CTRL_WORD_LENGTH) || nbytes > LCDIF_CTRL_COUNT)
        return 0;
    lcdif_wait_idle();
    /* No swizzle: the first byte on the bus is bits 7:0 of each FIFO word.
     * Philips' byte writer reads the swizzle back, so it keeps working. */
    REG32(HW_LCDIF_CTRL_CLR) = LCDIF_CTRL_DATA_SWIZZLE | LCDIF_CTRL_DATA_SELECT |
                               LCDIF_CTRL_RUN | LCDIF_CTRL_COUNT;
    REG32(HW_LCDIF_CTRL_SET) = LCDIF_CTRL_RUN | LCDIF_CTRL_DATA_SELECT | nbytes;
    stream_word = 0;
    stream_bytes = 0;
    return 1;
}

static void stream_byte(u8 b)
{
    stream_word |= (u32)b << (8 * stream_bytes);
    if (++stream_bytes == 4) {
        while (REG32(HW_LCDIF_STAT) & LCDIF_STAT_TXFIFO_FULL)
            ;
        REG32(HW_LCDIF_DATA) = stream_word;
        stream_word = 0;
        stream_bytes = 0;
    }
}

void lcd_stream_pixel(u16 c)
{
    stream_byte(c >> 8);
    stream_byte(c & 0xff);
}

void lcd_stream_end(void)
{
    if (stream_bytes) {
        while (REG32(HW_LCDIF_STAT) & LCDIF_STAT_TXFIFO_FULL)
            ;
        REG32(HW_LCDIF_DATA) = stream_word;
    }
    lcdif_wait_idle();
}

void lcd_fill_rect(int x, int y, int w, int h, u16 c)
{
    lcd_window(x, y, x + w - 1, y + h - 1);
    for (int i = 0; i < w * h; i++)
        lcd_pixel_out(c);
}

/* Each glyph: 5 rows of 3 bits, MSB = leftmost pixel. */
const u8 font3x5[16][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
    {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1},
    {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {7, 5, 7, 5, 5}, {6, 5, 6, 5, 6},
    {7, 4, 4, 4, 7}, {6, 5, 5, 5, 6}, {7, 4, 7, 4, 7}, {7, 4, 7, 4, 4},
};

void lcd_digit_scaled(int x, int y, unsigned d, int scale, u16 fg, u16 bg)
{
    lcd_window(x, y, x + 4 * scale - 1, y + 6 * scale - 1);
    for (int py = 0; py < 6 * scale; py++)
        for (int px = 0; px < 4 * scale; px++) {
            int row = py / scale, col = px / scale;
            int on = row < 5 && col < 3 && (font3x5[d & 15][row] >> (2 - col)) & 1;
            lcd_pixel_out(on ? fg : bg);
        }
}

void lcd_hex_digit(int x, int y, unsigned d, u16 fg, u16 bg)
{
    lcd_digit_scaled(x, y, d, 1, fg, bg);
}

void lcd_dec(int x, int y, u32 v, int digits, int scale, u16 fg, u16 bg)
{
    for (int i = digits - 1; i >= 0; i--) {
        lcd_digit_scaled(x + i * 4 * scale, y, v % 10, scale, fg, bg);
        v /= 10;
    }
}

void lcd_hex(int x, int y, u32 v, int digits, u16 fg, u16 bg)
{
    for (int i = 0; i < digits; i++)
        lcd_hex_digit(x + i * 4, y, v >> ((digits - 1 - i) * 4), fg, bg);
}
