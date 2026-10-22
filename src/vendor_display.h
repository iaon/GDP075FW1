#pragma once

#include <initializer_list>
#include <epd4c/GxEPD2_750c_GDEM075F52.h>
#include "refresh_request.h"

// Controller commands and per-byte CS/DC sequence recovered from app0.
class VendorGDEM075F52 : public GxEPD2_750c_GDEM075F52 {
 public:
  static const bool hasPartialUpdate = false;
  enum class Error { None, ResetTimeout, FastSetupTimeout, PowerOnTimeout,
                     RefreshTimeout, PowerOffTimeout, InvalidRequest,
                     InvalidRows, IncompleteFrame };
  bool ok() const { return error_ == Error::None; }
  Error error() const { return error_; }
  const char *errorText() const {
    switch (error_) {
      case Error::None: return "ok";
      case Error::ResetTimeout: return "display reset timeout";
      case Error::FastSetupTimeout: return "display fast setup timeout";
      case Error::PowerOnTimeout: return "display power-on timeout";
      case Error::RefreshTimeout: return "display refresh timeout";
      case Error::PowerOffTimeout: return "display power-off timeout";
      case Error::InvalidRequest: return "invalid display refresh request";
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
    request_ = panel::RefreshRequest();
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

  bool beginFrame(const panel::RefreshRequest &request) {
    // Test registers and partial windows must not leak into another frame,
    // including callers that start a frame without first hibernating.
    const bool reset = !ok() || request_.mode != panel::RefreshMode::Full ||
                       request.mode != panel::RefreshMode::Full;
    // A fresh frame is the explicit recovery point after a failed operation.
    error_ = Error::None;
    if (!validRequest(request)) {
      fail(Error::InvalidRequest);
      return false;
    }
    request_ = request;
    if (reset) _init_display_done = false;
    if (!initVendor()) return false;
    // Every request loads all controller RAM, even when only an ROI will be
    // physically refreshed. No retained RAM or previous-frame seed is needed.
    if (panel::isWindow(request_.mode)) setWindow(0, 0, WIDTH, HEIGHT, false);
    command(0x10, {});
    nextRow_ = 0;
    return true;
  }

  void setPaged() override { beginFrame(panel::RefreshRequest()); }

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
    if (panel::isWindow(request_.mode)) {
      setWindow(request_.x, request_.y, request_.w, request_.h, true);
      command(0x50, {0x97});
    }
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
  bool validRequest(const panel::RefreshRequest &request) const {
    switch (request.mode) {
      case panel::RefreshMode::Full:
      case panel::RefreshMode::FullFast:
        return request.x == 0 && request.y == 0 && request.w == WIDTH &&
               request.h == HEIGHT;
      case panel::RefreshMode::Window:
      case panel::RefreshMode::WindowFastExperimental:
        return panel::validWindow(request);
    }
    return false;
  }

  void setWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool partial) {
    const uint16_t right = x + w - 1;
    const uint16_t bottom = y + h - 1;
    command(0x83, {uint8_t(x >> 8), uint8_t(x), uint8_t(right >> 8),
                   uint8_t(right), uint8_t(y >> 8), uint8_t(y),
                   uint8_t(bottom >> 8), uint8_t(bottom),
                   uint8_t(partial ? 1 : 0)});
  }

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
    _power_is_on = false;
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
    if (panel::isFast(request_.mode)) {
      // GxEPD2 1.6.9 fast-full setup; combining it with a window is an
      // explicit experiment, not a claim of fast-partial support.
      command(0xe0, {0x02});
      command(0xe6, {0x5a});
      command(0xa5, {0x00});
      if (!waitReady(Error::FastSetupTimeout)) return false;
    }
    command(0x04, {});
    _power_is_on = true;  // Power-off cleanup is needed even if BUSY sticks.
    if (!waitReady(Error::PowerOnTimeout)) return false;
    _init_display_done = true;
    _hibernating = false;
    return true;
  }

  static constexpr uint32_t kBusyTimeoutMs = 50000;
  Error error_ = Error::None;
  panel::RefreshRequest request_;
  int16_t nextRow_ = HEIGHT;
};
