#pragma once
#include "Arduino.h"
namespace trace {
struct Command { uint8_t command; std::vector<uint8_t> data; };
extern std::vector<Command> commands;
}
struct SPISettings {
  SPISettings(unsigned hz = 4000000, int = MSBFIRST, int = SPI_MODE0) : hz(hz) {}
  unsigned hz;
};
class SPIClass {
 public:
  void begin() { testhw::events.push_back({testhw::Event::BusInit, -1, 0}); }
  void beginTransaction(SPISettings settings) {
    testhw::events.push_back({testhw::Event::Begin, -1, settings.hz});
  }
  void endTransaction() { testhw::events.push_back({testhw::Event::End, -1, 0}); }
  uint8_t transfer(uint8_t value) {
    testhw::events.push_back({testhw::Event::Byte, -1, value});
    if (testhw::pins[14] == LOW) {
      trace::commands.push_back({value, {}});
      if (value == testhw::stuckOnCommand) testhw::busyStuck = true;
    } else {
      trace::commands.back().data.push_back(value);
    }
    return 0;
  }
};
extern SPIClass SPI;
