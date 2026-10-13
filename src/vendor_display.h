#pragma once

#include <initializer_list>
#include <epd4c/GxEPD2_750c_GDEM075F52.h>

// Controller commands and per-byte CS/DC sequence recovered from app0.
class VendorGDEM075F52 : public GxEPD2_750c_GDEM075F52 {
 public:
  static const bool hasPartialUpdate = false;
  enum class Error { None, ResetTimeout, PowerOnTimeout, RefreshTimeout,
                     PowerOffTimeout, InvalidRows, IncompleteFrame };
  bool ok() const { return error_ == Error::None; }
  Error error() const { return error_; }
  const char *errorText() const {
    switch (error_) {
      case Error::None: return "ok";
      case Error::ResetTimeout: return "display reset timeout";
      case Error::PowerOnTimeout: return "display power-on timeout";
      case Error::RefreshTimeout: return "display refresh timeout";
      case Error::PowerOffTimeout: return "display power-off timeout";
      case Error::InvalidRows: return "invalid display rows";
      case Error::IncompleteFrame: return "incomplete display frame";
    }
    return "display error";
  }

  VendorGDEM075F52(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
      : GxEPD2_750c_GDEM075F52(cs, dc, rst, busy) {
    selectSPI(SPI, SPISettings(10000000, MSBFIRST, SPI_MODE0));
  }

  void init(uint32_t baud = 0) override { init(baud, true, 10, false); }
  void init(uint32_t baud, bool initial, uint16_t = 10, bool = false) override {
    if (baud) Serial.begin(baud);
    error_ = Error::None;
    _initial_write = initial;
    _power_is_on = _init_display_done = _hibernating = false;
    nextRow_ = HEIGHT;
    digitalWrite(_cs, HIGH);
    pinMode(_cs, OUTPUT);
    digitalWrite(_rst, HIGH);
    pinMode(_rst, OUTPUT);
    pinMode(_dc, OUTPUT);
    pinMode(_busy, INPUT);
    _pSPIx->begin();
    // Reset only when a frame starts; base init would add an extra reset.
  }

  void setPaged() override {
    // A fresh frame is the explicit recovery point after a failed operation.
    error_ = Error::None;
    if (!initVendor()) return;
    command(0x10, {});
    nextRow_ = 0;
  }

  void writeScreenBuffer(uint8_t value = 0xff) override {
    setPaged();
    if (!ok()) return;
    for (size_t i = 0; i < size_t(WIDTH) * HEIGHT / 4; ++i) {
      dataByte(value == 0xff ? 0x55 : 0x00);
      if (i % 200 == 199) yield();
    }
    _initial_write = false;
    nextRow_ = HEIGHT;
  }

  void writeNative(const uint8_t *data, const uint8_t *, int16_t x, int16_t y,
                   int16_t w, int16_t h, bool invert = false,
                   bool mirror_y = false, bool pgm = false) {
    if (!ok()) return;
    if (!data || !validRows(x, y, w, h)) { fail(Error::InvalidRows); return; }
    for (int16_t row = 0; row < h; ++row) {
      const int16_t sourceRow = mirror_y ? h - row - 1 : row;
      for (int16_t column = 0; column < WIDTH / 4; ++column) {
        const size_t index = size_t(sourceRow) * (WIDTH / 4) + column;
        const uint8_t value = pgm ? pgm_read_byte(data + index) : data[index];
        dataByte(invert ? ~value : value);
      }
      yield();
    }
    nextRow_ += h;
    _initial_write = false;
  }

  void writeImage(const uint8_t *black, const uint8_t *colour, int16_t x,
                  int16_t y, int16_t w, int16_t h, bool invert = false,
                  bool mirror_y = false, bool pgm = false) {
    if (!ok()) return;
    if (!black || !colour || !validRows(x, y, w, h)) {
      fail(Error::InvalidRows); return;
    }
    for (int16_t row = 0; row < h; ++row) {
      const int16_t sourceRow = mirror_y ? h - row - 1 : row;
      for (int16_t column = 0; column < WIDTH / 8; ++column) {
        const size_t index = size_t(sourceRow) * (WIDTH / 8) + column;
        uint8_t bw = pgm ? pgm_read_byte(black + index) : black[index];
        uint8_t cr = pgm ? pgm_read_byte(colour + index) : colour[index];
        if (invert) { bw = ~bw; cr = ~cr; }
        for (unsigned half = 0; half < 2; ++half) {
          uint8_t packed = 0;
          for (unsigned pixel = 0; pixel < 4; ++pixel) {
            packed = (packed << 2) | (!(cr & 0x80) ? 3 : (bw & 0x80) ? 1 : 0);
            bw <<= 1;
            cr <<= 1;
          }
          dataByte(packed);
        }
      }
      yield();
    }
    nextRow_ += h;
    _initial_write = false;
  }

  void refresh(bool = false) override {
    if (!ok()) return;
    if (nextRow_ != HEIGHT) { fail(Error::IncompleteFrame); return; }
    command(0x12, {0x00});
    waitReady(Error::RefreshTimeout);
  }
  void refresh(int16_t, int16_t, int16_t, int16_t) override { refresh(false); }

  void powerOff() override {
    if (_power_is_on) {
      command(0x02, {0x00});
      waitReady(Error::PowerOffTimeout);
    }
    _power_is_on = false;
  }

  void hibernate() override {
    // Cleanup must preserve the first failure for callers, including HTTP.
    powerOff();
    command(0x07, {0xa5});
    _hibernating = true;
    _init_display_done = false;
    nextRow_ = HEIGHT;
  }

 private:
  bool validRows(int16_t x, int16_t y, int16_t w, int16_t h) const {
    return _init_display_done && x == 0 && w == WIDTH && y == nextRow_ &&
           y >= 0 && h >= 1 && y + h <= HEIGHT;
  }
  void fail(Error error) {
    if (ok()) error_ = error;
    _init_display_done = false;
  }
  bool waitReady(Error error) {
    const uint32_t start = millis();
    delay(1);  // Allow BUSY to become active after the command.
    while (digitalRead(_busy) == LOW) {
      if (uint32_t(millis() - start) >= kBusyTimeoutMs) {
        fail(error);
        Serial.println(errorText());
        return false;
      }
      delay(1);  // Yield to the ESP32 scheduler/watchdog.
    }
    return true;
  }
  void writeByte(uint8_t value, bool data) {
    _pSPIx->beginTransaction(_spi_settings);
    // Match original 0x400d26d0/0x400d26f4: CS LOW, then DC, byte, CS HIGH.
    digitalWrite(_cs, LOW);
    digitalWrite(_dc, data ? HIGH : LOW);
    _pSPIx->transfer(value);
    digitalWrite(_cs, HIGH);
    _pSPIx->endTransaction();
  }
  void dataByte(uint8_t value) { writeByte(value, true); }
  void command(uint8_t value, std::initializer_list<uint8_t> data) {
    writeByte(value, false);
    for (uint8_t byte : data) dataByte(byte);
  }

  bool initVendor() {
    if (_init_display_done) return true;
    delay(100);
    digitalWrite(_rst, LOW);
    delay(10);
    digitalWrite(_rst, HIGH);
    delay(10);
    if (!waitReady(Error::ResetTimeout)) return false;
    command(0x4d, {0x78});
    command(0x00, {0x2f, 0x29});
    command(0xe3, {0x88});
    command(0x50, {0x37});
    command(0x61, {0x03, 0x20, 0x01, 0xe0});
    command(0x65, {0x00, 0x00, 0x00, 0x00});
    command(0xe9, {0x01});
    command(0xf0, {0x5f});
    command(0x30, {0x08});
    command(0x04, {});
    _power_is_on = true;  // Power-off cleanup is needed even if BUSY sticks.
    if (!waitReady(Error::PowerOnTimeout)) return false;
    _init_display_done = true;
    _hibernating = false;
    return true;
  }

  static constexpr uint32_t kBusyTimeoutMs = 50000;
  Error error_ = Error::None;
  int16_t nextRow_ = HEIGHT;
};
