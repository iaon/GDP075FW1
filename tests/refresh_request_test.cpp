#include "refresh_request.h"
#include <assert.h>
#include <stdio.h>
#include <string>

namespace {
panel::RefreshParameters window(const char *x, const char *y,
                                const char *w, const char *h,
                                panel::RefreshMode mode = panel::RefreshMode::Window) {
  panel::RefreshParameters parser(mode);
  if (x) parser.add("x", x);
  if (y) parser.add("y", y);
  if (w) parser.add("w", w);
  if (h) parser.add("h", h);
  return parser;
}
}

int main() {
  using panel::RefreshMode;
  using panel::RefreshRequest;
  struct Mapping { const char *path; RefreshMode mode; bool native; };
  const Mapping paths[] = {
      {"/frame", RefreshMode::Full, false},
      {"/frame/native", RefreshMode::Full, true},
      {"/frame/native/fast-full", RefreshMode::FullFast, true},
      {"/frame/native/partial", RefreshMode::Window, true},
      {"/frame/native/fast-window", RefreshMode::WindowFastExperimental, true}};
  for (const auto &entry : paths) {
    RefreshRequest request(RefreshMode::Window, 4, 4, 8, 8);
    bool native = false;
    assert(panel::requestForPath(entry.path, request, native));
    assert(request.mode == entry.mode && native == entry.native);
    assert(request.x == 0 && request.y == 0 && request.w == 800 && request.h == 480);
  }
  RefreshRequest request;
  bool native = false;
  assert(!panel::requestForPath("/frame/native/unknown", request, native));

  for (auto mode : {RefreshMode::Window, RefreshMode::WindowFastExperimental}) {
    auto valid = window("200", "120", "400", "240", mode);
    assert(!valid.finish(request));
    assert(request.mode == mode && request.x == 200 && request.y == 120 &&
           request.w == 400 && request.h == 240);
    auto fullWindow = window("0", "0", "800", "480", mode);
    assert(!fullWindow.finish(request) && request.mode == mode);
    auto edge = window("796", "478", "4", "2", mode);
    assert(!edge.finish(request));
    auto verticalOdd = window("4", "1", "4", "3", mode);
    assert(!verticalOdd.finish(request));  // Only x/w require multiples of four.
  }

  const char *invalid[][4] = {
      {nullptr, "0", "4", "2"}, {"0", nullptr, "4", "2"},
      {"0", "0", nullptr, "2"}, {"0", "0", "4", nullptr},
      {"", "0", "4", "2"}, {"-4", "0", "4", "2"},
      {"+4", "0", "4", "2"}, {" 4", "0", "4", "2"},
      {"4 ", "0", "4", "2"}, {"4abc", "0", "4", "2"},
      {"0x4", "0", "4", "2"}, {"4.0", "0", "4", "2"},
      {"65536", "0", "4", "2"}, {"4294967296", "0", "4", "2"},
      {"800", "0", "4", "2"}, {"0", "480", "4", "2"},
      {"0", "0", "0", "2"}, {"0", "0", "4", "1"},
      {"1", "0", "4", "2"}, {"0", "0", "5", "2"},
      {"796", "0", "8", "2"}, {"0", "479", "4", "2"},
      {"65535", "65535", "65535", "65535"}};
  const RefreshRequest untouched(RefreshMode::FullFast, 1, 2, 3, 4);
  for (const auto &values : invalid) {
    auto parser = window(values[0], values[1], values[2], values[3]);
    request = untouched;
    assert(parser.finish(request));
    assert(panel::sameRequest(request, untouched));  // Failed parse is not a partial commit.
  }
  auto duplicate = window("0", "0", "4", "2");
  duplicate.add("x", "0");
  assert(duplicate.finish(request));
  auto unknown = window("0", "0", "4", "2");
  unknown.add("mode", "full");
  assert(unknown.finish(request));
  for (auto mode : {RefreshMode::Full, RefreshMode::FullFast}) {
    panel::RefreshParameters parser(mode);
    assert(!parser.finish(request) && request.mode == mode);
    parser.add("x", "0");
    assert(parser.finish(request));
  }
  request = RefreshRequest(RefreshMode::Window, 0, 0, 800, 480);
  assert(!panel::sameRequest(request, RefreshRequest()));
  assert(panel::sameRequest(request, request));
  puts("Refresh routes, strict decimal/window parameters, duplicate fields and atomic parsing passed");
}
