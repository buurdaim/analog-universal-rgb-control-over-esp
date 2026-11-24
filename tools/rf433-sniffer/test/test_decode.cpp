// clang++ -std=c++11 -Wall -Wextra -I.. test_decode.cpp -o /tmp/t && /tmp/t
#include "rf_decode.h"
#include <cstdio>
#include <random>
#include <string>
#include <vector>

struct P { int level; int us; };
static std::mt19937 rng(7);
static int jit(int us, double pct, int skewHigh, int level) {
  std::uniform_real_distribution<double> d(1 - pct, 1 + pct);
  int v = (int)(us * d(rng)) + (level ? skewHigh : -skewHigh);
  return v < 40 ? 40 : v;
}
static int fails = 0;
static void check(bool ok, const std::string &what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) fails++;
}
static std::vector<RfCode> run(const std::vector<P> &s, uint16_t gap = 2200) {
  RfFramer fr; fr.gapUs = gap;
  RfFrame f; RfCode c; std::vector<RfCode> out;
  for (auto &p : s) if (fr.feed(p.level, p.us, f) && rfDecode(f, c)) out.push_back(c);
  return out;
}
static std::string bitsOf(const RfCode &c) {
  std::string s;
  for (int i = 0; i < c.bits; i++) s += (c.data[i / 8] & (0x80 >> (i % 8))) ? '1' : '0';
  return s;
}
static void noise(std::vector<P> &s, int n) {
  std::exponential_distribution<double> d(1.0 / 180);
  int lv = s.empty() ? 1 : !s.back().level;
  for (int i = 0; i < n; i++, lv = !lv) s.push_back({lv, 15 + (int)d(rng)});
  if (s.back().level) s.push_back({0, 200});
}
static void ev1527(std::vector<P> &s, const std::string &b, int T, int rep, double j, int skew) {
  for (int r = 0; r < rep; r++) {
    s.push_back({1, jit(T, j, skew, 1)}); s.push_back({0, jit(31 * T, j, skew, 0)});
    for (char ch : b) {
      s.push_back({1, jit(ch == '1' ? 3 * T : T, j, skew, 1)});
      s.push_back({0, jit(ch == '1' ? T : 3 * T, j, skew, 0)});
    }
  }
  s.push_back({1, jit(T, j, skew, 1)}); s.push_back({0, 31 * T});
}
static int countBits(const std::vector<RfCode> &v, const std::string &b, int type) {
  int n = 0;
  for (auto &c : v) if (c.type == type && bitsOf(c) == b) n++;
  return n;
}

int main() {
  const std::string A = "101000011011001011000100";
  for (int T : {120, 200, 320, 450, 600}) for (int skew : {-30, 0, 30}) {
    std::vector<P> s; noise(s, 400); ev1527(s, A, T, 8, 0.10, skew); noise(s, 400);
    auto v = run(s);
    check(countBits(v, A, RF_PWM) >= 7, "EV1527 T=" + std::to_string(T) + " skew " + std::to_string(skew) +
          " -> " + std::to_string(countBits(v, A, RF_PWM)) + "/8");
  }
  { std::vector<P> s; ev1527(s, std::string(24, '0'), 300, 4, 0.1, 0);
    check(countBits(run(s), std::string(24, '0'), RF_PWM) >= 3, "all zeros"); }
  { std::vector<P> s; ev1527(s, std::string(24, '1'), 300, 4, 0.1, 0);
    check(countBits(run(s), std::string(24, '1'), RF_PWM) >= 3, "all ones"); }
  { // one different bit must give a different code
    std::string B = A; B[23] = '1';
    std::vector<P> s; ev1527(s, A, 300, 4, 0.1, 0); ev1527(s, B, 300, 4, 0.1, 0);
    auto v = run(s);
    check(countBits(v, A, RF_PWM) >= 3 && countBits(v, B, RF_PWM) >= 3, "last bit tells two buttons apart"); }
  { // NEC-like: 9 ms / 4.5 ms header, constant high
    const std::string N = "00000000111111110100010110111010";
    std::vector<P> s; noise(s, 200);
    for (int r = 0; r < 5; r++) {
      s.push_back({1, 9000}); s.push_back({0, 4500});
      for (char ch : N) { s.push_back({1, jit(560, .1, 0, 1)}); s.push_back({0, jit(ch == '1' ? 1690 : 560, .1, 0, 0)}); }
      s.push_back({1, 560}); s.push_back({0, 40000});
    }
    check(countBits(run(s), N, RF_PULSEDIST) >= 4, "pulse-distance, 32 bits"); }
  { // high time only
    const std::string N = "1100101001110001";
    std::vector<P> s;
    for (int r = 0; r < 5; r++) {
      for (char ch : N) { s.push_back({1, jit(ch == '1' ? 900 : 300, .1, 0, 1)}); s.push_back({0, jit(400, .1, 0, 0)}); }
      s.push_back({1, 300}); s.push_back({0, 8000});
    }
    check(countBits(run(s), N, RF_PULSEWIDTH) >= 4, "pulse-width, 16 bits"); }
  { // inverted PWM: each bit is low first
    const std::string N = "1011001110001011";
    std::vector<P> s;
    for (int r = 0; r < 5; r++) {
      s.push_back({1, 400});
      for (char ch : N) { s.push_back({0, jit(ch == '1' ? 400 : 800, .08, 0, 0)}); s.push_back({1, jit(ch == '1' ? 800 : 400, .08, 0, 1)}); }
      s.push_back({0, 9000});
    }
    auto v = run(s);
    check(countBits(v, N, RF_PWM_INV) >= 4, "inverted PWM, 16 bits"); }
  { // Manchester
    const std::string N = "110100101100101000111010";
    std::vector<P> s;
    for (int r = 0; r < 5; r++) {
      std::vector<int> lv;
      for (char ch : N) { lv.push_back(ch == '1'); lv.push_back(ch != '1'); }
      std::vector<P> f;
      for (int x : lv) { if (!f.empty() && f.back().level == x) f.back().us += 400; else f.push_back({x, 400}); }
      for (auto &p : f) p.us = jit(p.us, .1, 0, p.level);
      if (f.back().level == 0) f.back().us += 8000; else f.push_back({0, 8000});
      s.insert(s.end(), f.begin(), f.end());
    }
    check(countBits(run(s), N, RF_MANCHESTER) >= 4, "Manchester, 24 bits"); }
  { // noise only: nothing may be confirmed twice in a row
    std::vector<P> s;
    std::exponential_distribution<double> sh(1.0 / 180), lg(1.0 / 2500);
    int lv = 1;
    for (int i = 0; i < 3000000; i++, lv = !lv) s.push_back({lv, 15 + (int)((!lv && rng() % 20 == 0) ? lg(rng) : sh(rng))});
    auto v = run(s);
    int conf = 0;
    for (size_t i = 1; i < v.size(); i++) if (rfSameCode(v[i], v[i - 1])) conf++;
    check(conf == 0, "3M noise pulses: " + std::to_string(v.size()) + " single decodes, " + std::to_string(conf) + " confirmed"); }
  { // milder noise, nothing under 60 us
    std::vector<P> s; std::uniform_int_distribution<int> d(150, 900);
    int lv = 1;
    for (int i = 0; i < 2000000; i++, lv = !lv) s.push_back({lv, (!lv && rng() % 25 == 0) ? 5000 : d(rng)});
    auto v = run(s); int conf = 0;
    for (size_t i = 1; i < v.size(); i++) if (rfSameCode(v[i], v[i - 1])) conf++;
    check(conf == 0, "2M slow noise pulses: " + std::to_string(v.size()) + " single decodes, " + std::to_string(conf) + " confirmed"); }
  printf("%s\n", fails ? "SOME FAILED" : "all passed");
  return fails != 0;
}
