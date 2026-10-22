#pragma once

#include <stdint.h>
#include <string.h>

namespace panel {

enum class RefreshMode { Full, FullFast, Window, WindowFastExperimental };

struct RefreshRequest {
  RefreshMode mode;
  uint16_t x, y, w, h;
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
  return a.mode == b.mode && a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

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
