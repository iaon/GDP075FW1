#pragma once

#include <stdint.h>

// The production application uses only the wrapper's public driver and init.
template <typename Driver, uint16_t PageHeight>
class GxEPD2_4C {
 public:
  explicit GxEPD2_4C(const Driver &driver) : epd2(driver) {}
  void init(uint32_t baud, bool initial, uint16_t reset, bool pulldown) {
    epd2.init(baud, initial, reset, pulldown);
  }
  Driver epd2;
};
