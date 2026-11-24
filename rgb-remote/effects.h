// Animations made of color codes. U4 fades from one color to the next by itself
// (a full channel in about 1.13 s), so slow steps look smooth and quick steps jump.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "colors.h"

enum EffectId : uint8_t {
  FX_NONE, FX_RAINBOW, FX_RANDOM, FX_PARTY, FX_POLICE, FX_FIRE, FX_OCEAN,
  FX_FOREST, FX_SUNSET, FX_STROBE, FX_BREATHE, FX_CUSTOM, FX_COUNT
};
static const char *const EFFECT_NAMES[FX_COUNT] = {
  "none", "rainbow", "random", "party", "police", "fire", "ocean",
  "forest", "sunset", "strobe", "breathe", "custom"
};
inline int effectByName(const char *n) {
  for (uint8_t i = 0; i < FX_COUNT; i++)
    if (!strcmp(n, EFFECT_NAMES[i])) return i;
  return -1;
}

// Speed 1 (slow) to 100 (fast) -> milliseconds per step. 130 ms is what two
// frames take, so that's the floor.
inline uint32_t stepMs(uint8_t speed, uint32_t slow, uint32_t fast) {
  if (speed < 1) speed = 1;
  if (speed > 100) speed = 100;
  // Even steps on the slider feel even when the time shrinks by a constant ratio.
  return (uint32_t)(fast * powf((float)slow / fast, (100 - speed) / 99.0f) + 0.5f);
}

#define CUSTOM_MAX 15
struct EffectStep {
  uint8_t command;   // the byte to send
  uint32_t ms;       // until the next step
  bool brightness;   // a brightness button: no colour change
};

// n counts steps from 0. rnd is any random number. custom: the user's colors.
inline EffectStep effectStep(uint8_t fx, uint32_t n, uint32_t rnd, uint8_t speed,
                             int lastIndex, const uint8_t *custom, uint8_t customCount) {
  static const uint8_t POLICE[] = {0, 10};
  static const uint8_t FIRE[]   = {0, 1, 2, 0, 1, 0};
  static const uint8_t OCEAN[]  = {10, 9, 8, 7, 8, 9};
  static const uint8_t FOREST[] = {5, 4, 3, 4, 5, 6};
  static const uint8_t SUNSET[] = {2, 1, 0, 14, 13, 14, 0, 1};
  EffectStep s = {0, 500, false};
  switch (fx) {
    case FX_RAINBOW: s.command = COLORS[n % COLOR_COUNT].code; s.ms = stepMs(speed, 4000, 130); break;
    case FX_RANDOM: case FX_PARTY: {
      uint8_t i = rnd % COLOR_COUNT;
      if ((int)i == lastIndex) i = (i + 1 + (rnd >> 8) % (COLOR_COUNT - 1)) % COLOR_COUNT;
      s.command = COLORS[i].code;
      s.ms = fx == FX_PARTY ? stepMs(speed, 900, 130) : stepMs(speed, 6000, 400);
      break;
    }
    case FX_POLICE: s.command = COLORS[POLICE[n % 2]].code; s.ms = stepMs(speed, 2000, 130); break;
    case FX_FIRE:   s.command = COLORS[FIRE[rnd % 6]].code; s.ms = stepMs(speed, 1500, 130) * (60 + rnd / 7 % 90) / 100; break;
    case FX_OCEAN:  s.command = COLORS[OCEAN[n % 6]].code;  s.ms = stepMs(speed, 5000, 300); break;
    case FX_FOREST: s.command = COLORS[FOREST[n % 6]].code; s.ms = stepMs(speed, 5000, 300); break;
    case FX_SUNSET: s.command = COLORS[SUNSET[n % 8]].code; s.ms = stepMs(speed, 5000, 300); break;
    case FX_STROBE: s.command = n % 2 ? 0x82 : 0x85; s.ms = stepMs(speed, 1500, 150); break;
    case FX_BREATHE: s.command = n % 30 < 15 ? 0x83 : 0x86; s.ms = stepMs(speed, 900, 320); s.brightness = true; break;
    case FX_CUSTOM:
      if (customCount) s.command = COLORS[custom[n % customCount] % COLOR_COUNT].code;
      s.ms = stepMs(speed, 6000, 130);
      break;
  }
  if (s.ms < 130) s.ms = 130;
  return s;
}
