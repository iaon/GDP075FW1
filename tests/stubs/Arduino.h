#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>

constexpr int LOW = 0, HIGH = 1, MSBFIRST = 1, SPI_MODE0 = 0;
constexpr int INPUT = 1, OUTPUT = 3;
class String {
 public:
  String(const char *text = "") : value(text) {}
  String(const std::string &text) : value(text) {}
  bool isEmpty() const { return value.empty(); }
  size_t length() const { return value.size(); }
  const char *c_str() const { return value.c_str(); }
  bool operator==(const char *text) const { return value == text; }
  void toCharArray(char *out, size_t size) const { snprintf(out, size, "%s", c_str()); }
  std::string value;
};
namespace testhw {
struct Event { enum Kind { Begin, Pin, Byte, End, Mode, BusInit } kind; int pin; unsigned value; };
extern uint64_t clockMs;
extern int pins[40];
extern bool busyStuck;
extern int stuckOnCommand;
extern unsigned yields;
extern std::vector<Event> events;
void reset();
}
inline uint32_t millis() { return uint32_t(testhw::clockMs); }
inline void delay(unsigned ms) { testhw::clockMs += ms; }
inline void yield() { ++testhw::yields; }
inline int digitalRead(int pin) {
  return pin == 13 && testhw::busyStuck ? LOW : testhw::pins[pin];
}
inline void digitalWrite(int pin, int value) {
  testhw::pins[pin] = value;
  testhw::events.push_back({testhw::Event::Pin, pin, unsigned(value)});
}
inline void pinMode(int pin, int mode) { testhw::events.push_back({testhw::Event::Mode, pin, unsigned(mode)}); }
inline uint8_t pgm_read_byte(const uint8_t *data) { return *data; }
class FakeSerial {
 public:
  void begin(uint32_t) {}
  int available() const { return input.size(); }
  int read() { int value = input.front(); input.pop_front(); return value; }
  void print(const char *text) { output += text; }
  void print(char value) { output += value; }
  void println() { output += '\n'; }
  void println(const char *text) { output += text; output += '\n'; }
  template <typename... Args> void printf(const char *format, Args... args) {
    char buffer[512]; snprintf(buffer, sizeof(buffer), format, args...); output += buffer;
  }
  void feed(const std::vector<uint8_t> &bytes) { input.insert(input.end(), bytes.begin(), bytes.end()); }
  void feed(const char *text) { input.insert(input.end(), text, text + strlen(text)); }
  std::deque<uint8_t> input;
  std::string output;
};
extern FakeSerial Serial;
struct Restarted {};
struct FakeEsp { unsigned restarts = 0; void restart() { ++restarts; throw Restarted{}; } };
extern FakeEsp ESP;
