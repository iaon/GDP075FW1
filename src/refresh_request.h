#pragma once

#include <stdint.h>
#include <string.h>

namespace panel {

enum class RefreshMode { Full, FullFast, Window, WindowFastExperimental };

struct RefreshRequest {
  RefreshMode mode;
  uint16_t x, y, w, h;
  bool pllExperiment = false, extendedPll = false;
  bool recovery = false;
  uint8_t pll = 0x08;
  RefreshRequest(RefreshMode selected = RefreshMode::Full,
                 uint16_t left = 0, uint16_t top = 0,
                 uint16_t width = 800, uint16_t height = 480)
      : mode(selected), x(left), y(top), w(width), h(height) {}
};

inline bool isWindow(RefreshMode mode) {
  return mode == RefreshMode::Window || mode == RefreshMode::WindowFastExperimental;
}

inline bool isFast(RefreshMode mode) {
  return mode == RefreshMode::FullFast || mode == RefreshMode::WindowFastExperimental;
}

inline const char *modeName(RefreshMode mode) {
  switch (mode) {
    case RefreshMode::Full: return "full";
    case RefreshMode::FullFast: return "fast_full";
    case RefreshMode::Window: return "partial";
    case RefreshMode::WindowFastExperimental: return "fast_window_experimental";
  }
  return "invalid";
}

inline bool validWindow(const RefreshRequest &request) {
  // Check bounds before subtraction, and never silently round an ROI.
  return request.x < 800 && request.y < 480 && request.w >= 4 && request.h >= 2 &&
         request.w <= 800 - request.x && request.h <= 480 - request.y &&
         request.x % 4 == 0 && request.w % 4 == 0;
}

inline bool sameRequest(const RefreshRequest &a, const RefreshRequest &b) {
  return a.mode == b.mode && a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h &&
         a.pllExperiment == b.pllExperiment && a.extendedPll == b.extendedPll &&
         a.recovery == b.recovery && a.pll == b.pll;
}

// JD79665AA v1.0.0, 8.2.11 p36. Table values, never measured rates.
inline const char *nominalHz(uint8_t pll) {
  static const char *const rates[] = {"12.5", "25", "50", "65", "75", "85", "100", "120"};
  return rates[pll & 7];
}
inline bool allowedPll(uint8_t pll, bool extended) {
  if (pll & 0xf0) return false;
  return extended || pll == 0x08 || pll == 0x09 || pll == 0x0a ||
         pll == 0x02 || pll == 0x03 || pll == 0x04;
}

class PllParameters {
 public:
  void add(const char *name, const char *text) {
    if (error_) return;
    uint8_t bit = 0;
    if (!strcmp(name, "base")) bit = 1;
    else if (!strcmp(name, "pll")) bit = 2;
    else if (!strcmp(name, "extended")) bit = 4;
    else if (!strcmp(name, "x")) bit = 8;
    else if (!strcmp(name, "y")) bit = 16;
    else if (!strcmp(name, "w")) bit = 32;
    else if (!strcmp(name, "h")) bit = 64;
    else { error_ = "unexpected experiment parameter"; return; }
    if (seen_ & bit) { error_ = "duplicate experiment parameter"; return; }
    seen_ |= bit;
    if (!text) text = "";
    if (bit == 1) {
      if (!strcmp(text, "full")) request_.mode = RefreshMode::Full;
      else if (!strcmp(text, "fast-full")) request_.mode = RefreshMode::FullFast;
      else if (!strcmp(text, "partial")) request_.mode = RefreshMode::Window;
      else if (!strcmp(text, "fast-window")) request_.mode = RefreshMode::WindowFastExperimental;
      else error_ = "base must be full, fast-full, partial or fast-window";
    } else if (bit == 2) {
      if (strlen(text) != 4 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X') ||
          hex(text[2]) < 0 || hex(text[3]) < 0) error_ = "pll must be exactly 0xHH";
      else request_.pll = uint8_t(hex(text[2]) * 16 + hex(text[3]));
    } else if (bit == 4) {
      if (strcmp(text, "1")) error_ = "extended opt-in must be 1";
      else request_.extendedPll = true;
    } else {
      uint32_t value = 0;
      if (!*text) { error_ = "empty window parameter"; return; }
      for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9' || value > (65535u - (*p - '0')) / 10) {
          error_ = "window parameters must be decimal integers in 0..65535"; return;
        }
        value = value * 10 + (*p - '0');
      }
      if (bit == 8) request_.x = value;
      if (bit == 16) request_.y = value;
      if (bit == 32) request_.w = value;
      if (bit == 64) request_.h = value;
    }
  }
  const char *finish(RefreshRequest &out) {
    if (!error_ && (seen_ & 3) != 3) error_ = "base and pll are required";
    if (!error_ && !allowedPll(request_.pll, request_.extendedPll))
      error_ = "PLL not in initial allowlist; reserved bits forbidden; other 0x00..0x0f require extended=1";
    if (!error_ && isWindow(request_.mode) &&
        ((seen_ & 120) != 120 || !validWindow(request_))) error_ = "invalid or missing window";
    if (!error_ && !isWindow(request_.mode) && (seen_ & 120)) error_ = "window forbidden for full base";
    if (!error_) { request_.pllExperiment = true; out = request_; }
    return error_;
  }
 private:
  static int hex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
  }
  RefreshRequest request_;
  uint8_t seen_ = 0;
  const char *error_ = nullptr;
};

constexpr const char *kLutBlockedReason =
    "JD79665AA register-LUT command/payload length/phase format and validated panel profile unavailable; LUT_EN stays 0";

inline bool requestForPath(const char *path, RefreshRequest &request, bool &native) {
  request = RefreshRequest();
  native = true;
  if (!strcmp(path, "/frame")) native = false;
  else if (!strcmp(path, "/frame/native")) {}
  else if (!strcmp(path, "/frame/native/fast-full")) request.mode = RefreshMode::FullFast;
  else if (!strcmp(path, "/frame/native/partial")) request.mode = RefreshMode::Window;
  else if (!strcmp(path, "/frame/native/fast-window"))
    request.mode = RefreshMode::WindowFastExperimental;
  else return false;
  return true;
}

// Parse decimal query arguments without String::toInt's permissive truncation.
// A fresh instance is used at upload start and again before drawing.
class RefreshParameters {
 public:
  explicit RefreshParameters(RefreshMode mode) : request_(mode) {}
  void add(const char *name, const char *text) {
    if (error_) return;
    uint8_t bit = 0;
    uint16_t *value = nullptr;
    if (!strcmp(name, "x")) { bit = 1; value = &request_.x; }
    else if (!strcmp(name, "y")) { bit = 2; value = &request_.y; }
    else if (!strcmp(name, "w")) { bit = 4; value = &request_.w; }
    else if (!strcmp(name, "h")) { bit = 8; value = &request_.h; }
    if (!isWindow(request_.mode) || !bit) { error_ = "unexpected parameter"; return; }
    if (seen_ & bit) { error_ = "duplicate window parameter"; return; }
    seen_ |= bit;
    if (!text || !*text) { error_ = "window parameters must be decimal integers"; return; }
    uint32_t number = 0;
    for (const char *p = text; *p; ++p) {
      if (*p < '0' || *p > '9' || number > (65535u - (*p - '0')) / 10) {
        error_ = "window parameters must be decimal integers in 0..65535";
        return;
      }
      number = number * 10 + (*p - '0');
    }
    *value = static_cast<uint16_t>(number);
  }
  const char *finish(RefreshRequest &request) {
    if (!error_ && isWindow(request_.mode)) {
      if (seen_ != 15) error_ = "supply x, y, w and h in the query string";
      else if (!validWindow(request_))
        error_ = "invalid window: x/w multiples of 4, w>=4, h>=2, within 800x480";
    }
    if (!error_) request = request_;
    return error_;
  }

 private:
  RefreshRequest request_;
  uint8_t seen_ = 0;
  const char *error_ = nullptr;
};

}  // namespace panel
