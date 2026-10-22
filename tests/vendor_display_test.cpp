#include "vendor_display.h"
#include <assert.h>
#include <stdio.h>

namespace {

void expectCommands(const std::vector<trace::Command> &expected) {
  assert(trace::commands.size() == expected.size());
  for (size_t index = 0; index < expected.size(); ++index) {
    assert(trace::commands[index].command == expected[index].command);
    assert(trace::commands[index].data == expected[index].data);
  }
}

unsigned commandCount(uint8_t command) {
  unsigned count = 0;
  for (const auto &entry : trace::commands)
    if (entry.command == command) ++count;
  return count;
}

unsigned resetPulses() {
  unsigned count = 0;
  for (const auto &event : testhw::events)
    if (event.kind == testhw::Event::Pin && event.pin == 12 && event.value == LOW)
      ++count;
  return count;
}

void writeFullFrame(VendorGDEM075F52 &display) {
  uint8_t row[200];
  for (int y = 0; y < 480; ++y) {
    for (size_t x = 0; x < sizeof row; ++x) row[x] = uint8_t(y + x);
    display.writeNative(row, nullptr, 0, y, 800, 1);
  }
  assert(display.ok());
  assert(trace::commands.back().command == 0x10);
  const auto &bytes = trace::commands.back().data;
  assert(bytes.size() == 96000);
  for (size_t i = 0; i < bytes.size(); ++i)
    assert(bytes[i] == uint8_t(i / 200 + i % 200));
}

void testRefreshModes(const std::vector<trace::Command> &normalInit) {
  using panel::RefreshMode;
  for (auto mode : {RefreshMode::FullFast, RefreshMode::Window,
                    RefreshMode::WindowFastExperimental}) {
    testhw::reset();
    VendorGDEM075F52 display(27, 14, 12, 13);
    display.init();
    const bool window = panel::isWindow(mode);
    const bool fast = panel::isFast(mode);
    const panel::RefreshRequest request = window ?
        panel::RefreshRequest(mode, 260, 258, 536, 222) :
        panel::RefreshRequest(mode);
    assert(display.beginFrame(request));
    assert(resetPulses() == 1);
    assert(testhw::clockMs == (fast ? 123 : 122));
    auto expected = normalInit;
    expected.pop_back();  // DTM starts after optional full-RAM window setup.
    if (fast) {
      expected.insert(expected.end() - 1, {{0xe0, {0x02}}, {0xe6, {0x5a}},
                                           {0xa5, {0x00}}});
    }
    if (window) expected.push_back({0x83, {0, 0, 3, 0x1f, 0, 0, 1, 0xdf, 0}});
    expected.push_back({0x10, {}});
    expectCommands(expected);

    // All 96,000 bytes are transmitted, including pixels outside the ROI.
    // Each diagnostic request works from a reset, with no seeded prior frame.
    writeFullFrame(display);
    const size_t beforeRefresh = trace::commands.size();
    display.refresh();
    assert(display.ok());
    assert(commandCount(0x12) == 1);
    assert(trace::commands.size() == beforeRefresh + (window ? 3 : 1));
    if (window) {
      assert(trace::commands[beforeRefresh].command == 0x83);
      assert(trace::commands[beforeRefresh].data ==
             (std::vector<uint8_t>{1, 4, 3, 0x1b, 1, 2, 1, 0xdf, 1}));
      assert(trace::commands[beforeRefresh + 1].command == 0x50);
      assert(trace::commands[beforeRefresh + 1].data == std::vector<uint8_t>{0x97});
    }
    assert(trace::commands.back().command == 0x12);
    assert(trace::commands.back().data == std::vector<uint8_t>{0});

    // Even without hibernate(), the next default frame resets test settings
    // and retains the exact original full-update command sequence.
    trace::commands.clear();
    testhw::events.clear();
    display.setPaged();
    assert(display.ok() && resetPulses() == 1);
    expectCommands(normalInit);
    writeFullFrame(display);
    const size_t beforeLegacyRefresh = trace::commands.size();
    display.refresh(4, 2, 16, 4);  // Legacy overload still performs a full update.
    assert(trace::commands.size() == beforeLegacyRefresh + 1);
    assert(trace::commands.back().command == 0x12);
    assert(commandCount(0x83) == 0);
    display.hibernate();
    assert(display.ok());

    // Hibernation also requires a reset and a complete RAM upload next time.
    trace::commands.clear();
    testhw::events.clear();
    assert(display.beginFrame(request));
    assert(resetPulses() == 1);
    expectCommands(expected);
    display.refresh();
    assert(display.error() == VendorGDEM075F52::Error::IncompleteFrame);
    assert(commandCount(0x12) == 0);
  }
}

void testWindowValidation() {
  using panel::RefreshMode;
  const panel::RefreshRequest invalid[] = {
      {RefreshMode::Window, 1, 0, 4, 2},
      {RefreshMode::Window, 0, 0, 5, 2},
      {RefreshMode::Window, 0, 0, 0, 2},
      {RefreshMode::Window, 0, 0, 4, 0},
      {RefreshMode::Window, 0, 0, 4, 1},
      {RefreshMode::Window, 800, 0, 4, 2},
      {RefreshMode::Window, 0, 480, 4, 2},
      {RefreshMode::Window, 796, 0, 8, 2},
      {RefreshMode::Window, 0, 479, 4, 2},
      {RefreshMode::Window, 0xffff, 0xffff, 0xffff, 0xffff},
      {RefreshMode::WindowFastExperimental, 0, 0, 4, 1},
      {RefreshMode::Full, 4, 0, 796, 480},
      {RefreshMode::FullFast, 0, 0, 800, 2},
      {static_cast<RefreshMode>(99), 0, 0, 800, 480}};
  for (const auto &request : invalid) {
    testhw::reset();
    VendorGDEM075F52 display(27, 14, 12, 13);
    assert(!display.beginFrame(request));
    assert(display.error() == VendorGDEM075F52::Error::InvalidRequest);
    display.refresh();
    assert(trace::commands.empty() && testhw::events.empty());
  }

  for (const auto &request : {
           panel::RefreshRequest(RefreshMode::Window, 796, 478, 4, 2),
           panel::RefreshRequest(RefreshMode::Window, 0, 0, 800, 480)}) {
    testhw::reset();
    VendorGDEM075F52 display(27, 14, 12, 13);
    assert(display.beginFrame(request));
    writeFullFrame(display);
    display.refresh();
    assert(display.ok());
    const auto &window = trace::commands[trace::commands.size() - 3];
    assert(window.command == 0x83);
    assert(window.data == (request.x ?
        std::vector<uint8_t>{3, 0x1c, 3, 0x1f, 1, 0xde, 1, 0xdf, 1} :
        std::vector<uint8_t>{0, 0, 3, 0x1f, 0, 0, 1, 0xdf, 1}));
    // A full-sized ROI still tests PMODE=1 instead of quietly changing modes.
    assert(trace::commands[trace::commands.size() - 2].data == std::vector<uint8_t>{0x97});
    assert(commandCount(0x12) == 1);
  }
}

void testFastSetupFailure(const std::vector<trace::Command> &normalInit) {
  using panel::RefreshMode;
  for (auto mode : {RefreshMode::FullFast, RefreshMode::WindowFastExperimental}) {
    testhw::reset();
    testhw::clockMs = 0xfffffff0;  // Includes wraparound during the setup wait.
    VendorGDEM075F52 display(27, 14, 12, 13);
    testhw::stuckOnCommand = 0xa5;
    assert(!display.beginFrame(panel::RefreshRequest(mode)));
    assert(display.error() == VendorGDEM075F52::Error::FastSetupTimeout);
    assert(commandCount(0x04) == 0 && commandCount(0x10) == 0);
    display.refresh();
    assert(commandCount(0x12) == 0);
    display.hibernate();
    assert(display.error() == VendorGDEM075F52::Error::FastSetupTimeout);
    assert(trace::commands.back().command == 0xa5);  // BUSY stuck: no sleep command.

    testhw::busyStuck = false;
    testhw::stuckOnCommand = -1;
    trace::commands.clear();
    testhw::events.clear();
    assert(display.beginFrame(panel::RefreshRequest()));
    assert(resetPulses() == 1);
    expectCommands(normalInit);
    writeFullFrame(display);
    display.refresh();
    display.hibernate();
    assert(display.ok());
  }
}

}  // namespace

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
  expectCommands(expected);
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
  testRefreshModes(expected);
  testWindowValidation();
  testFastSetupFailure(expected);
  puts("Display command trace, refresh modes, ROI validation, BUSY faults and recovery tests passed");
}
