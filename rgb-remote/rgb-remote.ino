// RGB Remote: the ESP32 plays the LED controller's own remote, over Wi-Fi.
//
// Wiring: U4 pin 4 to GPIO 34, a jumper from GPIO 34 to GPIO 27, GND to GND.
// The controller (U4) keeps driving the MOSFETs. The real remote still works.
// Optional, for checking what the LEDs do: MOSFET gates R, G, B to GPIO 32, 33, 25.
//
// First use: the board opens its own Wi-Fi "RGB-Remote-XXXX", password
// "rgbremote". Join it and open http://192.168.4.1, or use the app.
// Console (115200): status, wifi clear, restart

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "rf_decode.h"
#include "rf_remote.h"
#include "colors.h"
#include "effects.h"
#include "page.h"

#define FW "1.0"
#define RF_PIN   34
#define SEND_PIN 27
static const uint8_t OUT[3] = {32, 33, 25};

// ---------- state ----------
struct Light {
  int8_t on = -1;          // -1 not known yet: U4 never answers
  int8_t color = -1;       // index into COLORS
  int8_t bright = -1;      // 0..15, -1 not known yet
  uint8_t effect = FX_NONE;
  uint8_t speed = 50;
  bool u4mode = false;     // one of U4's own modes is running
  int8_t mode = -1;        // which, 0..10; U4 keeps it even while a color shows
  int8_t wantBright = -1;  // the sender is moving towards this
};
static Light light;
static uint8_t custom[CUSTOM_MAX] = {0, 5, 10}, customCount = 3;
static uint32_t changes = 1;             // counts every change, so the app can tell
static uint32_t echoOk = 0, remotePresses = 0;
static char deviceName[24], hostName[24];
static Preferences prefs;

// ---------- edges from the receiver line ----------
#define RING 2048
static volatile uint32_t ring[RING];
static volatile uint16_t head = 0, tail_ = 0;
static volatile uint32_t lastEdge = 0;

void IRAM_ATTR onEdge() {
  uint32_t now = micros(), us = now - lastEdge;
  lastEdge = now;
  if (us > 1000000) us = 1000000;
  uint8_t ended = !digitalRead(RF_PIN);
  uint16_t next = (head + 1) % RING;
  if (next == tail_) return;
  ring[head] = (us << 1) | ended;
  head = next;
}

// ---------- sending ----------
// When each code last went out, to know our own echo. The last frame of a press is
// only complete when the line next changes, so its echo can come with the next press.
static volatile uint32_t lastSent[256];
static volatile uint32_t sentAt = 0, heardSeq = 0, sentSeq = 0;

// Busy-waits, so the code must run in the sender task and nowhere else. The pin
// is driven only here; otherwise it floats and the real remote works.
static void sendCode(uint8_t cmd, uint8_t frames) {
  uint16_t us[SEND_PULSES];
  remoteFrame(REMOTE_ADDRESS, cmd, us);
  digitalWrite(SEND_PIN, LOW);
  pinMode(SEND_PIN, OUTPUT);
  delayMicroseconds(6000);
  for (uint8_t f = 0; f < frames; f++) {
    sentAt = millis();
    lastSent[cmd] = sentAt | 1;
    uint32_t t = micros();
    for (uint8_t i = 0; i < SEND_PULSES; i++) {
      digitalWrite(SEND_PIN, i % 2 ? LOW : HIGH);
      t += us[i];
      int32_t left = (int32_t)(t - micros());
      if (left > 0) delayMicroseconds(left);
    }
    // A long press leaves the remote's own pauses, and lets the rest run.
    if (f >= 2 && frames > 6) vTaskDelay(pdMS_TO_TICKS(14));
  }
  delayMicroseconds(8000);
  pinMode(SEND_PIN, INPUT);
  sentAt = millis();
  sentSeq = sentSeq + 1;
}

// ---------- the sender task ----------
struct Job { uint8_t cmd; uint8_t frames; };
static QueueHandle_t jobs;
static volatile bool fxRestart = false;

// Measured: 2 frames with 180 ms of silence is one step; with 150 ms U4 takes it for
// a held button. Held, it steps about every second frame after the first ten.
static uint16_t pressGap = 200;
static uint8_t stepFrames = 2;
#define HOLD_FRAMES 50                   // enough to reach either end
static void changed() { changes = changes + 1; }

// One brightness press towards the wanted step. U4 stops at both ends, so from
// an unknown step the button is held until the nearer end first.
static bool brightnessStep() {
  int8_t want = light.wantBright;
  if (want < 0) return false;
  if (light.bright < 0) {
    bool up = want > 7;
    sendCode(up ? CMD_BRIGHT_UP : CMD_BRIGHT_DOWN, HOLD_FRAMES);
    light.bright = up ? BRIGHT_MAX : 0;
    changed();
    return true;
  }
  if (light.bright == want) { light.wantBright = -1; return false; }
  bool up = want > light.bright;
  sendCode(up ? CMD_BRIGHT_UP : CMD_BRIGHT_DOWN, stepFrames);
  light.bright += up ? 1 : -1;
  changed();
  return true;
}

static volatile int8_t wantColor = -1;   // the newest color asked for; older ones are skipped

static void senderTask(void *) {
  uint32_t n = 0, next = 0;
  int last = -1;
  for (;;) {
    Job j;
    if (xQueueReceive(jobs, &j, pdMS_TO_TICKS(5)) == pdTRUE) {
      sendCode(j.cmd, j.frames);
      int i = colorIndex(j.cmd);
      if (i >= 0 && light.effect == FX_NONE) light.color = i;
      vTaskDelay(pdMS_TO_TICKS(pressGap));
      next = millis();
      continue;
    }
    int8_t wc = wantColor;
    if (wc >= 0) {
      wantColor = -1;
      sendCode(COLORS[wc].code, 3);
      light.color = wc;
      vTaskDelay(pdMS_TO_TICKS(40));
      continue;
    }
    if (brightnessStep()) { vTaskDelay(pdMS_TO_TICKS(pressGap)); continue; }
    if (fxRestart) { fxRestart = false; n = 0; next = millis(); last = -1; }
    uint8_t fx = light.effect;
    if (fx == FX_NONE || light.on == 0 || (int32_t)(millis() - next) < 0) continue;
    EffectStep st = effectStep(fx, n++, esp_random(), light.speed, last, custom, customCount);
    sendCode(st.command, 2);
    int i = colorIndex(st.command);
    if (i >= 0) { last = i; if (light.effect == fx) light.color = i; }
    if (st.brightness && light.bright >= 0) {
      int b = light.bright + (st.command == CMD_BRIGHT_UP ? 1 : -1);
      light.bright = b < 0 ? 0 : b > BRIGHT_MAX ? BRIGHT_MAX : b;
    }
    next = millis() + (st.ms > 110 ? st.ms - 110 : 20);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void press(uint8_t cmd, uint8_t frames = 4) {
  Job j = {cmd, frames};
  xQueueSend(jobs, &j, 0);
}

// ---------- what the app and the pages ask for ----------
static void setOn(bool on) {
  if (!on) light.effect = FX_NONE;
  press(on ? CMD_ON : CMD_OFF);
  light.on = on;
  changed();
}
static void stopEffect() {
  if (light.effect == FX_BREATHE) light.wantBright = BRIGHT_MAX;
  if (light.effect == FX_STROBE) press(CMD_ON);   // it may have stopped while dark
  light.effect = FX_NONE;
}
static void setColor(uint8_t i) {
  if (light.on != 1) setOn(true);
  stopEffect();
  light.u4mode = false;
  light.color = i;
  wantColor = i;
  changed();
}
static void setEffect(uint8_t fx) {
  if (fx == FX_NONE) { stopEffect(); changed(); return; }
  if (light.on != 1) setOn(true);
  stopEffect();
  light.u4mode = false;
  if (fx == FX_BREATHE) { light.wantBright = BRIGHT_MAX; if (light.color >= 0) wantColor = light.color; }
  if (fx == FX_STROBE && light.color >= 0) wantColor = light.color;
  light.effect = fx;
  fxRestart = true;
  changed();
}
// A flash write stops both cores for a moment and would spoil a code on its way out,
// so settings are saved once the sender has been quiet for a while.
static volatile uint32_t saveAt = 0;
static void saveMode() { saveAt = millis() + 2000; }
static void saveLoop() {
  if (!saveAt || (int32_t)(millis() - saveAt) < 0) return;
  bool busy = uxQueueMessagesWaiting(jobs) || light.wantBright >= 0 || wantColor >= 0 || millis() - sentAt < 1000;
  if (busy || (light.effect != FX_NONE && light.on != 0)) { saveAt = millis() + 500; return; }
  saveAt = 0;
  if (prefs.getChar("mode", -1) != light.mode) prefs.putChar("mode", light.mode);
  uint8_t was[CUSTOM_MAX];
  size_t n = prefs.getBytes("custom", was, sizeof was);
  if (n != customCount || memcmp(was, custom, n)) prefs.putBytes("custom", custom, customCount);
}

// What one mode press does to U4's place: after a color the first press only goes
// back to it, every later one moves by one. Measured.
static void trackMode(bool up) {
  if (light.u4mode && light.mode >= 0)
    light.mode = (light.mode + (up ? 1 : U4_MODE_COUNT - 1)) % U4_MODE_COUNT;
  light.u4mode = true;
  light.color = -1;
  saveMode();
}

// One mode button.
static void u4Step(bool up) {
  if (light.on != 1) setOn(true);
  stopEffect();
  wantColor = -1;
  press(up ? CMD_MODE_UP : CMD_MODE_DOWN, stepFrames);
  trackMode(up);
  changed();
}

// Straight to one mode. False while the place isn't known.
static bool u4Select(uint8_t mode) {
  if (light.mode < 0) return false;
  if (light.on != 1) setOn(true);
  stopEffect();
  wantColor = -1;
  if (!light.u4mode) press(CMD_MODE_UP, stepFrames);       // back to its place
  int n = modePresses(light.mode, mode);
  for (int k = 0; k < abs(n); k++) press(n > 0 ? CMD_MODE_UP : CMD_MODE_DOWN, stepFrames);
  light.u4mode = true;
  light.mode = mode;
  light.color = -1;
  saveMode();
  changed();
  return true;
}

// The real remote was used.
static void remotePressed(uint8_t cmd) {
  remotePresses++;
  light.wantBright = -1;
  wantColor = -1;
  int i = colorIndex(cmd);
  if (i >= 0) { light.effect = FX_NONE; light.u4mode = false; light.color = i; }
  switch (cmd) {
    case CMD_ON: light.on = 1; break;
    case CMD_OFF: light.on = 0; light.effect = FX_NONE; break;
    case CMD_BRIGHT_UP: case CMD_BRIGHT_DOWN:
      light.effect = light.effect == FX_BREATHE ? (uint8_t)FX_NONE : light.effect;
      light.bright = -1;                 // how long it was held isn't known
      break;
    case CMD_MODE_UP: case CMD_MODE_DOWN:
      light.effect = FX_NONE;
      trackMode(cmd == CMD_MODE_UP);
      break;
    case CMD_SPEED_UP: case CMD_SPEED_DOWN: break;
  }
  changed();
}

// ---------- reading the line ----------
static RfFramer framer;
static RfFrame frame;
static RfCode code, lastCode;
static bool haveLast = false;
static uint8_t sameCount = 0;
static uint32_t lastCodeMs = 0;
static int heldCmd = -1;

static void onFrame() {
  RemoteCode rc;
  if (!rfDecode(frame, code)) return;
  remoteNormalize(code);
  if (!remoteRead(code, rc) || rc.address != REMOTE_ADDRESS) return;
  uint32_t now = millis();
  bool again = haveLast && rfSameCode(code, lastCode) && now - lastCodeMs < 400;
  sameCount = again ? sameCount + (sameCount < 200) : 1;
  lastCode = code;
  haveLast = true;
  lastCodeMs = now;
  if (sameCount < 2) return;
  if (lastSent[rc.command] && now - lastSent[rc.command] < 1500) {   // our own code coming back
    if (heardSeq != sentSeq) { heardSeq = sentSeq; echoOk++; }
    return;
  }
  if (sameCount == 2) { heldCmd = rc.command; remotePressed(rc.command); }
}

static void pump() {
  while (tail_ != head) {
    uint32_t v = ring[tail_];
    tail_ = (tail_ + 1) % RING;
    if (framer.feed(v & 1, v >> 1, frame)) onFrame();
  }
}

// ---------- Wi-Fi ----------
static WebServer web(80);
static DNSServer dns;
static String ssid, pass;
static bool apOn = false, mdnsOn = false;
static uint32_t tryAt = 0, lostAt = 0;

static void startAp() {
  if (apOn) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(deviceName, "rgbremote");
  dns.start(53, "*", WiFi.softAPIP());
  apOn = true;
  Serial.printf("own Wi-Fi: %s, password rgbremote, http://%s\n", deviceName, WiFi.softAPIP().toString().c_str());
}
static void stopAp() {
  if (!apOn) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apOn = false;
}
static void joinWifi() {
  if (ssid.length()) WiFi.begin(ssid.c_str(), pass.c_str());
  tryAt = millis();
}
static void wifiLoop() {
  bool up = WiFi.status() == WL_CONNECTED;
  static bool was = false;
  if (up && !was) {
    Serial.printf("on Wi-Fi: http://%s or http://%s.local\n", WiFi.localIP().toString().c_str(), hostName);
    if (!mdnsOn && MDNS.begin(hostName)) {
      MDNS.setInstanceName(deviceName);
      MDNS.addService("rgbremote", "tcp", 80);
      MDNS.addServiceTxt("rgbremote", "tcp", "fw", FW);
      MDNS.addServiceTxt("rgbremote", "tcp", "name", (const char *)deviceName);
      mdnsOn = true;
    }
    lostAt = millis();
  }
  if (!up && was) { lostAt = millis(); if (mdnsOn) { MDNS.end(); mdnsOn = false; } }
  was = up;
  if (up) {
    // Leave the own network up a little, so the app that did the setup hears back.
    if (apOn && millis() - lostAt > 15000) stopAp();
  } else {
    if (!ssid.length() || millis() - lostAt > 25000) startAp();
    // A retry makes the own network stutter, so not while someone is on it.
    bool busy = apOn && WiFi.softAPgetStationNum() > 0;
    if (ssid.length() && !busy && millis() - tryAt > 20000) { WiFi.disconnect(); joinWifi(); }
  }
  if (apOn) dns.processNextRequest();
}

// ---------- the HTTP side ----------
static bool viaAp() { return apOn && web.client().localIP() == WiFi.softAPIP(); }

static String esc(const String &s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += c;
  }
  return o;
}

static String stateJson() {
  String s = "{\"name\":\"" + String(deviceName) + "\",\"fw\":\"" FW "\",\"changes\":" + changes;
  s += ",\"on\":" + String(light.on) + ",\"color\":" + String(light.color) + ",\"bright\":" + String(light.wantBright >= 0 ? light.wantBright : light.bright);
  s += ",\"effect\":\"" + String(EFFECT_NAMES[light.effect]) + "\",\"speed\":" + String(light.speed);
  s += ",\"u4mode\":" + String(light.u4mode ? "true" : "false");
  s += ",\"mode\":\"" + String(light.mode >= 0 ? U4_MODES[light.mode] : "") + "\",\"custom\":[";
  for (uint8_t i = 0; i < customCount; i++) s += (i ? "," : "") + String(custom[i]);
  s += "],\"sent\":" + String(sentSeq) + ",\"heard\":" + String(echoOk) + ",\"remote\":" + String(remotePresses);
  s += ",\"wifi\":\"" + esc(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("")) + "\",\"ip\":\"" +
       (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("")) + "\",\"rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  return s + "}";
}
static void sendState() { web.sendHeader("Cache-Control", "no-store"); web.send(200, "application/json", stateJson()); }
static void bad(const char *why) { web.send(400, "text/plain", why); }

// A page on another site must not be able to switch the lights from a browser.
static bool foreignPage() {
  String o = web.header("Origin");
  if (!o.length() || o == "null") return o == "null";
  o.replace("http://", "");
  return o != web.hostHeader();
}

static void handleSet() {
  if (foreignPage()) { web.send(403, "text/plain", "other origin"); return; }
  if (web.hasArg("speed")) {
    int v = web.arg("speed").toInt();
    if (v < 1 || v > 100) return bad("speed 1-100");
    light.speed = v; changed();
  }
  if (web.hasArg("custom")) {
    uint8_t list[CUSTOM_MAX], n = 0;
    String a = web.arg("custom");
    for (int at = 0; at < (int)a.length() && n < CUSTOM_MAX;) {
      int c = a.indexOf(',', at);
      if (c < 0) c = a.length();
      int v = a.substring(at, c).toInt();
      if (v < 0 || v >= COLOR_COUNT) return bad("custom: colors 0-14");
      list[n++] = v;
      at = c + 1;
    }
    if (n < 2) return bad("custom: at least 2 colors");
    memcpy(custom, list, n);
    customCount = n;
    saveMode();
    changed();
  }
  if (web.hasArg("on")) {
    String v = web.arg("on");
    if (v == "1") setOn(true);
    else if (v == "0") setOn(false);
    else if (v == "toggle") setOn(light.on != 1);
    else return bad("on 0, 1 or toggle");
  }
  if (web.hasArg("bright")) {
    int v = web.arg("bright").toInt();
    if (v < 0 || v > BRIGHT_MAX) return bad("bright 0-15");
    if (light.effect == FX_BREATHE) light.effect = FX_NONE;
    light.wantBright = v; changed();
  }
  if (web.hasArg("color")) {
    int v = web.arg("color").toInt();
    if (v < 0 || v >= COLOR_COUNT || !isdigit(web.arg("color")[0])) return bad("color 0-14");
    setColor(v);
  } else if (web.hasArg("rgb")) {
    char *end;
    String a = web.arg("rgb");
    long v = strtol(a.c_str(), &end, 16);
    if (a.length() != 6 || *end) return bad("rgb RRGGBB");
    setColor(nearestColor(v >> 16, v >> 8, v));
  }
  if (web.hasArg("effect")) {
    int fx = effectByName(web.arg("effect").c_str());
    if (fx < 0) return bad("no such effect");
    setEffect(fx);
  }
  if (web.hasArg("mode")) u4Step(web.arg("mode") != "-");
  if (web.hasArg("u4")) {                  // one of U4's modes by name
    int m = -1;
    for (uint8_t k = 0; k < U4_MODE_COUNT; k++) if (web.arg("u4") == U4_MODES[k]) m = k;
    if (m < 0) return bad("no such mode");
    if (!u4Select(m)) { web.send(409, "text/plain", "mode place not known: calibrate first"); return; }
  }
  if (web.hasArg("calibrate")) {           // "the LEDs show this mode now"
    int m = -1;
    for (uint8_t k = 0; k < U4_MODE_COUNT; k++) if (web.arg("calibrate") == U4_MODES[k]) m = k;
    if (m < 0) return bad("no such mode");
    light.mode = m; light.u4mode = true; light.color = -1; saveMode(); changed();
  }
  if (web.hasArg("spd")) { press(web.arg("spd") == "-" ? CMD_SPEED_DOWN : CMD_SPEED_UP); changed(); }
  if (web.hasArg("press")) {               // one remote button by name
    int c = remoteParse(web.arg("press").c_str());
    if (c < 0) return bad("no such button");
    press(c); remotePressed(c);
  }
  sendState();
}

static void handleInfo() {
  String s = "{\"name\":\"" + String(deviceName) + "\",\"fw\":\"" FW "\",\"colors\":[";
  for (uint8_t i = 0; i < COLOR_COUNT; i++) {
    char b[40];
    snprintf(b, sizeof b, "%s[%u,%u,%u,%u]", i ? "," : "", COLORS[i].code, COLORS[i].r, COLORS[i].g, COLORS[i].b);
    s += b;
  }
  s += "],\"bright\":[";
  for (uint8_t i = 0; i < 16; i++) s += (i ? "," : "") + String(BRIGHT[i]);
  s += "],\"effects\":[";
  for (uint8_t i = 1; i < FX_COUNT; i++) s += String(i > 1 ? ",\"" : "\"") + EFFECT_NAMES[i] + "\"";
  s += "],\"modes\":[";
  for (uint8_t i = 0; i < U4_MODE_COUNT; i++) s += String(i ? ",\"" : "\"") + U4_MODES[i] + "\"";
  web.send(200, "application/json", s + "]}");
}

// What the LEDs really get, when the gate wires are on: percent for R, G, B.
static void handleLevels() {
  uint32_t hi[3] = {0, 0, 0}, n = 0, t = micros();
  while (micros() - t < 60000) { for (uint8_t c = 0; c < 3; c++) hi[c] += digitalRead(OUT[c]); n++; }
  char b[48];
  snprintf(b, sizeof b, "[%.1f,%.1f,%.1f]", hi[0] * 100.0 / n, hi[1] * 100.0 / n, hi[2] * 100.0 / n);
  web.send(200, "application/json", b);
}

static void handleScan() {
  if (!viaAp()) { web.send(403, "text/plain", "only on the board's own Wi-Fi"); return; }
  int n = WiFi.scanNetworks();
  String s = "[";
  for (int i = 0; i < n; i++) {
    bool twice = !WiFi.SSID(i).length();
    for (int k = 0; k < i; k++) twice |= WiFi.SSID(k) == WiFi.SSID(i);
    if (!twice) s += (s.length() > 1 ? ",\"" : "\"") + esc(WiFi.SSID(i)) + "\"";
  }
  WiFi.scanDelete();
  web.send(200, "application/json", s + "]");
}

// Only from the board's own Wi-Fi, so nobody on the home network can move it.
static void handleWifi() {
  if (!viaAp() || foreignPage()) { web.send(403, "text/plain", "only on the board's own Wi-Fi"); return; }
  String s = web.arg("ssid"), p = web.arg("pass");
  if (!s.length() || s.length() > 32 || p.length() > 63 || (p.length() && p.length() < 8)) return bad("check the name and password");
  ssid = s;
  pass = p;
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  web.send(200, "text/plain", "saved");
  Serial.printf("joining %s\n", ssid.c_str());
  lostAt = millis();
  WiFi.disconnect();
  joinWifi();
}

static void setupWeb() {
  static const char *want[] = {"Origin"};
  web.collectHeaders(want, 1);
  web.on("/", []() { if (viaAp()) web.send(200, "text/html", PAGE_SETUP); else web.send(200, "text/html", PAGE_HOME); });
  web.on("/control", []() { web.send(200, "text/html", PAGE_HOME); });
  web.on("/api/state", HTTP_GET, sendState);
  web.on("/api/info", HTTP_GET, handleInfo);
  web.on("/api/levels", HTTP_GET, handleLevels);
  web.on("/api/set", HTTP_POST, handleSet);
  web.on("/api/scan", HTTP_GET, handleScan);
  web.on("/api/wifi", HTTP_POST, handleWifi);
  web.onNotFound([]() {
    if (viaAp()) { web.sendHeader("Location", "http://192.168.4.1/"); web.send(302, "text/plain", ""); }
    else web.send(404, "text/plain", "not found");
  });
  web.begin();
}

// ---------- console ----------
static void console() {
  static char line[40];
  static uint8_t n = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch != '\n') { if (n < sizeof line - 1 && ch != '\r') line[n++] = ch; continue; }
    line[n] = 0;
    n = 0;
    if (!strcmp(line, "status")) {
      Serial.println(stateJson());
      Serial.printf("own Wi-Fi %s, joined %s, saved network: %s, heap %u, up %lu s\n", apOn ? "on" : "off",
                    WiFi.status() == WL_CONNECTED ? "yes" : "no", ssid.length() ? "yes" : "none",
                    ESP.getFreeHeap(), (unsigned long)(millis() / 1000));
    } else if (!strcmp(line, "wifi clear")) {
      prefs.remove("ssid"); prefs.remove("pass");
      ssid = pass = "";
      WiFi.disconnect();
      Serial.println("Wi-Fi forgotten");
    } else if (!strcmp(line, "restart")) ESP.restart();
    else if (!strncmp(line, "effect ", 7)) { int f = effectByName(line + 7); if (f >= 0) setEffect(f); Serial.println(f >= 0 ? "ok" : "no such effect"); }
    else if (!strncmp(line, "color ", 6)) { int c = atoi(line + 6); if (c >= 0 && c < COLOR_COUNT) setColor(c); Serial.println("ok"); }
    else if (!strncmp(line, "bright ", 7)) { int b = atoi(line + 7); if (b >= 0 && b <= BRIGHT_MAX) light.wantBright = b; Serial.println("ok"); }
    else if (!strncmp(line, "speed ", 6)) { int b = atoi(line + 6); if (b >= 1 && b <= 100) light.speed = b; Serial.println("ok"); }
    else if (!strncmp(line, "steps ", 6)) {     // steps <frames> <gap ms>: for measuring
      unsigned f, g;
      if (sscanf(line + 6, "%u %u", &f, &g) == 2 && f > 1 && f < 10) { stepFrames = f; pressGap = g; Serial.println("ok"); }
    }
    else if (!strcmp(line, "u4step +") || !strcmp(line, "u4step -")) { u4Step(line[7] == '+'); Serial.println("ok"); }
    else if (!strncmp(line, "u4 ", 3)) { int m = atoi(line + 3); Serial.println(m >= 0 && m < U4_MODE_COUNT && u4Select(m) ? "ok" : "not known"); }
    else if (!strncmp(line, "calibrate ", 10)) { int m = atoi(line + 10); if (m >= 0 && m < U4_MODE_COUNT) { light.mode = m; light.u4mode = true; light.color = -1; saveMode(); } Serial.println("ok"); }
    else if (!strncmp(line, "tap ", 4)) {       // tap <button> <frames> <count> <gap ms>: for measuring
      char b[12]; unsigned f, k, g;
      int c = sscanf(line + 4, "%11s %u %u %u", b, &f, &k, &g) == 4 ? remoteParse(b) : -1;
      if (c >= 0 && f >= 1 && f < 200 && k <= 30) { pressGap = g; while (k--) press(c, f); Serial.println("ok"); }
    }
    else if (!strncmp(line, "press ", 6)) { int c = remoteParse(line + 6); if (c >= 0) press(c); Serial.println(c >= 0 ? "ok" : "no such button"); }
    else if (!strcmp(line, "levels")) {
      uint32_t hi[3] = {0, 0, 0}, k = 0, t = micros();
      while (micros() - t < 60000) { for (uint8_t c = 0; c < 3; c++) hi[c] += digitalRead(OUT[c]); k++; }
      Serial.printf("levels %.1f %.1f %.1f\n", hi[0] * 100.0 / k, hi[1] * 100.0 / k, hi[2] * 100.0 / k);
    } else if (*line) Serial.println("status, wifi clear, restart, effect, color, bright, speed, press, levels");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(SEND_PIN, INPUT);
  pinMode(RF_PIN, INPUT);
  for (uint8_t i = 0; i < 3; i++) pinMode(OUT[i], INPUT_PULLDOWN);
  uint64_t mac = ESP.getEfuseMac();
  uint16_t id = (uint16_t)((mac >> 32) & 0xFF) << 8 | (uint16_t)((mac >> 40) & 0xFF);
  snprintf(deviceName, sizeof deviceName, "RGB-Remote-%04X", id);
  snprintf(hostName, sizeof hostName, "rgb-remote-%04x", id);
  prefs.begin("rgb", false);
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  size_t n = prefs.getBytes("custom", custom, sizeof custom);
  if (n >= 2) customCount = n;
  light.mode = prefs.getChar("mode", -1);
  if (light.mode >= U4_MODE_COUNT) light.mode = -1;
  Serial.printf("\n%s, firmware %s\n", deviceName, FW);
  attachInterrupt(digitalPinToInterrupt(RF_PIN), onEdge, CHANGE);
  jobs = xQueueCreate(32, sizeof(Job));
  xTaskCreatePinnedToCore(senderTask, "sender", 4096, nullptr, 2, nullptr, 1);
  WiFi.persistent(false);
  WiFi.setHostname(hostName);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  joinWifi();
  setupWeb();
}

void loop() {
  pump();
  web.handleClient();
  wifiLoop();
  console();
  saveLoop();
  delay(1);
}
