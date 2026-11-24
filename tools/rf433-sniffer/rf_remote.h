// The LED controller's own remote, worked out from captures on 2026-11-24.
//
//   frame = high 310 us, low 5.2 ms, then 28 bits (1.5 ms each)
//   0 = high 310 us + low 1195 us,  1 = high 1195 us + low 310 us
//   28 bits = 12-bit remote address + 8 command bits, each sent twice
//   First frames come back to back; while the button is held a frame repeats
//   every ~62 ms, with ~14 ms of silence after it.
#pragma once
#include "rf_decode.h"

#define REMOTE_BITS 28
#define REMOTE_ADDRESS 0xC81   // this remote; another one has its own

// Commands, labelled on the real remote.
enum RemoteCommand : uint8_t {
  CMD_ON = 0x82, CMD_OFF = 0x85,
  CMD_BRIGHT_UP = 0x86, CMD_BRIGHT_DOWN = 0x83,
  CMD_SPEED_UP = 0x89, CMD_SPEED_DOWN = 0x87,
  CMD_MODE_UP = 0x84, CMD_MODE_DOWN = 0x88
};

inline const char *remoteCommandName(uint8_t c) {
  switch (c) {
    case CMD_ON:          return "on";
    case CMD_OFF:         return "off";
    case CMD_BRIGHT_UP:   return "brightness+";
    case CMD_BRIGHT_DOWN: return "brightness-";
    case CMD_SPEED_UP:    return "speed+";
    case CMD_SPEED_DOWN:  return "speed-";
    case CMD_MODE_UP:     return "mode+";
    case CMD_MODE_DOWN:   return "mode-";
  }
  return nullptr;
}

// Colour wheel: 00pppp11, p = position 0..15, and 15 is followed by 0. The last two
// bits must be 11: U4 ignored 00, 01, 02, 04, 05, 06, 14 and 28. It also ignores a
// code sent as a single frame.
// 14 of 16 captured; 2 and 12 have not shown up yet.
inline int8_t remoteWheel(uint8_t c) { return (c & 0xC3) == 0x03 ? c >> 2 : -1; }

struct RemoteCode { uint16_t address; uint8_t command; };

// Held frames end in silence, so the decoder sees 27 bits and a lone high.
// That high is the 28th bit. In back-to-back frames the lone high is the next
// frame's sync pulse and carries nothing.
inline void remoteNormalize(RfCode &c) {
  if (c.type != RF_PWM || c.tLong < c.tShort * 3 || c.tLong > c.tShort * 5) return;
  if (c.bits == REMOTE_BITS - 1 && c.tail) { rfAddBit(c, c.tail == 2); c.tail = 0; }
  else if (c.bits == REMOTE_BITS && c.tail == 1) c.tail = 0;
}

// False when the code is from some other transmitter, or a bit pair disagrees.
inline bool remoteRead(const RfCode &c, RemoteCode &r) {
  if (c.type != RF_PWM || c.bits != REMOTE_BITS || c.tail) return false;
  r.address = (uint16_t)c.data[0] << 4 | c.data[1] >> 4;
  uint16_t twice = (uint16_t)(c.data[1] & 0x0F) << 12 | (uint16_t)c.data[2] << 4 | c.data[3] >> 4;
  r.command = 0;
  for (int8_t i = 7; i >= 0; i--) {
    uint8_t pair = (twice >> (i * 2)) & 3;
    if (pair == 1 || pair == 2) return false;
    r.command = r.command << 1 | (pair == 3);
  }
  return true;
}

// ---------- sending ----------
#define SEND_PULSES (2 + REMOTE_BITS * 2)
#define SEND_SHORT 310
#define SEND_LONG  1195
#define SEND_SYNC  5230
#define SEND_PAUSE 14200   // silence between held frames

// Fills us[] with one frame: high, low, high, low ... starting with the sync.
inline void remoteFrame(uint16_t address, uint8_t command, uint16_t us[SEND_PULSES]) {
  uint32_t bits = address & 0xFFF;
  for (int8_t i = 7; i >= 0; i--) bits = bits << 2 | ((command >> i & 1) ? 3 : 0);
  uint8_t n = 0;
  us[n++] = SEND_SHORT;
  us[n++] = SEND_SYNC;
  for (int8_t i = REMOTE_BITS - 1; i >= 0; i--) {
    bool b = bits >> i & 1;
    us[n++] = b ? SEND_LONG : SEND_SHORT;
    us[n++] = b ? SEND_SHORT : SEND_LONG;
  }
}

// What the user types -> command byte. Returns -1 when it isn't one.
inline int remoteParse(const char *t) {
  static const struct { const char *name; uint8_t cmd; } T[] = {
    {"on", CMD_ON}, {"off", CMD_OFF},
    {"b+", CMD_BRIGHT_UP}, {"b-", CMD_BRIGHT_DOWN},
    {"s+", CMD_SPEED_UP}, {"s-", CMD_SPEED_DOWN},
    {"m+", CMD_MODE_UP}, {"m-", CMD_MODE_DOWN},
  };
  for (const auto &e : T)
    if (!strcmp(t, e.name)) return e.cmd;
  for (const auto &e : T)
    if (!strcmp(t, remoteCommandName(e.cmd))) return e.cmd;
  if (t[0] == 'w' && t[1] >= '0' && t[1] <= '9') {
    char *end;
    long p = strtol(t + 1, &end, 10);
    if (!*end && p <= 15) return (int)(p << 2 | 3);
  }
  return -1;
}
