// clang++ -std=c++11 -Imock -I.. test_sketch.cpp -o /tmp/s && /tmp/s
#include <Arduino.h>
#include <random>
uint64_t nowUs = 0; int pinLevel = 0; Ser Serial;
int sendMode = 0; static bool jumper = true; static int sendLevel = 0;
void onEdge();
// --- simulated U4 ---
static int u4Cmd = 0x03; static bool u4On = true; static bool wired = true; static int duty[3]; static double cur[3]; static uint64_t curT = 0;
static uint64_t u4Last = 0; static int u4Prev = -1, u4Need = 2, u4Run = 0;
static void u4Edge(int v);
int mockOut(int pin) {
  if (!wired) return 0;
  int i = pin == 32 ? 0 : pin == 33 ? 1 : 2;
  double dt = (nowUs - curT) * 0.0009; curT = nowUs;
  for (int k = 0; k < 3; k++) { double t = u4On ? duty[k] : 0; if (cur[k] < t) cur[k] = cur[k] + dt > t ? t : cur[k] + dt; else cur[k] = cur[k] - dt < t ? t : cur[k] - dt; }
  return (int)(nowUs % 1000) < (int)cur[i];
}
void mockWrite(int v) { // jumper from GPIO 27 to GPIO 34
  if (v == sendLevel) return;
  sendLevel = v;
  if (jumper && sendMode == OUTPUT) { pinLevel = v; onEdge(); u4Edge(v); }
}
#include "../rf433-sniffer.ino"
static void u4Set(int c) {
  if (c == CMD_ON) u4On = true; else if (c == CMD_OFF) u4On = false;
  if (c < 0x40 && (c & 3) == 3 && c != 0x0B && c != 0x33) { u4Cmd = c; int h = c * 6; for (int i = 0; i < 3; i++) { int d = abs(((h + 128 * i) % 384) - 192) * 1000 / 192; duty[i] = d > 333 ? (d - 333) * 3 / 2 : 0; } }
  if (c == 0x40) for (int i = 0; i < 3; i++) duty[i] = 1000;
}
static RfFramer u4Framer;
static void u4Edge(int v);
void mockRelease() { u4Edge(1); }
static void u4Edge(int v) {
  static RfFrame f; RfCode c; RemoteCode r;
  uint64_t us = nowUs - u4Last; u4Last = nowUs;
  if (u4Framer.feed(!v, us, f) && rfDecode(f, c)) {
    remoteNormalize(c);
    if (remoteRead(c, r) && r.address == 0xC81) {
      u4Run = r.command == u4Prev ? u4Run + 1 : 1; u4Prev = r.command;
      if (u4Run >= u4Need) u4Set(r.command);
    }
  }
}
static std::mt19937 rng(3);
static void pulse(int level, int us) { // level lasts us, then flips
  pinLevel = level; nowUs += us; pinLevel = !level; onEdge(); loop();
}
static void noise(int ms) {
  uint64_t end = nowUs + ms * 1000ull; std::exponential_distribution<double> d(1.0 / 150);
  while (nowUs < end) pulse(pinLevel, 20 + (int)d(rng));
  if (pinLevel) pulse(1, 100);
}
static void press(uint32_t code, int T, int rep) {
  noise(600);
  for (int r = 0; r < rep; r++) {
    pulse(1, T); pulse(0, 31 * T);
    for (int i = 23; i >= 0; i--) { bool b = code >> i & 1; pulse(1, b ? 3 * T : T); pulse(0, b ? T : 3 * T); }
  }
  pulse(1, T); pulse(0, 31 * T);
  noise(600);
}
// The real remote: address C81, 3 back-to-back frames, then held frames.
static void remote(uint8_t cmd, int held) {
  uint32_t bits = 0xC81;
  for (int i = 7; i >= 0; i--) bits = bits << 2 | ((cmd >> i & 1) ? 3 : 0);
  noise(300);
  for (int r = 0; r < 3 + held; r++) {
    pulse(1, 310); pulse(0, 5230);
    for (int i = 27; i >= 0; i--) { bool b = bits >> i & 1; pulse(1, b ? 1195 : 310);
      pulse(0, (b ? 310 : 1195) + (i == 0 && r >= 2 ? 14200 : 0)); }
  }
  pulse(1, 310); pulse(0, 5230);
  noise(600);
}
static void type(const char *s) { Serial.in += s; Serial.in += "\n"; loop(); }
int main() {
  setup();
  type("learn red");   press(0xA1B201, 320, 6);
  type("learn green"); press(0xA1B202, 320, 6);
  type("learn off\r"); press(0xA1B2F0, 320, 6);
  press(0xA1B202, 320, 4);
  type("list");
  type("clear");
  puts("--- real remote ---");
  type("learn onoff"); remote(0x83, 20);
  remote(0x84, 0);
  remote(0x86, 5); remote(0x3B, 2); remote(0x00, 3); remote(0xFF, 3);
  remote(0x83, 0);
  type("list"); type("seen");
  puts("--- sending, with the jumper ---");
  for (const char *c : {"on", "off", "b+", "brightness-", "s+", "s-", "m+", "m-", "w0", "w15", "w16", "w", "cmd 00", "cmd FF", "cmd 5A", "cmd zz", "next", "prev", "hold b- 2000", "hold xx 5", "addr 123", "on", "addr C81", "scan 80 83 100", "scan 9 1", "blah"}) { printf("> %s\n", c); type(c); nowUs += 500000; loop(); }
  puts("--- outputs ---"); u4Set(0x03);
  for (const char *c : {"levels", "w5", "levels", "c64", "map 00 44", "map 82 86", "fade 0 8", "fade 1 99", "speed", "frames 1", "frames 12", "frames 3"}) { printf("> %s\n", c); type(c); nowUs += 500000; loop(); }
  puts("--- not wired ---"); wired = false; type("map 00 02"); wired = true;
  puts("--- anim ---"); type("anim blah"); type("frames 2");
  for (const char *c : {"anim rainbow 300", "anim random 0", "anim random 200", "anim strobe 500", "anim breathe 200"}) { Serial.autoEnter = nowUs + 6000000; printf("> %s\n", c); type(c); nowUs += 500000; loop(); loop(); }
  printf("U4 on at the end: %d\n", u4On);
  puts("--- watch ---");
  Serial.autoEnter = nowUs + 5200000; type("watch 3E 44 1500");
  Serial.in = "watch 80 FF 1500\nq\n"; loop(); loop();
  type("watch 9 1");
  puts("--- without the jumper ---");
  jumper = false; type("on"); jumper = true;
  int bad = 0; for (int c = 0; c < 256; c++) { sendCode(c, 5); if (heard != c) bad++; nowUs += 400000; loop(); }
  printf("all 256 codes sent and read back, wrong: %d\n", bad);
  remote(0x84, 3);
  noise(120000);
  puts("(2 minutes of noise done)");
}
