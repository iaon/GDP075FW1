#pragma once
#include "Arduino.h"
class IPAddress {
 public:
  IPAddress(uint32_t value = 0) : value_(value) {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
      : value_(uint32_t(a) | (uint32_t(b) << 8) | (uint32_t(c) << 16) | (uint32_t(d) << 24)) {}
  uint8_t operator[](size_t index) const { return value_ >> (8 * index); }
  bool operator==(const IPAddress &other) const { return value_ == other.value_; }
  operator uint32_t() const { return value_; }
  String toString() const {
    char text[16]; snprintf(text, sizeof(text), "%u.%u.%u.%u", (*this)[0], (*this)[1], (*this)[2], (*this)[3]);
    return String(text);
  }
 private:
  uint32_t value_;
};
