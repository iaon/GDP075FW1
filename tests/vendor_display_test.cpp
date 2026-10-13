#include "vendor_display.h"
#include <assert.h>
#include <stdio.h>

int main() {
  testhw::reset();
  VendorGDEM075F52 display(27, 14, 12, 13);
  display.init(115200, true, 10, false);
  assert(testhw::clockMs == 0 && trace::commands.empty());
  for (const auto &event : testhw::events)
    assert(!(event.kind == testhw::Event::Pin && event.pin == 12 && event.value == LOW));
  display.setPaged();
  assert(testhw::clockMs == 122);
  // Command/data pairs independently transcribed from app0 0x400d2568.
  const std::vector<trace::Command> expected = {
      {0x4d, {0x78}}, {0x00, {0x2f, 0x29}}, {0xe3, {0x88}},
      {0x50, {0x37}}, {0x61, {0x03, 0x20, 0x01, 0xe0}},
      {0x65, {0, 0, 0, 0}}, {0xe9, {1}}, {0xf0, {0x5f}},
      {0x30, {8}}, {0x04, {}}, {0x10, {}}};
  assert(trace::commands.size() == expected.size());
  for (size_t index = 0; index < expected.size(); ++index) {
    assert(trace::commands[index].command == expected[index].command);
    assert(trace::commands[index].data == expected[index].data);
  }
  uint8_t row[200];
  for (auto &byte : row) byte = 0x1b;  // black / white / yellow / red
  for (int y = 0; y < 480; ++y)
    display.writeNative(row, nullptr, 0, y, 800, 1);
  assert(trace::commands.back().command == 0x10);
  assert(trace::commands.back().data == std::vector<uint8_t>(96000, 0x1b));
  display.refresh();
  assert(trace::commands.back().command == 0x12);
  assert(trace::commands.back().data == std::vector<uint8_t>{0});
  display.hibernate();
  assert(display.ok());
  assert(trace::commands[trace::commands.size() - 2].command == 0x02);
  assert(trace::commands[trace::commands.size() - 2].data == std::vector<uint8_t>{0});
  assert(trace::commands.back().command == 0x07);
  assert(trace::commands.back().data == std::vector<uint8_t>{0xa5});

  // Execute the real driver's byte transport; check every CS/DC transaction.
  for (size_t index = 0; index < testhw::events.size(); ++index) {
    const auto &event = testhw::events[index];
    if (event.kind != testhw::Event::Byte) continue;
    assert(index >= 3 && index + 2 < testhw::events.size());
    assert(testhw::events[index - 3].kind == testhw::Event::Begin);
    assert(testhw::events[index - 3].value == 10000000);
    assert(testhw::events[index - 2].pin == 27 && testhw::events[index - 2].value == LOW);
    assert(testhw::events[index - 1].pin == 14);
    assert(testhw::events[index + 1].pin == 27 && testhw::events[index + 1].value == HIGH);
    assert(testhw::events[index + 2].kind == testhw::Event::End);
  }

  trace::commands.clear();
  display.setPaged();
  assert(trace::commands[0].command == 0x4d);  // Wake reinitializes controller.
  display.writeNative(row, nullptr, 0, 0, 800, 1);
  const size_t before = trace::commands.size();
  display.refresh();  // An incomplete frame must not trigger a refresh.
  assert(trace::commands.size() == before);
  assert(display.error() == VendorGDEM075F52::Error::IncompleteFrame);

  trace::commands.clear();
  display.writeScreenBuffer();
  assert(trace::commands.back().data == std::vector<uint8_t>(96000, 0x55));
  display.setPaged();
  uint8_t black[100], colour[100];
  for (unsigned index = 0; index < 100; ++index) {
    black[index] = 0x55;
    colour[index] = 0xff;
  }
  display.writeImage(black, colour, 0, 0, 800, 1);
  assert(trace::commands.back().data == std::vector<uint8_t>(200, 0x11));

  for (auto error : {VendorGDEM075F52::Error::ResetTimeout,
                    VendorGDEM075F52::Error::PowerOnTimeout,
                    VendorGDEM075F52::Error::RefreshTimeout,
                    VendorGDEM075F52::Error::PowerOffTimeout}) {
    testhw::reset();
    testhw::clockMs = 0xfffffff0;  // Timeout arithmetic must survive millis wrap.
    VendorGDEM075F52 failing(27, 14, 12, 13);
    if (error == VendorGDEM075F52::Error::ResetTimeout) testhw::busyStuck = true;
    if (error == VendorGDEM075F52::Error::PowerOnTimeout) testhw::stuckOnCommand = 0x04;
    failing.setPaged();
    if (failing.ok()) {
      for (int y = 0; y < 480; ++y) failing.writeNative(row, nullptr, 0, y, 800, 1);
      if (error == VendorGDEM075F52::Error::RefreshTimeout) testhw::stuckOnCommand = 0x12;
      failing.refresh();
      if (error == VendorGDEM075F52::Error::PowerOffTimeout) testhw::stuckOnCommand = 0x02;
    }
    failing.hibernate();
    assert(!failing.ok() && failing.error() == error);  // Cleanup preserves cause.
    testhw::busyStuck = false;
    testhw::stuckOnCommand = -1;
    failing.setPaged();
    for (int y = 0; y < 480; ++y) failing.writeNative(row, nullptr, 0, y, 800, 1);
    failing.refresh();
    failing.hibernate();
    assert(failing.ok());  // A new frame recovers after the fault is removed.
  }
  testhw::reset();
  VendorGDEM075F52 invalid(27, 14, 12, 13);
  invalid.setPaged();
  invalid.writeNative(row, nullptr, 0, 1, 800, 1);
  assert(invalid.error() == VendorGDEM075F52::Error::InvalidRows);
  const size_t commandsBefore = trace::commands.size();
  invalid.refresh();
  assert(trace::commands.size() == commandsBefore);
  puts("Display command trace, per-byte SPI, BUSY faults and recovery tests passed");
}
