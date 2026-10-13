#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include "vendor_protocol.h"

class WifiManager {
 public:
  void begin();
  void poll();
  bool ready() const { return phase_ == Phase::Idle && connected_; }
  const vendor::WifiConfig &config() const { return config_; }
  bool takeStatusUpdate();

 private:
  enum class Phase { Idle, Dhcp, Static };
  void loadConfig();
  void startConnection();
  void finishConnection(bool success);
  void pollSerial();
  void applyConfig(const vendor::WifiConfig &config);
  void loadRestartCount();
  void clearRestartCount();

  vendor::WifiConfig config_;
  Phase phase_ = Phase::Idle;
  uint32_t phaseStarted_ = 0;
  uint32_t lastCheck_ = 0;
  uint32_t lastDot_ = 0;
  bool connected_ = false;
  bool statusUpdate_ = false;
  IPAddress staticAddress_;
  uint8_t serialBuffer_[256] = {};
  size_t serialBytes_ = 0;
  uint32_t serialLastByte_ = 0;
  bool serialOverflow_ = false;
};
