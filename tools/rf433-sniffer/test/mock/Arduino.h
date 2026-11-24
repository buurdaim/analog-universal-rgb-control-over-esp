// Just enough Arduino to run the sketch on the Mac.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <string>
#define IRAM_ATTR
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLDOWN 2
#define LOW 0
#define HIGH 1
#define CHANGE 0
extern uint64_t nowUs; extern int pinLevel;
inline unsigned long micros() { return (unsigned long)nowUs; }
inline unsigned long millis() { return (unsigned long)(nowUs / 1000); }
inline void delay(int ms) { nowUs += ms * 1000ull; }
int mockOut(int pin);
inline int digitalRead(int p) { if (p == 32 || p == 33 || p == 25) { nowUs += 1; return mockOut(p); } return pinLevel; }
inline long random(long n) { return rand() % n; }
extern int sendMode; void mockWrite(int v);
void mockRelease();
inline void pinMode(int p, int m) { if (p == 27) { if (sendMode == OUTPUT && m == INPUT) mockRelease(); sendMode = m; } }
inline void digitalWrite(int p, int v) { if (p == 27) mockWrite(v); }
inline void delayMicroseconds(uint32_t us) { nowUs += us; }
inline int digitalPinToInterrupt(int p) { return p; }
inline void attachInterrupt(int, void (*)(), int) {}
struct Ser {
  std::string in;
  void begin(long) {}
  uint64_t autoEnter = 0;                // press Enter by itself at this time
  int available() { if (autoEnter && nowUs > autoEnter) { autoEnter = 0; in += "\n"; } return in.size(); }
  int read() { int c = in[0]; in.erase(0, 1); return c; }
  void print(const char *s) { fputs(s, stdout); }
  void print(char c) { putchar(c); }
  void println(const char *s = "") { puts(s); }
  void printf(const char *f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); }
};
extern Ser Serial;
