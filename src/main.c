/*
 * Raga Tamagotchi.
 *
 * Four needs, shown as vertical bars in the left sidebar:
 *   fullness (apple), happiness (heart), cleanliness (drop), energy (moon).
 * Rhythm follows the original 1997 Tamagotchi (adult stage): a heart of
 * hunger/happiness every ~75 min, a poop every ~3 h (up to 4), a meal or a
 * game worth one heart (25 points), and a fixed night's sleep during which
 * nothing drains. Cleanliness only drops while poop lies around. Energy is
 * our addition: it runs down over the day and refills at night or in a nap.
 *
 *   boot          choose a pet (LEFT/RIGHT, PLAY), then set the clock:
 *                 UP/DOWN change, LEFT/RIGHT field, PLAY ok
 *   MENU          open the action bar: feed / play / clean / nap
 *   LEFT / RIGHT  choose an action, PLAY confirms, BACK or MENU closes
 *                 (play opens a second bar: quick ball game or a mini game)
 *   PLAY (idle)   pet the pet: a little happiness
 *   any button    wakes a napping pet (not the night's sleep)
 *
 * The screen switches off after a minute without input; any button wakes it.
 * Holding the power slider for 2 s switches the device off. Holding BACK
 * while switching on starts the original music player instead.
 */
#include "buttons.h"
#include "clock.h"
#include "fb.h"
#include "game.h"
#include "power.h"
#include "sprites.h"

/* ---- tuning ------------------------------------------------------------ */

/* Raise to test: 60 makes an hour pass in a minute. */
#define TIME_SCALE 1
#define MINUTES(m) ((unsigned)(m) * 60000u / TIME_SCALE)

#define STAT_MAX 100

enum stat { S_FOOD, S_FUN, S_CLEAN, S_ENERGY, S_COUNT };

#define HEART (STAT_MAX / 4) /* the original's meters have 4 hearts */

/* Milliseconds per point lost while awake. Food and fun: one heart per
 * 75 min (full to empty in 5 h). Clean: a poop empties it in 3 h, more poops
 * proportionally faster; no poop, no loss. Energy: 14 h, about one day. */
static const unsigned decay_ms[S_COUNT] = {
    MINUTES(75) / HEART,
    MINUTES(75) / HEART,
    MINUTES(3 * 60) / STAT_MAX,
    MINUTES(14 * 60) / STAT_MAX,
};

#define FEED_AMOUNT    HEART
#define PLAY_AMOUNT    HEART
#define PLAY_FOOD_COST 5
#define PLAY_ENERGY_COST 10
#define PET_AMOUNT     3
#define NAP_GAIN_MS    (MINUTES(60) / STAT_MAX)      /* empty to full in a 1 h nap */
#define NIGHT_GAIN_MS  (MINUTES(6 * 60) / STAT_MAX)  /* full well before morning */
#define MAX_POOP       4
#define POOP_MS        MINUTES(150)                  /* every 2.5-3.5 h while awake */
#define POOP_JITTER_MS MINUTES(60)

/* Night's sleep, minutes after midnight (original Tamatchi: 9 PM - 9 AM). */
#define BEDTIME (21 * 60)
#define WAKEUP  (9 * 60)

/* ---- layout ------------------------------------------------------------ */

/* One status row at the top: icon + small bar per stat, then sun/moon and
 * the clock on the right. Everything below it is the play area. */
#define HUD_H    8
#define STAT_W   24                        /* per stat: icon, gap, bar */
#define BAR_W    14
#define BAR_H    4
#define SUN_X    (S_COUNT * STAT_W + 2)
#define TIME_X   (LCD_W - 19)
#define PLAY_X   0                         /* play area: x PLAY_X..LCD_W-1 */
#define PLAY_CX  ((PLAY_X + LCD_W) / 2)
#define GROUND_Y 56
#define PET_S    2               /* sprite scale */
#define PET_PX   (16 * PET_S)
#define PET_Y    (GROUND_Y - 30)  /* feet (sprite row 14) touch the grass */
#define POOP_Y   (GROUND_Y + 1)
#define MENU_Y   54
#define HOP_PX   8

/* ---- timings ----------------------------------------------------------- */

#define EAT_MS    1800
#define PLAY_MS   2400
#define CLEAN_MS  1500
#define REFUSE_MS 800
#define LOVE_MS   700
#define WANDER_MS 120
#define BLINK_MS  150

enum state {
    ST_CHOOSE, ST_SETCLOCK, ST_IDLE, ST_MENU, ST_PICK, ST_EAT, ST_PLAY, ST_GAME, ST_CLEAN,
    ST_NAP, ST_NIGHT, ST_REFUSE,
};
enum action { ACT_FEED, ACT_PLAY, ACT_CLEAN, ACT_NAP, ACT_COUNT };
enum mood { MOOD_HAPPY, MOOD_OK, MOOD_SAD, MOOD_SICK };

/* ---- sprites ----------------------------------------------------------- */

/* 8x8 menu / scene icons (pet body, apple, ball, heart: sprites.c) */
static const char *const drop[8] = {
    "...cc...",
    "..cccc..",
    ".cccccc.",
    "cccccccc",
    "ccwccccc",
    "cwcccccc",
    ".cccccc.",
    "..cccc..",
};

static const char *const moon[8] = {
    "..yyyy..",
    ".yyy....",
    "yyy.....",
    "yyy.....",
    "yyy.....",
    "yyy.....",
    ".yyy....",
    "..yyyy..",
};

static const char *const sun[8] = {
    "..yyyy..",
    ".yyyyyy.",
    "yyyyyyyy",
    "yyyyyyyy",
    "yyyyyyyy",
    "yyyyyyyy",
    ".yyyyyy.",
    "..yyyy..",
};

static const char *const poop[6] = {
    "..n...",
    "..nn..",
    ".nnnn.",
    ".nnnn.",
    "nnnnnn",
    "nnnnnn",
};

static const char *const zzz[5] = {
    "wwwww",
    "...w.",
    "..w..",
    ".w...",
    "wwwww",
};

static const char *const bubble[4] = {
    ".ww.",
    "w..w",
    "w..w",
    ".ww.",
};

/* 5x5 sidebar icons, one per stat */
static const char *const icon5[S_COUNT][5] = {
    {"..n..", ".rrr.", "rrrrr", "rrrrr", ".rrr."},  /* food */
    {"rr.rr", "rrrrr", "rrrrr", ".rrr.", "..r.."},  /* fun */
    {"..c..", ".ccc.", "ccccc", "ccccc", ".ccc."},  /* clean */
    {".yyy.", "yy...", "yy...", "yy...", ".yyy."},  /* energy */
};

static const char *const *const action_icon[ACT_COUNT] = {apple, ball, drop, moon};

static const struct game *const games[] = {&flappy_game, &snake_game, &catch_game, &runner_game};
#define NGAMES ((int)(sizeof(games) / sizeof(games[0])))
#define NPICK  (1 + NGAMES)

#define C_HUD    RGB(30, 30, 44)
#define C_MENU   RGB(30, 30, 44)
#define C_SELECT RGB(255, 255, 255)
static const struct { u16 sky, grass; } day = {RGB(120, 190, 255), RGB(70, 180, 80)},
                                       night = {RGB(20, 24, 60), RGB(20, 70, 40)};

/* ---- game state -------------------------------------------------------- */

static struct {
    int stat[S_COUNT];
    unsigned stat_tick[S_COUNT];    /* last decay time */
    enum state state;
    unsigned state_t0;
    enum action sel;
    int pick;                       /* play menu: 0 = ball, 1.. = games[] */
    const struct game *game;
    int x, dir;
    int poop_x[MAX_POOP], npoop;
    unsigned next_poop;
    unsigned sleep_tick;
    unsigned wander_t, blink_until, next_blink, love_until;
    unsigned rng;
    int clock_min;                  /* minutes after midnight at clock_ms() == clock_t0 */
    unsigned clock_t0;
    int clock_field;                /* clock screen: 0 hours, 1 minutes */
} g;

/* Minutes after midnight, from the time set at boot plus elapsed time. */
static int time_of_day(unsigned now)
{
    return (g.clock_min + (now - g.clock_t0) / MINUTES(1)) % (24 * 60);
}

static int is_night(unsigned now)
{
    int t = time_of_day(now);
    return BEDTIME > WAKEUP ? (t >= BEDTIME || t < WAKEUP) : (t >= BEDTIME && t < WAKEUP);
}

/* 0..n-1 for any n: two LCG steps give 32 usable bits (the high halves),
 * enough for millisecond ranges spanning hours. */
static unsigned rnd(unsigned n)
{
    g.rng = g.rng * 1103515245u + 12345u;
    unsigned hi = g.rng >> 16;
    g.rng = g.rng * 1103515245u + 12345u;
    return ((hi << 16) | (g.rng >> 16)) % n;
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void add_stat(enum stat s, int delta) { g.stat[s] = clamp(g.stat[s] + delta, 0, STAT_MAX); }

static void set_state(enum state s, unsigned now)
{
    g.state = s;
    g.state_t0 = now;
}

static enum mood mood(void)
{
    if (g.stat[S_FOOD] == 0 || g.stat[S_CLEAN] == 0)
        return MOOD_SICK;
    if (g.stat[S_FOOD] < 30 || g.stat[S_FUN] < 30 || g.stat[S_CLEAN] < 30 || g.stat[S_ENERGY] < 20)
        return MOOD_SAD;
    if (g.stat[S_FOOD] > 70 && g.stat[S_FUN] > 70 && g.stat[S_CLEAN] > 70)
        return MOOD_HAPPY;
    return MOOD_OK;
}

/* ---- drawing ----------------------------------------------------------- */

/* One sprite-pixel of the pet's face, in 16x16 sprite coordinates. */
static void face_px(int px, int py, int col, int row, char ch)
{
    fb_rect(px + col * PET_S, py + row * PET_S, PET_S, PET_S, palette(ch));
}

static void draw_eye(int px, int py, int col, enum mood m, int closed)
{
    if (closed) {
        face_px(px, py, col, 6, 'b');
        face_px(px, py, col + 1, 6, 'b');
        return;
    }
    if (m == MOOD_SICK) { /* x eyes */
        face_px(px, py, col, 5, 'b');
        face_px(px, py, col + 1, 6, 'b');
        face_px(px, py, col + 1, 5, 'b');
        face_px(px, py, col, 6, 'b');
        return;
    }
    face_px(px, py, col, 5, 'w');
    face_px(px, py, col + 1, 5, 'b');
    face_px(px, py, col, 6, 'b');
    face_px(px, py, col + 1, 6, 'b');
}

static void draw_pet(int px, int py, enum mood m, int closed, int mouth_open)
{
    pet_colors(pet, m == MOOD_SICK);
    fb_sprite(px, py, pet->body, 16, 16, PET_S, palette);

    draw_eye(px, py, 4, m, closed);
    draw_eye(px, py, 10, m, closed);

    if (mouth_open) {
        face_px(px, py, 7, 9, 'm');
        face_px(px, py, 8, 9, 'm');
        face_px(px, py, 7, 10, 'm');
        face_px(px, py, 8, 10, 'm');
    } else if (m == MOOD_HAPPY) {
        face_px(px, py, 6, 9, '#');
        face_px(px, py, 9, 9, '#');
        face_px(px, py, 7, 10, '#');
        face_px(px, py, 8, 10, '#');
    } else if (m == MOOD_OK) {
        face_px(px, py, 7, 10, '#');
        face_px(px, py, 8, 10, '#');
    } else {
        face_px(px, py, 7, 9, '#');
        face_px(px, py, 8, 9, '#');
        face_px(px, py, 6, 10, '#');
        face_px(px, py, 9, 10, '#');
    }
    if (m == MOOD_SAD && !closed)
        face_px(px, py, 4, 7, 'c'); /* tear */
}

/* Small bar beside its icon, filling from the left. */
static void draw_bar(int x, int y, int value)
{
    u16 c = value > 60 ? RGB(80, 220, 90) : value > 30 ? RGB(255, 200, 40) : RGB(240, 60, 60);
    int w = (value * BAR_W + STAT_MAX - 1) / STAT_MAX; /* any non-zero value shows */
    fb_rect(x, y, BAR_W, BAR_H, RGB(70, 70, 90));
    fb_rect(x, y, w, BAR_H, c);
}

/* HH:MM, two digits per field; returns the width. */
static int draw_time(int x, int y, int minutes, int scale, u16 c)
{
    int h = minutes / 60, m = minutes % 60, cell = 4 * scale;

    fb_number(x, y, h / 10, scale, c);
    fb_number(x + cell, y, h % 10, scale, c);
    fb_rect(x + 2 * cell, y + scale, scale, scale, c);
    fb_rect(x + 2 * cell, y + 3 * scale, scale, scale, c);
    fb_number(x + 2 * cell + 2 * scale, y, m / 10, scale, c);
    fb_number(x + 3 * cell + 2 * scale, y, m % 10, scale, c);
    return 4 * cell + scale;
}

static void draw_hud(int dark, unsigned now)
{
    fb_rect(0, 0, LCD_W, HUD_H, C_HUD);
    for (int i = 0; i < S_COUNT; i++) {
        int x = i * STAT_W;
        fb_sprite(x + 1, (HUD_H - 5) / 2, icon5[i], 5, 5, 1, palette);
        draw_bar(x + 8, (HUD_H - BAR_H) / 2, g.stat[i]);
    }
    fb_sprite(SUN_X, 0, dark ? moon : sun, 8, 8, 1, palette);
    draw_time(TIME_X, (HUD_H - 5) / 2, time_of_day(now), 1, RGB(255, 255, 255));
}

static void draw_scene(int dark)
{
    u16 sky = dark ? night.sky : day.sky;
    u16 grass = dark ? night.grass : day.grass;

    fb_rect(PLAY_X, HUD_H, LCD_W - PLAY_X, GROUND_Y - HUD_H, sky);
    fb_rect(PLAY_X, GROUND_Y, LCD_W - PLAY_X, LCD_H - GROUND_Y, grass);
    for (int i = 0; i < g.npoop; i++)
        fb_sprite(g.poop_x[i], POOP_Y, poop, 6, 6, 1, palette);
}

/* Bottom bar of 8x8 icons with the selected one framed. */
static void draw_icon_bar(const char *const *const *icons, int n, int sel)
{
    int slot = (LCD_W - PLAY_X) / n;

    fb_rect(PLAY_X, MENU_Y, LCD_W - PLAY_X, LCD_H - MENU_Y, C_MENU);
    for (int i = 0; i < n; i++) {
        int x = PLAY_X + i * slot + (slot - 8) / 2;
        if (sel == i)
            fb_rect(x - 2, MENU_Y, 12, 10, C_SELECT);
        fb_rect(x - 1, MENU_Y + 1, 10, 8, C_MENU);
        fb_sprite(x, MENU_Y + 1, icons[i], 8, 8, 1, palette);
    }
}

static void draw_pick_menu(void)
{
    const char *const *icons[NPICK];

    icons[0] = ball;
    for (int i = 0; i < NGAMES; i++)
        icons[i + 1] = games[i]->icon;
    draw_icon_bar(icons, NPICK, g.pick);
}

/* Beside the pet, on whichever side has room. */
static int side_x(int px)
{
    return px + PET_PX + 9 > LCD_W ? px - 9 : px + PET_PX + 1;
}

/* Apple next to the pet, losing a bite every third of the meal. */
static void draw_meal(int px, unsigned t)
{
    int bites = t * 3 / EAT_MS;
    int ax = side_x(px);
    fb_sprite(ax, PET_Y + 20, apple, 8, 8, 1, palette);
    if (bites > 0)
        fb_rect(ax + 4, PET_Y + 20, 4, 3 * bites, day.sky); /* bitten off */
}

/* Height of a hop at time t within a hop of period ms (parabola). */
static int hop(unsigned t, unsigned period)
{
    int p = t % period;
    int half = period / 2;
    int d = p - half;
    return HOP_PX - HOP_PX * d * d / (half * half);
}

/* Bubbles rising around the pet and over the poops while cleaning. */
static void draw_bubbles(int px, unsigned t)
{
    for (int i = 0; i < 6; i++) {
        int bx = px - 4 + (i * 7) % (PET_PX + 8);
        int by = GROUND_Y - 6 - (int)((t / 40 + i * 9) % 36);
        fb_sprite(bx, by, bubble, 4, 4, 1, palette);
    }
    for (int i = 0; i < g.npoop; i++)
        fb_sprite(g.poop_x[i] + 1, POOP_Y - 4 - (int)((t / 60 + i * 5) % 8), bubble, 4, 4, 1, palette);
}

/* Z's drifting up from the sleeping pet. */
static void draw_zzz(int px, unsigned t)
{
    for (int i = 0; i < 2; i++) {
        int phase = (t / 80 + i * 12) % 24;
        fb_sprite(px + PET_PX - 6 + phase / 4, PET_Y - phase, zzz, 5, 5, 1, palette);
    }
}

#define CLOCK_SCALE 4
#define CLOCK_W     (16 * CLOCK_SCALE + CLOCK_SCALE)
#define CLOCK_X     ((LCD_W - CLOCK_W) / 2)
#define CLOCK_Y     18

/* Pet choice: the current species large in the middle, arrows either side. */
static void draw_choose_screen(unsigned now)
{
    const u16 arrow = RGB(255, 255, 255);
    int bob = (now / 300) % 2;

    fb_rect(0, 0, LCD_W, LCD_H, day.sky);
    fb_rect(0, 54, LCD_W, LCD_H - 54, day.grass);
    pet_colors(pet, 0);
    fb_sprite((LCD_W - 48) / 2, 6 + bob, pet->body, 16, 16, 3, palette);
    for (int i = 0; i < 5; i++) { /* < and > */
        fb_rect(10 + i, 30 - i, 1, 2 * i + 1, arrow);
        fb_rect(LCD_W - 11 - i, 30 - i, 1, 2 * i + 1, arrow);
    }
    /* which one of how many: dots along the grass */
    for (int i = 0; i < nspecies; i++)
        fb_rect(LCD_W / 2 - nspecies * 3 + i * 6, 58, 3, 3,
                &species[i] == pet ? arrow : RGB(40, 110, 50));
}

static void draw_clock_screen(void)
{
    const u16 white = RGB(255, 255, 255);
    int field_x = CLOCK_X + g.clock_field * (8 * CLOCK_SCALE + 2 * CLOCK_SCALE);

    fb_rect(0, 0, LCD_W, LCD_H, night.sky);
    fb_sprite(4, 4, sun, 8, 8, 1, palette);
    fb_sprite(LCD_W - 12, 4, moon, 8, 8, 1, palette);
    draw_time(CLOCK_X, CLOCK_Y, g.clock_min, CLOCK_SCALE, white);
    fb_rect(field_x, CLOCK_Y + 6 * CLOCK_SCALE, 7 * CLOCK_SCALE, 2, RGB(255, 220, 60));
}

static void render(unsigned now)
{
    enum mood m = mood();
    unsigned t = now - g.state_t0;
    int px = g.x, py = PET_Y;
    int closed = now < g.blink_until;
    int mouth_open = 0;
    int asleep = g.state == ST_NAP || g.state == ST_NIGHT;

    if (g.state == ST_CHOOSE) {
        draw_choose_screen(now);
        return;
    }
    if (g.state == ST_SETCLOCK) {
        draw_clock_screen();
        return;
    }
    if (g.state == ST_GAME) {
        g.game->render(now);
        return;
    }

    draw_scene(g.state == ST_NIGHT);

    switch (g.state) {
    case ST_EAT:
        mouth_open = (t / 300) % 2;
        draw_meal(px, t);
        break;
    case ST_PLAY:
        m = MOOD_HAPPY;
        py -= hop(t, PLAY_MS / 4);
        fb_sprite(side_x(px), GROUND_Y - 8 - hop(t + PLAY_MS / 8, PLAY_MS / 4), ball, 8, 8, 1, palette);
        break;
    case ST_REFUSE:
        px += (t / 100) % 2 ? 2 : -2;
        if (m == MOOD_HAPPY)
            m = MOOD_OK;
        break;
    default:
        break;
    }
    if (asleep)
        closed = 1;

    if (now < g.love_until) {
        m = MOOD_HAPPY;
        closed = 1;
        fb_sprite(px + 12, PET_Y - 6, heart, 7, 7, 1, palette);
    }

    draw_pet(px, py, m, closed, mouth_open);

    if (g.state == ST_CLEAN)
        draw_bubbles(px, t);
    if (asleep)
        draw_zzz(px, t);

    draw_hud(g.state == ST_NIGHT, now); /* after the pet: rising Z's may reach it */

    if (g.state == ST_MENU)
        draw_icon_bar(action_icon, ACT_COUNT, g.sel);
    else if (g.state == ST_PICK)
        draw_pick_menu();
}

/* ---- logic ------------------------------------------------------------- */

static void decay(unsigned now)
{
    int night = g.state == ST_NIGHT, nap = g.state == ST_NAP;

    for (int s = 0; s < S_COUNT; s++) {
        unsigned interval = decay_ms[s];
        int frozen = (night && s != S_ENERGY)          /* night: no care needed */
                  || (s == S_ENERGY && (night || nap)) /* refilled by sleep instead */
                  || (s == S_CLEAN && g.npoop == 0);   /* only poop makes it dirty */
        if (frozen) {
            g.stat_tick[s] = now;
            continue;
        }
        if (nap && (s == S_FOOD || s == S_FUN))
            interval *= 2;
        if (s == S_CLEAN)
            interval /= g.npoop;
        while (now - g.stat_tick[s] >= interval) {
            g.stat_tick[s] += interval;
            add_stat(s, -1);
        }
    }
}

static void schedule_poop(unsigned now) { g.next_poop = now + POOP_MS + rnd(POOP_JITTER_MS); }

static void maybe_poop(unsigned now)
{
    if ((int)(now - g.next_poop) < 0)
        return;
    schedule_poop(now);
    if (g.npoop < MAX_POOP)
        g.poop_x[g.npoop++] = PLAY_X + 2 + rnd(LCD_W - PLAY_X - 10);
}

static void wake_up(unsigned now)
{
    g.next_blink = now + 1000;
    /* no poop at night: one that came due while asleep arrives within the hour */
    if ((int)(now - g.next_poop) >= 0)
        g.next_poop = now + rnd(POOP_JITTER_MS);
    set_state(ST_IDLE, now);
}

/* Bedtime catches the pet when it is free (not mid-meal or mid-game). */
static int can_fall_asleep(void)
{
    switch (g.state) {
    case ST_IDLE: case ST_MENU: case ST_PICK: case ST_NAP: case ST_REFUSE:
        return 1;
    default:
        return 0;
    }
}

#define REPEAT_DELAY_MS 400
#define REPEAT_MS       80

static void update_clock_screen(unsigned now, unsigned pressed, unsigned held)
{
    static unsigned repeat_at;
    int step = 0;

    if (pressed & (BTN_UP | BTN_DOWN)) {
        step = pressed & BTN_UP ? 1 : -1;
        repeat_at = now + REPEAT_DELAY_MS;
    } else if ((held & (BTN_UP | BTN_DOWN)) && (int)(now - repeat_at) >= 0) {
        step = held & BTN_UP ? 1 : -1;
        repeat_at = now + REPEAT_MS;
    }
    if (step) {
        int h = g.clock_min / 60, m = g.clock_min % 60;
        if (g.clock_field == 0)
            h = (h + step + 24) % 24;
        else
            m = (m + step + 60) % 60;
        g.clock_min = h * 60 + m;
    }
    if (pressed & (BTN_LEFT | BTN_RIGHT))
        g.clock_field ^= 1;

    if (pressed & BTN_PLAY) {
        g.clock_t0 = now;
        for (int s = 0; s < S_COUNT; s++)
            g.stat_tick[s] = now;
        schedule_poop(now);
        g.next_blink = now + 1500;
        set_state(ST_IDLE, now);
    }
}

static void wander(unsigned now)
{
    if (now - g.wander_t < WANDER_MS)
        return;
    g.wander_t = now;
    if (rnd(30) == 0)
        g.dir = (int)rnd(3) - 1; /* left, stop or right */
    g.x += g.dir;
    if (g.x <= PLAY_X || g.x >= LCD_W - PET_PX) {
        g.x = clamp(g.x, PLAY_X, LCD_W - PET_PX);
        g.dir = -g.dir;
    }
}

static void blink(unsigned now)
{
    if (now < g.next_blink)
        return;
    g.blink_until = now + BLINK_MS;
    g.next_blink = now + 2000 + rnd(3000);
}

static void confirm(unsigned now)
{
    int ok;

    switch (g.sel) {
    case ACT_FEED:  ok = g.stat[S_FOOD] < 95; break;
    case ACT_PLAY:  ok = g.stat[S_FOOD] >= 10 && g.stat[S_ENERGY] >= 15; break; /* hungry or tired */
    case ACT_CLEAN: ok = g.npoop > 0 || g.stat[S_CLEAN] < 95; break;
    default:        ok = g.stat[S_ENERGY] < 90; break;
    }
    if (!ok) {
        set_state(ST_REFUSE, now);
        return;
    }
    static const enum state next[ACT_COUNT] = {ST_EAT, ST_PICK, ST_CLEAN, ST_NAP};
    set_state(next[g.sel], now);
    if (g.sel == ACT_PLAY)
        g.pick = 0;
    if (g.sel == ACT_NAP)
        g.sleep_tick = now;
}

/* Tiring and hunger-making, like the quick ball game. */
static void play_costs(void)
{
    add_stat(S_FOOD, -PLAY_FOOD_COST);
    add_stat(S_ENERGY, -PLAY_ENERGY_COST);
}

static void update(unsigned now, unsigned pressed, unsigned held)
{
    unsigned t = now - g.state_t0;

    if (g.state == ST_CHOOSE) {
        int i = pet - species;
        if (pressed & BTN_LEFT)
            pet = &species[(i + nspecies - 1) % nspecies];
        if (pressed & BTN_RIGHT)
            pet = &species[(i + 1) % nspecies];
        if (pressed & BTN_PLAY)
            set_state(ST_SETCLOCK, now);
        return;
    }
    if (g.state == ST_SETCLOCK) {
        update_clock_screen(now, pressed, held);
        return;
    }

    decay(now);
    if (g.state != ST_NAP && g.state != ST_NIGHT) {
        blink(now);
        maybe_poop(now);
    }
    if (is_night(now) && g.state != ST_NIGHT && can_fall_asleep()) {
        g.sleep_tick = now;
        set_state(ST_NIGHT, now);
        return;
    }

    switch (g.state) {
    case ST_CHOOSE:
    case ST_SETCLOCK:
        break;
    case ST_IDLE:
        wander(now);
        if (pressed & BTN_MENU) {
            g.sel = ACT_FEED;
            set_state(ST_MENU, now);
        } else if (pressed & BTN_PLAY) {
            add_stat(S_FUN, PET_AMOUNT);
            g.love_until = now + LOVE_MS;
        }
        break;
    case ST_MENU:
        if (pressed & BTN_LEFT)
            g.sel = (g.sel + ACT_COUNT - 1) % ACT_COUNT;
        if (pressed & BTN_RIGHT)
            g.sel = (g.sel + 1) % ACT_COUNT;
        if (pressed & BTN_PLAY)
            confirm(now);
        else if (pressed & (BTN_BACK | BTN_MENU))
            set_state(ST_IDLE, now);
        break;
    case ST_PICK:
        if (pressed & BTN_LEFT)
            g.pick = (g.pick + NPICK - 1) % NPICK;
        if (pressed & BTN_RIGHT)
            g.pick = (g.pick + 1) % NPICK;
        if (pressed & BTN_PLAY) {
            if (g.pick == 0) {
                set_state(ST_PLAY, now);
            } else {
                g.game = games[g.pick - 1];
                g.game->start(now);
                set_state(ST_GAME, now);
            }
        } else if (pressed & (BTN_BACK | BTN_MENU)) {
            set_state(ST_MENU, now);
        }
        break;
    case ST_GAME:
        if (g.game->update(now, pressed, held)) {
            add_stat(S_FUN, g.game->reward());
            play_costs();
            set_state(ST_IDLE, now);
        }
        break;
    case ST_EAT:
        if (t >= EAT_MS) {
            add_stat(S_FOOD, FEED_AMOUNT);
            set_state(ST_IDLE, now);
        }
        break;
    case ST_PLAY:
        if (t >= PLAY_MS) {
            add_stat(S_FUN, PLAY_AMOUNT);
            play_costs();
            set_state(ST_IDLE, now);
        }
        break;
    case ST_CLEAN:
        if (t >= CLEAN_MS) {
            g.npoop = 0;
            g.stat[S_CLEAN] = STAT_MAX;
            set_state(ST_IDLE, now);
        }
        break;
    case ST_NAP:
        while (now - g.sleep_tick >= NAP_GAIN_MS) {
            g.sleep_tick += NAP_GAIN_MS;
            add_stat(S_ENERGY, 1);
        }
        if (pressed || g.stat[S_ENERGY] >= STAT_MAX)
            wake_up(now);
        break;
    case ST_NIGHT:
        while (now - g.sleep_tick >= NIGHT_GAIN_MS) {
            g.sleep_tick += NIGHT_GAIN_MS;
            add_stat(S_ENERGY, 1);
        }
        if (!is_night(now))
            wake_up(now);
        break;
    case ST_REFUSE:
        if (t >= REFUSE_MS)
            set_state(ST_IDLE, now);
        break;
    }
}

#define SCREEN_TIMEOUT_MS 60000 /* real time, not TIME_SCALE'd */
#define POWER_HOLD_MS     2000

/* Holding BACK while switching on boots the original music player instead:
 * returning hands control back to the bootloader, which loads it. */
static int wants_original_player(void)
{
    for (int i = 0; i < 4; i++) /* a few samples: the debouncer needs two */
        if (buttons_read() & BTN_BACK)
            return 1;
    return 0;
}

int main(void)
{
    unsigned prev = 0;
    int screen_on = 1, power_armed = 0;
    unsigned last_input = 0, power_since = 0;

    buttons_init();
    if (wants_original_player())
        return 0;

    watchdog_disable();
    clock_init();

    for (int s = 0; s < S_COUNT; s++) {
        g.stat[s] = 80;
        g.stat_tick[s] = 0;
    }
    g.state = ST_CHOOSE;
    pet = &species[0];
    g.state_t0 = 0;
    g.clock_min = 12 * 60;
    g.clock_t0 = 0;
    g.clock_field = 0;
    g.sel = ACT_FEED;
    g.x = PLAY_CX - PET_PX / 2;
    g.dir = 1;
    g.npoop = 0;
    g.sleep_tick = 0;
    g.wander_t = g.blink_until = g.love_until = 0;
    g.next_blink = 1500;
    g.rng = clock_us_raw();
    g.next_poop = 0; /* scheduled when the clock is set */

    render(0);
    fb_invalidate();
    fb_present();
    lcd_enable();

    for (;;) {
        unsigned held = buttons_read();
        unsigned now = clock_ms();
        unsigned pressed = held & ~prev;
        prev = held;

        /* Power slider: the hold that switched us on doesn't count; after a
         * release, holding it again for 2 s switches off. */
        if (!(held & BTN_POWER)) {
            power_armed = 1;
            power_since = now;
        } else if (power_armed && now - power_since >= POWER_HOLD_MS) {
            power_off();
        }

        if (held)
            last_input = now;
        if (screen_on && now - last_input >= SCREEN_TIMEOUT_MS) {
            lcd_disable();
            screen_on = 0;
        }
        if (!screen_on) {
            update(now, 0, 0); /* time goes on in the dark */
            if (!pressed)
                continue;
            /* the press that wakes the screen does nothing else */
            render(now);
            fb_present();
            lcd_enable();
            screen_on = 1;
            continue;
        }

        update(now, pressed, held);
        render(now);
        fb_present();
    }
}
