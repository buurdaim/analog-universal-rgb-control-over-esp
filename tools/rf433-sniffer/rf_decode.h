// Decoder for on/off-keyed 433 MHz remotes. No Arduino code in here, so the
// same file is tested on the Mac (test/test_decode.cpp).
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RF_MAX_PULSES 300   // longest frame kept, in pulses
#define RF_MAX_BYTES  16    // longest code kept: 128 bits
#define RF_MIN_BITS   8
#define RF_MIN_US     60    // anything shorter is receiver noise

enum RfType : uint8_t {
  RF_NONE,      // could not be read
  RF_PWM,       // 0 = short high + long low, 1 = long high + short low (EV1527, PT2262)
  RF_PWM_INV,   // same, but each bit starts with the low part
  RF_PULSEWIDTH,// only the high time changes
  RF_PULSEDIST, // only the low time changes
  RF_MANCHESTER
};

inline const char *rfTypeName(uint8_t t) {
  switch (t) {
    case RF_PWM:        return "PWM";
    case RF_PWM_INV:    return "PWM-inv";
    case RF_PULSEWIDTH: return "pulse-width";
    case RF_PULSEDIST:  return "pulse-dist";
    case RF_MANCHESTER: return "manchester";
  }
  return "unknown";
}

struct RfFrame {
  uint16_t us[RF_MAX_PULSES]; // high, low, high, low ... always starts with a high
  uint16_t n;
  uint16_t gap;               // the long low that ended the frame
};

struct RfCode {
  uint8_t  type;
  uint8_t  bits;
  uint8_t  tail;              // last high without a low: 0 none, 1 short, 2 long
  uint8_t  data[RF_MAX_BYTES];// first bit received = top bit of data[0]
  uint16_t tShort, tLong, gap;
};

inline bool rfSameCode(const RfCode &a, const RfCode &b) {
  return a.type == b.type && a.bits == b.bits && a.tail == b.tail &&
         memcmp(a.data, b.data, RF_MAX_BYTES) == 0;
}

// Same shape, pulse for pulse? Used to spot real signals we can't decode.
inline bool rfSameFrame(const RfFrame &a, const RfFrame &b) {
  if (a.n != b.n || a.n < 16) return false;
  for (uint16_t i = 0; i < a.n; i++) {
    uint16_t hi = a.us[i] > b.us[i] ? a.us[i] : b.us[i];
    uint16_t d  = a.us[i] > b.us[i] ? a.us[i] - b.us[i] : b.us[i] - a.us[i];
    if (d > hi / 4 && d > 60) return false;
  }
  return true;
}

// Cuts the pulse stream into frames at every long low.
class RfFramer {
 public:
  uint16_t gapUs = 2200;

  // Returns true when 'out' holds a finished frame.
  bool feed(uint8_t level, uint32_t us, RfFrame &out) {
    if (!level && us >= gapUs) {
      bool ok = cur_.n >= RF_MIN_BITS * 2 - 1;
      if (ok) {
        memcpy(out.us, cur_.us, cur_.n * sizeof(uint16_t));
        out.n = cur_.n;
        out.gap = us > 65535 ? 65535 : us;
      }
      cur_.n = 0;
      return ok;
    }
    if (us < RF_MIN_US || (level && us > 3000)) { cur_.n = 0; return false; }
    uint8_t want = (cur_.n % 2 == 0);           // even index = high
    if (level != want) {                        // lost an edge
      cur_.n = 0;
      if (!level) return false;                 // frames start with a high
    }
    if (cur_.n >= RF_MAX_PULSES) { cur_.n = 0; return false; }
    cur_.us[cur_.n++] = us;
    return false;
  }

 private:
  RfFrame cur_ = {{0}, 0, 0};
};

// One or two groups of similar durations.
struct RfGroups { uint8_t count; uint16_t split, lo, hi; };

inline int rfCmp16(const void *a, const void *b) {
  return (int)*(const uint16_t *)a - (int)*(const uint16_t *)b;
}

inline bool rfGroup(const uint16_t *us, uint16_t first, uint16_t n, RfGroups &g) {
  static uint16_t s[RF_MAX_PULSES];
  uint16_t m = 0;
  for (uint16_t i = first; i < n; i += 2) s[m++] = us[i];
  if (m < RF_MIN_BITS - 1) return false;
  qsort(s, m, sizeof(uint16_t), rfCmp16);
  uint16_t cut = 0;
  float best = 1.45f;
  for (uint16_t i = 1; i < m; i++) {
    float r = (float)s[i] / s[i - 1];
    if (r > best) { best = r; cut = i; }
  }
  if (!cut) {
    if (s[m - 1] > s[0] * 1.9f) return false;
    g.count = 1;
    g.lo = g.hi = s[m / 2];
    g.split = 0;
    return true;
  }
  if (s[cut - 1] > s[0] * 1.9f || s[m - 1] > s[cut] * 1.9f) return false;
  g.count = 2;
  g.lo = s[cut / 2];
  g.hi = s[cut + (m - cut) / 2];
  g.split = (s[cut - 1] + s[cut]) / 2;
  return true;
}

inline void rfAddBit(RfCode &c, bool b) {
  if (c.bits >= RF_MAX_BYTES * 8) return;
  if (b) c.data[c.bits / 8] |= 0x80 >> (c.bits % 8);
  c.bits++;
}

inline bool rfManchester(const RfFrame &f, const RfGroups &h, const RfGroups &l, RfCode &c) {
  // Turn the frame into half-bit levels, then read them in pairs.
  static uint8_t lv[RF_MAX_PULSES * 2 + 1];
  uint16_t m = 0;
  for (uint16_t i = 0; i < f.n; i++) {
    const RfGroups &g = (i % 2) ? l : h;
    bool lng = f.us[i] > g.split;
    lv[m++] = !(i % 2);
    if (lng) lv[m++] = !(i % 2);
  }
  if (m % 2) lv[m++] = 0;                       // last low is hidden in the gap
  for (uint8_t off = 0; off < 2; off++) {
    RfCode t = c;
    bool ok = true;
    for (uint16_t i = off; i + 1 < m && ok; i += 2) {
      if (lv[i] == lv[i + 1]) ok = false;
      else rfAddBit(t, lv[i]);
    }
    if (ok && t.bits >= RF_MIN_BITS) { c = t; c.type = RF_MANCHESTER; return true; }
  }
  return false;
}

inline bool rfDecode(const RfFrame &f, RfCode &c) {
  memset(&c, 0, sizeof c);
  c.gap = f.gap;
  RfGroups h, l;
  uint16_t even = f.n & ~1;                     // the last lone high is judged separately
  if (!rfGroup(f.us, 0, even, h) || !rfGroup(f.us, 1, even, l)) return false;
  c.tShort = h.lo < l.lo ? h.lo : l.lo;
  c.tLong  = h.hi > l.hi ? h.hi : l.hi;
  if (c.tLong < c.tShort * 1.4f) return false;  // all pulses alike: a preamble, not data

  uint16_t pairs = f.n / 2;
  bool oddTail = f.n % 2;

  if (h.count == 1 && l.count == 1) {           // every bit the same
    c.type = RF_PWM;
    for (uint16_t i = 0; i < pairs; i++) rfAddBit(c, h.lo > l.lo);
  } else if (h.count == 2 && l.count == 1) {
    c.type = RF_PULSEWIDTH;
    for (uint16_t i = 0; i < pairs; i++) rfAddBit(c, f.us[i * 2] > h.split);
  } else if (h.count == 1 && l.count == 2) {
    c.type = RF_PULSEDIST;
    for (uint16_t i = 0; i < pairs; i++) rfAddBit(c, f.us[i * 2 + 1] > l.split);
  } else {
    bool hl = true, lh = true;
    for (uint16_t i = 0; i < pairs && hl; i++)
      hl = (f.us[i * 2] > h.split) != (f.us[i * 2 + 1] > l.split);
    for (uint16_t i = 1; i + 1 < f.n && lh; i += 2)
      lh = (f.us[i] > l.split) != (f.us[i + 1] > h.split);
    if (hl) {
      c.type = RF_PWM;
      for (uint16_t i = 0; i < pairs; i++) rfAddBit(c, f.us[i * 2] > h.split);
    } else if (lh) {
      c.type = RF_PWM_INV;
      for (uint16_t i = 1; i + 1 < f.n; i += 2) rfAddBit(c, f.us[i + 1] > h.split);
      oddTail = false;
    } else {
      return rfManchester(f, h, l, c);
    }
  }
  if (oddTail) {
    uint16_t mid = h.count == 2 ? h.split : (h.lo > l.lo ? 0 : 65535);
    c.tail = f.us[f.n - 1] > mid ? 2 : 1;
  }
  return c.bits >= RF_MIN_BITS;
}
