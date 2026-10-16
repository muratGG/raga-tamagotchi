/*
 * Screenshots on the host: the game draws every frame into fb[][] before
 * sending it to the panel, so compiling the same code for the build machine
 * and setting up a scene gives exactly what the Raga shows. Writes 3x scaled
 * PNGs into the directory given as the only argument (make screenshots).
 *
 * The device's main.c is included whole so the scenes can set its state;
 * the hardware-only functions it and the games reference are stubbed below
 * (lcd.c is linked but never called).
 */
#define main tama_main
#include "../src/main.c"
#undef main

#include <stdio.h>
#include <string.h>
#include <zlib.h>

/* ---- hardware stubs ----------------------------------------------------- */

void buttons_init(void) {}
unsigned buttons_read(void) { return 0; }
void clock_init(void) {}
unsigned clock_ms(void) { return 0; }
unsigned clock_us_raw(void) { return 12345; }
void power_off(void) {}
void battery_init(void) {}
unsigned battery_mv(void) { return 0; }
int battery_percent(unsigned mv) { (void)mv; return 82; }
int usb_power(void) { return 0; }
int battery_charging(void) { return 0; }
void charger_update(unsigned now) { (void)now; }

/* ---- PNG (zlib for the image data) -------------------------------------- */

#define SCALE 3
#define PNG_W (LCD_W * SCALE)
#define PNG_H (LCD_H * SCALE)

static void be32(unsigned char *p, unsigned long v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void chunk(FILE *f, const char *type, const unsigned char *data, size_t n)
{
    unsigned char hdr[8], tail[4];
    be32(hdr, n);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    fwrite(data, 1, n, f);
    be32(tail, crc32(crc32(0, hdr + 4, 4), data, n));
    fwrite(tail, 1, 4, f);
}

static void write_png(const char *path)
{
    static unsigned char raw[PNG_H * (1 + PNG_W * 3)];
    static unsigned char z[sizeof(raw) + sizeof(raw) / 1000 + 64];
    unsigned char ihdr[13] = {0};
    size_t n = 0;
    uLongf zn = sizeof(z);

    for (int y = 0; y < PNG_H; y++) {
        raw[n++] = 0; /* filter: none */
        for (int x = 0; x < PNG_W; x++) {
            u16 c = fb[y / SCALE][x / SCALE];
            raw[n++] = (c >> 11 & 0x1f) * 255 / 31;
            raw[n++] = (c >> 5 & 0x3f) * 255 / 63;
            raw[n++] = (c & 0x1f) * 255 / 31;
        }
    }
    if (compress2(z, &zn, raw, n, Z_BEST_COMPRESSION) != Z_OK) {
        fprintf(stderr, "%s: compression failed\n", path);
        return;
    }

    be32(ihdr, PNG_W);
    be32(ihdr + 4, PNG_H);
    ihdr[8] = 8; /* bit depth */
    ihdr[9] = 2; /* RGB */

    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return;
    }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    chunk(f, "IHDR", ihdr, sizeof(ihdr));
    chunk(f, "IDAT", z, zn);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    printf("%s\n", path);
}

/* ---- scenes ------------------------------------------------------------- */

static const char *outdir;

static void shot(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", outdir, name);
    write_png(path);
}

/* A pet mid-afternoon, needs as given, nothing animating. */
static void pet_scene(const struct species *s, int food, int fun, int clean, int energy)
{
    memset(&g, 0, sizeof(g));
    pet = s;
    g.stat[S_FOOD] = food;
    g.stat[S_FUN] = fun;
    g.stat[S_CLEAN] = clean;
    g.stat[S_ENERGY] = energy;
    g.state = ST_IDLE;
    g.clock_min = 15 * 60 + 42;
    g.x = PLAY_CX - PET_PX / 2;
    g.next_blink = ~0u;
}

/* ---- autopilots: look at the frame like a player would, press buttons ---- */

#define TICK_MS 10

static int same(int x, int y, u16 c) { return fb[y][x] == c; }

/* First pixel of colour c in the box, scanning rows top-down; 0 if none. */
static int find(u16 c, int x0, int y0, int x1, int y1, int *fx, int *fy)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (same(x, y, c)) {
                *fx = x;
                *fy = y;
                return 1;
            }
    return 0;
}

typedef void (*pilot_fn)(unsigned now, unsigned *pressed, unsigned *held);

static void play(const struct game *game, unsigned ms, unsigned start_button, pilot_fn pilot)
{
    game->start(0);
    game->update(1, start_button, 0); /* leave the ready screen */
    for (unsigned t = 2; t <= ms; t += TICK_MS) {
        unsigned pressed = 0, held = 0;
        game->render(t);
        pilot(t, &pressed, &held);
        game->update(t, pressed, held);
    }
    game->render(ms);
}

/* Flappy: flap when the pet nears the bottom of the gap it is heading for
 * (the pipe it is in, or the next one). */
static void flappy_pilot(unsigned now, unsigned *pressed, unsigned *held)
{
    static unsigned last_flap;
    const u16 sky = RGB(120, 190, 255);
    int bx, by, gap_bottom = 45;
    (void)held;

    if (!find(pet->body_color, 0, 0, 60, 59, &bx, &by))
        return;
    for (int x = 8; x < LCD_W - 6; x++) { /* pipes reach the top row */
        if (same(x, 0, sky))
            continue;
        /* the gap ends where the lower pipe starts; scan that pipe upwards
         * (from below the pet can't be in the way) */
        int y = 59;
        while (y > 0 && (same(x + 5, y, RGB(80, 190, 70)) || same(x + 5, y, RGB(30, 110, 40))))
            y--;
        gap_bottom = y + 1;
        break;
    }
    /* first body pixel is ~2 rows below the sprite top; hitbox bottom ~+10 */
    if (by + 10 > gap_bottom - 6 && now - last_flap > 200) {
        *pressed = BTN_PLAY;
        last_flap = now;
    }
}

/* Snake: head (pet colour) towards the apple; never a U-turn, never into a
 * wall or its own body if another way is free. */
static void snake_pilot(unsigned now, unsigned *pressed, unsigned *held)
{
    static const unsigned btn[4] = {BTN_UP, BTN_RIGHT, BTN_DOWN, BTN_LEFT};
    static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
    static int cur = 1; /* starts moving right */
    int hx, hy, ax, ay, want[8], n = 0;
    (void)now;
    (void)held;

    if (!find(pet->body_color, 0, 8, LCD_W - 1, LCD_H - 1, &hx, &hy) ||
        !find(palette('r'), 0, 8, LCD_W - 1, LCD_H - 1, &ax, &ay))
        return;
    hx /= 4; ax /= 4;
    hy = (hy - 8) / 4; ay = (ay - 8) / 4;

    if (ax > hx) want[n++] = 1;
    if (ax < hx) want[n++] = 3;
    if (ay > hy) want[n++] = 2;
    if (ay < hy) want[n++] = 0;
    for (int d = 0; d < 4; d++) /* then anything else */
        want[n++] = (cur + d) % 4;
    for (int i = 0; i < n; i++) {
        int d = want[i], nx = hx + dx[d], ny = hy + dy[d];
        if (d == (cur + 2) % 4 || nx < 0 || nx >= 32 || ny < 0 || ny >= 14)
            continue;
        if (same(nx * 4 + 1, 8 + ny * 4 + 1, RGB(110, 210, 90)))
            continue; /* own body */
        if (d != cur)
            *pressed = btn[d];
        cur = d;
        return;
    }
}

/* Catch: run under the lowest apple. */
static void catch_pilot(unsigned now, unsigned *pressed, unsigned *held)
{
    int px, py, ax = -1;
    (void)now;
    (void)pressed;

    if (!find(pet->body_color, 0, 46, LCD_W - 1, 61, &px, &py))
        return;
    for (int y = 45; y >= 0 && ax < 0; y--)
        for (int x = 0; x < LCD_W; x++)
            if (same(x, y, palette('r'))) {
                ax = x;
                break;
            }
    if (ax < 0)
        return;
    if (ax + 3 < px - 2) *held = BTN_LEFT;
    else if (ax + 3 > px + 8) *held = BTN_RIGHT;
}

/* Runner: jump when an obstacle gets close. */
static void runner_pilot(unsigned now, unsigned *pressed, unsigned *held)
{
    static unsigned last_jump;
    (void)held;

    for (int x = 30; x < 40; x++)
        if (same(x, 53, palette('G')) || same(x, 53, palette('s'))) {
            if (now - last_jump > 300) {
                *pressed = BTN_PLAY;
                last_jump = now;
            }
            return;
        }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTDIR\n", argv[0]);
        return 1;
    }
    outdir = argv[1];

    memset(&g, 0, sizeof(g));
    batt_mv = 4050; /* any reading: battery_percent() above decides the shot */
    g.state = ST_CHOOSE;
    pet = &species[3];
    render(0);
    shot("choose-pet");

    pet_scene(&species[0], 90, 85, 100, 80);
    render(0);
    shot("pet-happy");

    pet_scene(&species[1], 40, 25, 60, 70);
    g.npoop = 2;
    g.poop_x[0] = 12;
    g.poop_x[1] = 100;
    g.x = 40;
    render(0);
    shot("pet-sad-poop");

    pet_scene(&species[2], 50, 80, 90, 60);
    g.state = ST_EAT;
    g.state_t0 = 0;
    render(1000);
    shot("pet-eating");

    pet_scene(&species[4], 70, 75, 95, 40);
    g.state = ST_MENU;
    g.sel = ACT_FEED;
    render(0);
    shot("menu");

    pet_scene(&species[3], 80, 80, 90, 55);
    g.state = ST_NIGHT;
    g.clock_min = 23 * 60 + 5;
    render(900);
    shot("night");

    pet_scene(&species[0], 80, 60, 90, 70);
    g.state = ST_PICK;
    g.pick = 1;
    render(0);
    shot("games-menu");

    pet = &species[0];
    play(&flappy_game, 9000, BTN_PLAY, flappy_pilot);
    shot("game-flappy");

    pet = &species[1];
    play(&snake_game, 9000, BTN_RIGHT, snake_pilot);
    shot("game-snake");

    pet = &species[2];
    play(&catch_game, 9000, BTN_PLAY, catch_pilot);
    shot("game-catch");

    pet = &species[3];
    play(&runner_game, 9000, BTN_PLAY, runner_pilot);
    shot("game-runner");
    return 0;
}
