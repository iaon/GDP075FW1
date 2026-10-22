#pragma once
#include "Arduino.h"
#include "SPI.h"
class GxEPD2_750c_GDEM075F52 {
 public:
  static constexpr uint16_t WIDTH = 800, HEIGHT = 480;
  static const bool hasPartialUpdate = true;
  static const bool hasFastPartialUpdate = false;
  static const bool useFastFullUpdate = false;
  static constexpr uint16_t full_refresh_time = 21000, power_on_time = 150, power_off_time = 100;
  GxEPD2_750c_GDEM075F52(int cs, int dc, int rst, int busy)
      : _cs(cs), _dc(dc), _rst(rst), _busy(busy) {}
  virtual ~GxEPD2_750c_GDEM075F52() {}
  virtual void init(uint32_t = 0) {}
  virtual void init(uint32_t, bool, uint16_t = 10, bool = false) {}
  void selectSPI(SPIClass &spi, SPISettings settings) { _pSPIx = &spi; _spi_settings = settings; }
  virtual void setPaged() {}
  virtual void writeScreenBuffer(uint8_t = 0xff) {}
  virtual void refresh(bool = false) {}
  virtual void refresh(int16_t, int16_t, int16_t, int16_t) {}
  virtual void powerOff() {}
  virtual void hibernate() {}
 protected:
  SPIClass *_pSPIx = &SPI;
  SPISettings _spi_settings;
  int _cs, _dc, _rst, _busy;
  bool _init_display_done = false, _initial_write = true, _power_is_on = false, _hibernating = false;
};
