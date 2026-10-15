#include "sprites.h"

static const char *const chick[16] = {
    "......#.#.......",
    "......####......",
    "....##oooo##....",
    "...#oooooooo#...",
    "..#oooooooooo#..",
    "..#oooooooooo#..",
    ".#oooooooooooo#.",
    ".#oooooaaooooo#.",
    ".#ooppooooppoo#.",
    ".#oooooooooooo#.",
    "..#oooooooooo#..",
    "..#oooooooooo#..",
    "...#oooooooo#...",
    "....##oooo##....",
    "....aa....aa....",
    "................",
};

static const char *const cat[16] = {
    "..##........##..",
    "..#a#......#a#..",
    "..#oo######oo#..",
    "..#oooooooooo#..",
    "..#oooooooooo#..",
    "..#oooooooooo#..",
    ".#oooooooooooo#.",
    ".#oooooooooooo#.",
    ".#ooppooooppoo#.",
    ".#oooooooooooo#.",
    "..#oooooooooo#..",
    "..#oooooooooo##.",
    "...#oooooooo#.#.",
    "....##oooo##..#.",
    "....##....##....",
    "................",
};

static const char *const dog[16] = {
    "................",
    "......####......",
    "....##oooo##....",
    ".aa#oooooooo#aa.",
    "aa#oooooooooo#aa",
    "aa#oooooooooo#aa",
    "a#oooooooooooo#a",
    ".#oooooooooooo#.",
    ".#ooppobboppoo#.",
    ".#oooooooooooo#.",
    "..#oooooooooo#..",
    "..#oooooooooo#..",
    "...#oooooooo#...",
    "....##oooo##....",
    "....##....##....",
    "................",
};

static const char *const dragon[16] = {
    "....a......a....",
    ".....a####a.....",
    "....##oooo##....",
    "...#oooooooo#...",
    "..#oooooooooo#..",
    "..#oooooooooo#..",
    ".#oooooooooooo#.",
    "a#oooooooooooo#a",
    "a#ooppooooppoo#a",
    ".#oooooooooooo#.",
    "..#oooooooooo#..",
    "..#ooollllooo#..",
    "...#oollllooo#..",
    "....##oooo##..##",
    "....##....##....",
    "................",
};

static const char *const bunny[16] = {
    "....#a#..#a#....",
    "....#a#..#a#....",
    "....#a#oo#a#....",
    "...#oooooooo#...",
    "..#oooooooooo#..",
    "..#oooooooooo#..",
    ".#oooooooooooo#.",
    ".#oooooooooooo#.",
    ".#ooppooooppoo#.",
    ".#oooooooooooo#.",
    "..#oooooooooo#..",
    "..#oooooooooo#..",
    "...#oooooooo#...",
    "....##oooo##....",
    "....##....##....",
    "................",
};

const struct species species[] = {
    {chick,  RGB(255, 220, 70),  RGB(255, 140, 40),  RGB(255, 240, 160)},
    {cat,    RGB(250, 160, 70),  RGB(255, 150, 170), RGB(255, 220, 180)},
    {dog,    RGB(215, 170, 110), RGB(120, 75, 40),   RGB(240, 220, 190)},
    {dragon, RGB(110, 200, 90),  RGB(255, 200, 60),  RGB(220, 240, 170)},
    {bunny,  RGB(240, 240, 240), RGB(255, 150, 170), RGB(255, 255, 255)},
};
const int nspecies = sizeof(species) / sizeof(species[0]);
const struct species *pet = &species[0];

const char *const apple[8] = {
    "....n...",
    "...ngg..",
    ".rrnrr..",
    "rrrrrrr.",
    "rhrrrrr.",
    "rrrrrrr.",
    ".rrrrr..",
    "..r.r...",
};

const char *const ball[8] = {
    "..####..",
    ".#yyyy#.",
    "#yyyyyy#",
    "#rrrrrr#",
    "#rrrrrr#",
    "#yyyyyy#",
    ".#yyyy#.",
    "..####..",
};

const char *const heart[7] = {
    ".rr.rr.",
    "rrrrrrr",
    "rrrrrrr",
    "rrrrrrr",
    ".rrrrr.",
    "..rrr..",
    "...r...",
};

u16 body_color, accent_color, belly_color;

void pet_colors(const struct species *s, int sick)
{
    body_color = sick ? C_SICK : s->body_color;
    accent_color = s->accent_color;
    belly_color = sick ? C_SICK : s->belly_color;
}

u16 palette(char ch)
{
    switch (ch) {
    case '#': return RGB(40, 30, 60);
    case 'o': return body_color;
    case 'a': return accent_color;
    case 'l': return belly_color;
    case 'p': return RGB(255, 140, 170);
    case 'w': return RGB(255, 255, 255);
    case 'b': return RGB(20, 20, 20);
    case 'm': return RGB(200, 60, 60);
    case 'c': return RGB(90, 200, 255);
    case 'r': return RGB(220, 40, 40);
    case 'h': return RGB(255, 180, 180);
    case 'g': return RGB(60, 170, 60);
    case 'G': return RGB(30, 110, 40);
    case 'n': return RGB(110, 70, 30);
    case 'y': return RGB(255, 220, 60);
    case 's': return RGB(140, 140, 150);
    default:  return 0; /* transparent */
    }
}
