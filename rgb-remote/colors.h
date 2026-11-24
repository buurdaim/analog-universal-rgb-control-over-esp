// The colors U4 can make, measured on its outputs on 2026-11-24 at full
// brightness. Fifteen in all: 14 from the remote's wheel (positions 2 and 12 do
// nothing) and 0x47, which the remote never sends. Order: around the color ring.
#pragma once
#include <stdint.h>

struct RgbColor { uint8_t code, r, g, b; };   // r, g, b in percent

static const RgbColor COLORS[] = {
  {0x3B, 100,   0,   0},   // 0  red
  {0x3F, 100,  25,   0},   // 1
  {0x03, 100,  50,   0},   // 2  orange
  {0x07,  50, 100,   0},   // 3
  {0x0F,  25, 100,   0},   // 4
  {0x13,   0, 100,   0},   // 5  green
  {0x17,   0, 100,  25},   // 6
  {0x47,   0, 100,  50},   // 7  the hidden one
  {0x1B,   0,  50, 100},   // 8
  {0x1F,   0,  25, 100},   // 9
  {0x23,   0,   0, 100},   // 10 blue
  {0x27,  25,   0, 100},   // 11
  {0x2B,  50,   0, 100},   // 12 purple
  {0x2F, 100,   0,  50},   // 13
  {0x37, 100,   0,  25},   // 14
};
static const uint8_t COLOR_COUNT = sizeof COLORS / sizeof COLORS[0];

// Brightness as U4 shows it, in percent, step 0 (dimmest) to 15.
static const uint8_t BRIGHT[16] = {16, 20, 24, 28, 32, 38, 44, 50, 57, 63, 69, 76, 82, 88, 94, 100};
#define BRIGHT_MAX 15

inline int colorIndex(uint8_t code) {
  for (uint8_t i = 0; i < COLOR_COUNT; i++)
    if (COLORS[i].code == code) return i;
  return -1;
}

// The nearest of the 15 to any color (0-255 each).
inline uint8_t nearestColor(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t m = r > g ? (r > b ? r : b) : (g > b ? g : b);
  if (!m) return 0;
  // U4's colors are fully saturated, so compare without the gray part.
  uint8_t lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
  int span = m - lo ? m - lo : 1;
  int R = (r - lo) * 100 / span, G = (g - lo) * 100 / span, B = (b - lo) * 100 / span;
  long best = 0x7FFFFFFF;
  uint8_t at = 0;
  for (uint8_t i = 0; i < COLOR_COUNT; i++) {
    long dr = R - COLORS[i].r, dg = G - COLORS[i].g, db = B - COLORS[i].b;
    long d = dr * dr + dg * dg + db * db;
    if (d < best) { best = d; at = i; }
  }
  return at;
}

// U4's own modes, a loop of 11 that its mode buttons step through. U4 remembers
// the place. From a plain color, the first press only goes back to that place;
// after that each press moves one. Measured 2026-11-24.
static const char *const U4_MODES[] = {
  "breathe3", "fade7", "blue", "red", "green", "magenta", "cyan", "yellow", "white", "jump3", "jump7"
};
#define U4_MODE_COUNT 11

// Presses from mode a to mode b the short way round: positive is mode +.
inline int modePresses(int a, int b) {
  int d = ((b - a) % U4_MODE_COUNT + U4_MODE_COUNT) % U4_MODE_COUNT;
  return d > U4_MODE_COUNT / 2 ? d - U4_MODE_COUNT : d;
}
