// Reads the LED controller's remote codes and sends the same codes back.
//
// Wiring (leave U4 and everything else on the board as it is):
//   U4 pin 4 ------- READ pin (ESP32 GPIO 34)
//   U4 pin 8 (GND) - ESP GND
//   SEND pin (ESP32 GPIO 27): either a jumper to the READ pin, so the code goes
//   straight into U4, or the DATA pin of a 433 MHz transmitter module.
//   Optional, for 'levels', 'map' and 'speed': U4 pins 5, 6, 7, each through 10k,
//   to GPIO 32, 33, 25. These are only read.
//   The ESP runs from USB. Do not connect U4 pin 1 to the ESP.
//
// Serial Monitor: 115200, "New Line". On an ESP32-C3 SuperMini set
// Tools > USB CDC On Boot > Enabled.
//
// Type help for the commands.

#include <Arduino.h>
#include "rf_decode.h"
#include "rf_remote.h"

#if defined(ESP8266)
  #define RF_PIN 5      // D1
  #define SEND_PIN 4    // D2
  #define OUT_PINS 12, 13, 14
#elif CONFIG_IDF_TARGET_ESP32C3
  #define RF_PIN 6
  #define SEND_PIN 7
  #define OUT_PINS 3, 4, 5
#else
  #define RF_PIN 34     // classic ESP32
  #define SEND_PIN 27
  #define OUT_PINS 32, 33, 25
#endif

// ---------- edges from the interrupt ----------
#define RING 1024
static volatile uint32_t ring[RING];     // microseconds << 1 | level that just ended
static volatile uint16_t head = 0;
static volatile uint32_t edges = 0, lost = 0;
static volatile uint16_t tail_ = 0;
static volatile uint32_t lastEdge = 0;

void IRAM_ATTR onEdge() {
  uint32_t now = micros();
  uint32_t us = now - lastEdge;
  lastEdge = now;
  edges = edges + 1;
  if (us > 1000000) us = 1000000;
  uint8_t ended = !digitalRead(RF_PIN);
  uint16_t next = (head + 1) % RING;
  if (next == tail_) { lost = lost + 1; return; }
  ring[head] = (us << 1) | ended;
  head = next;
}

// ---------- learned buttons ----------
#define MAX_KEYS 64
struct Key { char name[16]; RfCode code; };
static Key keys[MAX_KEYS];
static uint8_t nKeys = 0;
static char waiting[16] = "";            // name for the next code
static uint16_t seenCount[256];          // presses per command byte
static uint16_t seenAddr = 0;
static bool quiet = false;               // no line per code while animating
static int heard = -1;                   // last command the reader decoded

static RfFramer framer;
static RfFrame frame, lastFrame;
static RfCode code, lastCode, shown;
static bool haveLast = false, haveShown = false, rawMode = false;
static uint16_t sameCount = 0, repeats = 0;
static uint32_t lastGoodMs = 0, pressMs = 0, lastOddMs = 0, lastCodeMs = 0;

static void printHex(const RfCode &c) {
  for (uint8_t i = 0; i < (c.bits + 7) / 8; i++) Serial.printf("%02X", c.data[i]);
}

static bool bitOf(const RfCode &c, uint8_t i) { return c.data[i / 8] & (0x80 >> (i % 8)); }

static void printBits(const RfCode &c, const uint8_t *mask) {
  for (uint8_t i = 0; i < c.bits; i++) {
    if (i && i % 4 == 0) Serial.print(' ');
    if (mask && !(mask[i / 8] & (0x80 >> (i % 8)))) Serial.print('-');
    else Serial.print(bitOf(c, i) ? '1' : '0');
  }
}

static const char *nameOf(const RfCode &c) {
  for (uint8_t i = 0; i < nKeys; i++)
    if (rfSameCode(keys[i].code, c)) return keys[i].name;
  return nullptr;
}

static void learn(const RfCode &c) {
  for (uint8_t i = 0; i < nKeys; i++) {
    if (strcmp(keys[i].name, waiting)) continue;
    if (!rfSameCode(keys[i].code, c))
      Serial.printf("   note: '%s' had a different code before. Replaced.\n", waiting);
    keys[i].code = c;
    waiting[0] = 0;
    return;
  }
  if (nKeys >= MAX_KEYS) { Serial.println("   table is full"); waiting[0] = 0; return; }
  strncpy(keys[nKeys].name, waiting, sizeof keys[nKeys].name - 1);
  keys[nKeys].code = c;
  nKeys++;
  Serial.printf("   saved as '%s' (%u learned)\n", waiting, nKeys);
  waiting[0] = 0;
}

static void printRaw(const RfFrame &f) {
  Serial.printf("   raw %u pulses (high,low,...), gap %u us:\n   ", f.n, f.gap);
  for (uint16_t i = 0; i < f.n; i++) Serial.printf("%u%s", f.us[i], i + 1 < f.n ? "," : "\n");
}

static void showCode(const RfCode &c) {
  RemoteCode rc;
  if (remoteRead(c, rc)) {
    heard = rc.command;
    if (quiet) return;
    Serial.printf("remote %03X  command 0x%02X  ", rc.address, rc.command);
    for (int8_t i = 7; i >= 0; i--) Serial.print((rc.command >> i & 1) ? '1' : '0');
    const char *nm = nameOf(c);
    if (remoteCommandName(rc.command)) Serial.printf("  %s", remoteCommandName(rc.command));
    else if (remoteWheel(rc.command) >= 0) Serial.printf("  wheel %d", remoteWheel(rc.command));
    if (nm) Serial.printf("  <%s>", nm);
    Serial.println();
    if (seenCount[rc.command] < 65535) seenCount[rc.command]++;
    seenAddr = rc.address;
    if (!rawMode) return;
  }
  Serial.printf("%s %u bits  0x", rfTypeName(c.type), c.bits);
  printHex(c);
  Serial.print("  ");
  printBits(c, nullptr);
  Serial.printf("  short %u us, long %u us, gap %u us", c.tShort, c.tLong, c.gap);
  if (c.tail) Serial.printf(", tail %s", c.tail == 2 ? "long" : "short");
  const char *nm = nameOf(c);
  if (nm) Serial.printf("  <%s>", nm);
  Serial.println();
}

static void listKeys() {
  if (!nKeys) { Serial.println("Nothing learned yet."); return; }
  uint8_t mask[RF_MAX_BYTES];
  memset(mask, 0xFF, sizeof mask);
  bool same = true;
  for (uint8_t k = 1; k < nKeys; k++) {
    if (keys[k].code.type != keys[0].code.type || keys[k].code.bits != keys[0].code.bits) same = false;
    for (uint8_t i = 0; i < RF_MAX_BYTES; i++) mask[i] &= ~(keys[k].code.data[i] ^ keys[0].code.data[i]);
  }
  Serial.println("--- learned ---");
  for (uint8_t k = 0; k < nKeys; k++) {
    Serial.printf("%-15s ", keys[k].name);
    RemoteCode rc;
    if (remoteRead(keys[k].code, rc)) {
      Serial.printf("remote %03X  command 0x%02X\n", rc.address, rc.command);
      continue;
    }
    Serial.print("0x");
    printHex(keys[k].code);
    Serial.print("  ");
    printBits(keys[k].code, nullptr);
    Serial.printf("  %s%s\n", rfTypeName(keys[k].code.type),
                  keys[k].code.tail == 2 ? " tail long" : "");
  }
  if (!same) Serial.println("The codes have different lengths, so no common part is shown.");
  else if (nKeys > 1) {
    Serial.print("same in all:    ");
    printBits(keys[0].code, mask);
    Serial.println("   (digits = remote's address, - = button part)");
  }
  Serial.println("---------------");
}

static void listSeen() {
  Serial.printf("--- commands seen, last remote %03X ---\n", seenAddr);
  for (uint16_t i = 0; i < 256; i++)
    if (seenCount[i]) Serial.printf("0x%02X  x%u\n", i, seenCount[i]);
}

static void stat() {
  uint32_t e = edges, wasLost = lost;
  delay(500);
  e = edges - e;
  lost = wasLost;                        // the pause itself overflows the buffer
  tail_ = head;
  Serial.printf("pin now %d, %lu edges/s, %lu lost\n", digitalRead(RF_PIN),
                (unsigned long)e * 2, (unsigned long)lost);
  if (!e) Serial.println("No edges: check pad 4, the divider, the shared GND, and that the board has 12 V.");
  else Serial.println("Edges arrive. Random ones with no button pressed are normal receiver noise.");
}

// ---------- sending ----------
static uint16_t sendAddr = REMOTE_ADDRESS;
static int lastSent = -1;
static uint32_t lastSendMs = 0;
static uint8_t animFrames = 2;           // frames per color while animating
static void pump();

// The pin is driven only while sending. Between codes it floats, so the real
// remote keeps working through the same wire.
static void sendCode(uint8_t cmd, uint16_t frames) {
  uint16_t us[SEND_PULSES];
  remoteFrame(sendAddr, cmd, us);
  heard = -1;
  digitalWrite(SEND_PIN, LOW);
  pinMode(SEND_PIN, OUTPUT);
  // Let U4 forget any half-received noise, unless we only just sent something.
  delayMicroseconds(millis() - lastSendMs < 60 ? 2000 : 12000);
  for (uint16_t f = 0; f < frames; f++) {
    uint32_t t = micros();
    for (uint8_t i = 0; i < SEND_PULSES; i++) {
      digitalWrite(SEND_PIN, i % 2 ? LOW : HIGH);
      t += us[i];
      int32_t left = (int32_t)(t - micros());
      if (left > 0) delayMicroseconds(left);
    }
    if (f >= 2) delayMicroseconds(SEND_PAUSE);  // held frames are spaced out
    pump();
  }
  delayMicroseconds(8000);               // low after the last bit ends the frame
  pinMode(SEND_PIN, INPUT);
  pump();
  lastSent = cmd;
  lastSendMs = millis();
}

static bool sendAndReport(uint8_t cmd, uint16_t frames) {
  sendCode(cmd, frames);
  bool ok = heard == cmd;
  Serial.printf("sent 0x%02X", cmd);
  if (remoteCommandName(cmd)) Serial.printf(" %s", remoteCommandName(cmd));
  else if (remoteWheel(cmd) >= 0) Serial.printf(" wheel %d", remoteWheel(cmd));
  Serial.println(ok ? "   heard back: yes" : "   heard back: NO (check the jumper or transmitter)");
  return ok;
}

// Sends every code from a to b. Any key stops it.
static void scan(uint8_t a, uint8_t b, uint16_t pauseMs) {
  Serial.println("Press Enter to stop.");
  for (uint16_t c = a; c <= b; c++) {
    if (Serial.available()) { Serial.println("stopped"); return; }
    sendAndReport(c, 6);
    for (uint16_t ms = 0; ms < pauseMs && !Serial.available(); ms += 10) { delay(10); pump(); }
  }
  Serial.println("scan finished");
}

// ---------- measuring U4's outputs ----------
static const uint8_t OUT[3] = { OUT_PINS };

// Duty in tenths of a percent and rising edges, over ms milliseconds.
static void sample(uint32_t ms, uint16_t duty[3], uint32_t rises[3]) {
  uint32_t hi[3] = {0, 0, 0}, n = 0, start = micros();
  bool was[3];
  for (uint8_t c = 0; c < 3; c++) { was[c] = digitalRead(OUT[c]); rises[c] = 0; }
  while (micros() - start < ms * 1000) {
    for (uint8_t c = 0; c < 3; c++) {
      bool v = digitalRead(OUT[c]);
      hi[c] += v;
      rises[c] += v && !was[c];
      was[c] = v;
    }
    n++;
  }
  for (uint8_t c = 0; c < 3; c++) duty[c] = n ? hi[c] * 1000ULL / n : 0;
}

static bool near(const uint16_t a[3], const uint16_t b[3], uint16_t tol) {
  for (uint8_t c = 0; c < 3; c++)
    if (abs((int)a[c] - (int)b[c]) > tol) return false;
  return true;
}

// Waits until the outputs stop changing. False if they never do.
static bool settle(uint16_t duty[3]) {
  uint16_t prev[3] = {0, 0, 0};
  uint32_t e[3];
  uint8_t same = 0;
  for (uint8_t i = 0; i < 30; i++) {
    sample(100, duty, e);
    pump();
    same = i && near(duty, prev, 12) ? same + 1 : 0;
    if (same >= 2) return true;
    memcpy(prev, duty, sizeof prev);
  }
  return false;
}

static void printDuty(const uint16_t d[3]) {
  Serial.printf("p5 %3u.%u%%  p6 %3u.%u%%  p7 %3u.%u%%", d[0] / 10, d[0] % 10,
                d[1] / 10, d[1] % 10, d[2] / 10, d[2] % 10);
}

static void levels() {
  uint16_t d[3];
  uint32_t e[3];
  sample(1000, d, e);
  printDuty(d);
  Serial.printf("   %lu / %lu / %lu Hz\n", (unsigned long)e[0], (unsigned long)e[1], (unsigned long)e[2]);
}

// Puts U4 in the same state before every code: on, wheel 0.
static bool baseline(uint16_t d[3]) {
  sendCode(0x03, 5);
  bool ok = settle(d);
  if (d[0] + d[1] + d[2] < 20) { sendCode(CMD_ON, 5); delay(300); sendCode(0x03, 5); ok = settle(d); }
  return ok;
}

static bool outputsWired(uint16_t base[3]) {
  uint16_t off[3];
  baseline(base);
  sendCode(CMD_OFF, 5);
  settle(off);
  baseline(base);
  if (base[0] + base[1] + base[2] >= 50 && off[0] + off[1] + off[2] < 20) return true;
  Serial.print("wheel 0: "); printDuty(base);
  Serial.print("\noff:     "); printDuty(off);
  Serial.println("\nU4's outputs aren't visible. Wire U4 pins 5, 6, 7 through 10k to the OUT pins.");
  return false;
}

// Sends every code from a to b and records what the outputs do.
static void mapCodes(uint8_t a, uint8_t b) {
  uint16_t base[3], now[3], lo[3], hi[3];
  uint32_t e[3];
  quiet = true;
  Serial.println("Mapping. Press Enter to stop.");
  if (outputsWired(base)) {
    Serial.print("base wheel 0: "); printDuty(base); Serial.println();
    uint16_t c = a, ignored = 0;
    for (; c <= b && !Serial.available(); c++) {
      if (remoteCommandName(c)) continue;                  // known buttons
      sample(60, now, e);
      if (!near(now, base, 25)) {                          // back to the same start
        baseline(now);
        if (!near(now, base, 25)) {
          Serial.printf("   base changed before 0x%02X: ", c);
          printDuty(now); Serial.println();
          memcpy(base, now, sizeof base);
        }
      }
      sendCode(c, 4);
      for (uint8_t k = 0; k < 5; k++) {
        sample(100, now, e);
        for (uint8_t i = 0; i < 3; i++) {
          if (!k || now[i] < lo[i]) lo[i] = now[i];
          if (!k || now[i] > hi[i]) hi[i] = now[i];
        }
        pump();
      }
      if (near(lo, base, 25) && near(hi, base, 25)) {
        ignored++;
        if (c % 16 == 15) Serial.printf("... up to 0x%02X\n", c);
        continue;
      }
      bool steady = settle(now);
      if (steady) {                                        // still steady a second later?
        memcpy(lo, now, sizeof lo);
        delay(1000);
        sample(100, now, e);
        steady = near(lo, now, 25);
      }
      Serial.printf("map 0x%02X  ", c);
      printDuty(now);
      Serial.println(steady ? "  steady" : "  moving");
    }
    Serial.printf("%u codes did nothing\n", ignored);
    Serial.println(c > b ? "map finished" : "stopped");
    baseline(now);
  }
  quiet = false;
}

// Duty over time, one line per step: R G B in percent.
static void trace(uint32_t ms, uint32_t stepMs) {
  uint16_t d[3];
  uint32_t e[3], t0 = millis();
  while (millis() - t0 < ms && !Serial.available()) {
    sample(stepMs, d, e);
    Serial.printf("t %5lu  %3u %3u %3u\n", (unsigned long)(millis() - t0), (d[0] + 5) / 10, (d[1] + 5) / 10, (d[2] + 5) / 10);
    pump();
  }
  Serial.println("trace finished");
}

// Records how U4 moves from one wheel color to another.
static void fade(uint8_t from, uint8_t to) {
  uint16_t d[3];
  uint32_t e[3];
  quiet = true;
  sendCode(from << 2 | 3, 4);
  settle(d);
  Serial.printf("wheel %u: ", from); printDuty(d); Serial.println();
  sendCode(to << 2 | 3, 2);
  uint32_t t0 = millis();
  for (uint8_t i = 0; i < 40; i++) {
    sample(50, d, e);
    Serial.printf("%4lu ms  ", (unsigned long)(millis() - t0)); printDuty(d); Serial.println();
  }
  quiet = false;
  Serial.println("fade finished");
}

// How many frames does U4 need before it obeys?
static void speedTest() {
  uint16_t a[3], b[3], now[3];
  uint32_t e[3];
  quiet = true;
  if (outputsWired(a)) {
    sendCode(0x23, 5);
    settle(b);
    for (uint8_t f = 1; f <= 5; f++) {
      uint8_t ok = 0;
      uint32_t t = 0;
      for (uint8_t i = 0; i < 10; i++) {
        uint32_t t0 = millis();
        sendCode(i % 2 ? 0x23 : 0x03, f);
        t += millis() - t0;
        delay(1500);                     // U4 fades for up to about 1.2 s
        sample(60, now, e);
        ok += near(now, i % 2 ? b : a, 40);
      }
      Serial.printf("%u frame(s): %u of 10 obeyed, %lu ms per code\n", f, ok, (unsigned long)(t / 10));
    }
  }
  quiet = false;
}

// ---------- animations ----------
// U4 only changes when it gets a code, so a step takes about 50 ms per frame.
static void anim(const char *what, uint32_t stepMs) {
  static const char *const NAMES[] = { "rainbow", "random", "strobe", "breathe" };
  int8_t k = -1;
  for (uint8_t i = 0; i < 4; i++) if (!strcmp(what, NAMES[i])) k = i;
  if (k < 0) { Serial.println("use: anim rainbow|random|strobe|breathe [ms per step]"); return; }
  Serial.printf("%s, %u frame(s) per step. Press Enter to stop.\n", what, animFrames);
  quiet = true;
  sendCode(CMD_ON, 5);
  uint32_t steps = 0, t0 = millis();
  int last = -1;
  while (!Serial.available()) {
    uint32_t t = millis();
    int c;
    if (k == 0) c = (steps % 16) << 2 | 3;
    else if (k == 1) { do c = random(16) << 2 | 3; while (c == last); }
    else if (k == 2) c = steps % 2 ? CMD_ON : CMD_OFF;
    else c = steps % 16 < 8 ? CMD_BRIGHT_DOWN : CMD_BRIGHT_UP;
    sendCode(c, animFrames);
    last = c;
    steps++;
    while (millis() - t < stepMs && !Serial.available()) { delay(2); pump(); }
  }
  quiet = false;
  if (k == 2) sendCode(CMD_ON, 5);
  Serial.printf("stopped after %lu steps, %lu ms each\n", (unsigned long)steps,
                (unsigned long)((millis() - t0) / (steps ? steps : 1)));
}

// For finding hidden functions by eye: wheel 0 before every code, so any effect
// shows up as a change. Enter marks the code, q stops.
static void watch(uint8_t a, uint8_t b, uint32_t ms) {
  uint8_t marked[64];
  uint8_t n = 0;
  bool stop = false;
  quiet = true;
  Serial.println("The LEDs go to wheel 0 before every code. If a code does something, press Enter.");
  Serial.println("q + Enter stops.");
  sendCode(CMD_ON, 5);
  for (uint16_t c = a; c <= b && !stop; c++) {
    if (remoteCommandName(c) || remoteWheel(c) >= 0) continue;   // known already
    sendCode(0x03, 3);
    for (uint16_t t = 0; t < 400; t += 10) { delay(10); pump(); }
    sendCode(c, 4);
    Serial.printf("0x%02X\n", c);
    bool mark = false;
    for (uint32_t t0 = millis(); millis() - t0 < ms && !stop;) {
      delay(5);
      pump();
      while (Serial.available()) {
        char ch = Serial.read();
        if (ch == 'q') stop = true;
        else if (ch == '\n' && !stop) mark = true;
      }
    }
    if (mark && n < sizeof marked) { marked[n++] = c; Serial.printf("   MARKED 0x%02X\n", c); }
  }
  quiet = false;
  Serial.print(n ? "marked:" : "nothing marked");
  for (uint8_t i = 0; i < n; i++) Serial.printf(" %02X", marked[i]);
  Serial.println();
  sendCode(0x03, 3);
}

static void help() {
  Serial.println("SEND   on off   b+ b-   s+ s-   m+ m-   w0 ... w15");
  Serial.println("       hold <name> <ms>    hold a button, e.g. hold b- 2000");
  Serial.println("       cmd <hex>           any command byte, e.g. cmd 83");
  Serial.println("       next  prev          step to the next or previous command byte");
  Serial.println("       scan <from> <to> [pause ms]   e.g. scan 80 8F 2000");
  Serial.println("       anim rainbow|random|strobe|breathe [ms]    Enter stops it");
  Serial.println("       frames <2-9>        frames per animation step (U4 ignores a single one)");
  Serial.println("       watch <from> <to> [ms]   hidden codes by eye, Enter marks, q stops");
  Serial.println("       addr <hex>          remote address to send as");
  Serial.println("U4     levels              duty and PWM frequency of pins 5, 6, 7");
  Serial.println("       map [from to]       what every code does, e.g. map 00 3F");
  Serial.println("       speed               fewest frames U4 needs");
  Serial.println("       fade <from> <to>    how U4 moves between two wheel positions");
  Serial.println("READ   learn <name>        then press that button on the remote");
  Serial.println("list  = table and common part    raw = show pulse times on/off");
  Serial.println("stat  = is a signal arriving?    gap <us> = frame gap (now in use below)");
  Serial.println("clear = forget learned buttons   seen = all commands so far   help");
  Serial.printf("gap %u us, pin GPIO %d\n", framer.gapUs, RF_PIN);
}

static void command(char *s) {
  while (*s == ' ') s++;
  char *e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\r')) *--e = 0;
  if (!*s) return;
  if (!strcmp(s, "list")) listKeys();
  else if (!strcmp(s, "seen")) listSeen();
  else if (!strcmp(s, "help")) help();
  else if (!strcmp(s, "stat")) stat();
  else if (!strcmp(s, "clear")) { nKeys = 0; memset(seenCount, 0, sizeof seenCount); Serial.println("cleared"); }
  else if (!strcmp(s, "raw")) { rawMode = !rawMode; Serial.printf("raw %s\n", rawMode ? "on" : "off"); }
  else if (!strncmp(s, "gap ", 4)) {
    int g = atoi(s + 4);
    if (g >= 800 && g <= 30000) framer.gapUs = g;
    Serial.printf("gap %u us\n", framer.gapUs);
  } else if (!strncmp(s, "learn ", 6)) {
    strncpy(waiting, s + 6, sizeof waiting - 1);
    waiting[sizeof waiting - 1] = 0;
    Serial.printf("Press '%s' on the remote now.\n", waiting);
  } else if (!strncmp(s, "cmd ", 4)) {
    char *end;
    long c = strtol(s + 4, &end, 16);
    if (end == s + 4 || *end || c < 0 || c > 255) Serial.println("use: cmd 83");
    else sendAndReport(c, 5);
  } else if (!strncmp(s, "addr ", 5)) {
    long a = strtol(s + 5, nullptr, 16);
    if (a >= 0 && a <= 0xFFF) sendAddr = a;
    Serial.printf("sending as remote %03X\n", sendAddr);
  } else if (!strcmp(s, "next") || !strcmp(s, "prev")) {
    if (lastSent < 0) Serial.println("send something first, e.g. cmd 80");
    else sendAndReport((uint8_t)(lastSent + (s[0] == 'n' ? 1 : -1)), 5);
  } else if (!strncmp(s, "scan ", 5)) {
    unsigned a, b, ms = 1500;
    if (sscanf(s + 5, "%x %x %u", &a, &b, &ms) < 2 || a > 255 || b > 255 || a > b || ms > 60000)
      Serial.println("use: scan 80 8F 2000");
    else scan(a, b, ms);
  } else if (!strcmp(s, "levels")) levels();
  else if (!strncmp(s, "trace ", 6)) {
    unsigned ms, st = 100;
    if (sscanf(s + 6, "%u %u", &ms, &st) < 1 || ms > 60000 || st < 10 || st > 1000) Serial.println("use: trace 5000 100");
    else trace(ms, st);
  }
  else if (!strncmp(s, "fade ", 5)) {
    unsigned a, b;
    if (sscanf(s + 5, "%u %u", &a, &b) != 2 || a > 15 || b > 15) Serial.println("use: fade 0 8");
    else fade(a, b);
  }
  else if (!strcmp(s, "speed")) speedTest();
  else if (!strcmp(s, "map")) mapCodes(0x00, 0xFF);
  else if (!strncmp(s, "map ", 4)) {
    unsigned a, b;
    if (sscanf(s + 4, "%x %x", &a, &b) != 2 || a > b || b > 255) Serial.println("use: map 00 3F");
    else mapCodes(a, b);
  } else if (!strncmp(s, "watch ", 6)) {
    unsigned a, b, ms = 1500;
    if (sscanf(s + 6, "%x %x %u", &a, &b, &ms) < 2 || a > b || b > 255 || ms > 20000)
      Serial.println("use: watch 40 7F");
    else watch(a, b, ms);
  } else if (!strncmp(s, "frames ", 7)) {
    int f = atoi(s + 7);
    if (f >= 2 && f <= 9) animFrames = f;
    Serial.printf("%u frame(s) per animation step\n", animFrames);
  } else if (!strncmp(s, "anim", 4)) {
    char what[12] = "";
    unsigned ms = 300;
    sscanf(s + 4, "%11s %u", what, &ms);
    anim(what, ms);
  } else if (!strncmp(s, "hold ", 5)) {
    char name[16];
    unsigned ms = 0;
    int c = sscanf(s + 5, "%15s %u", name, &ms) == 2 ? remoteParse(name) : -1;
    if (c < 0 || ms > 20000) Serial.println("use: hold b- 2000");
    else sendAndReport(c, 3 + ms / 62);
  } else {
    int c = remoteParse(s);
    if (c >= 0) sendAndReport(c, 5);
    else Serial.println("? type help");
  }
}

static void onFrame() {
  uint32_t now = millis();
  if (rfDecode(frame, code)) {
    remoteNormalize(code);
    // A code counts only when it arrives twice in a row; noise doesn't repeat.
    if (haveLast && rfSameCode(code, lastCode) && now - lastCodeMs < 400) sameCount++;
    else sameCount = 1;
    lastCode = code;
    haveLast = true;
    lastCodeMs = now;
    if (sameCount >= 2) {
      bool isNew = !haveShown || !rfSameCode(code, shown);
      if (isNew) {
        if (haveShown && !quiet) Serial.printf("   %u repeats\n", repeats);
        showCode(code);
        if (rawMode) printRaw(frame);
        if (waiting[0]) learn(code);
        shown = code;
        haveShown = true;
        repeats = 1;
        pressMs = now;
      }
      repeats++;
      lastGoodMs = now;
    }
  } else if (rfSameFrame(frame, lastFrame) && now - lastOddMs > 1000) {
    // Repeats exactly but fits no known coding: print it for study.
    Serial.println("Repeating signal, coding not recognised:");
    printRaw(frame);
    lastOddMs = now;
  }
  lastFrame = frame;
}

static void pump() {
  while (tail_ != head) {
    uint32_t v = ring[tail_];
    tail_ = (tail_ + 1) % RING;
    if (framer.feed(v & 1, v >> 1, frame)) onFrame();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  pinMode(RF_PIN, INPUT);
  pinMode(SEND_PIN, INPUT);
#if defined(ESP32)
  for (uint8_t i = 0; i < 3; i++) pinMode(OUT[i], INPUT_PULLDOWN);
#else
  for (uint8_t i = 0; i < 3; i++) pinMode(OUT[i], INPUT);
#endif
  attachInterrupt(digitalPinToInterrupt(RF_PIN), onEdge, CHANGE);
  Serial.println("\n433 MHz remote: read and send");
  help();
  stat();
}

void loop() {
  pump();
  if (haveShown && millis() - lastGoodMs > 300) {
    if (!quiet) Serial.printf("   released: %u repeats, %lu ms\n", repeats, (unsigned long)(lastGoodMs - pressMs));
    haveShown = false;
    haveLast = false;
  }
  static char line[48];
  static uint8_t n = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n') { line[n] = 0; n = 0; command(line); }
    else if (n < sizeof line - 1) line[n++] = ch;
  }
}
