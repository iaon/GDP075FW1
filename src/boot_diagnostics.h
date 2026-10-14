#pragma once

#include <Arduino.h>
#include <esp_system.h>

// Diagnostics deliberately do not write a boot counter to NVS.
inline const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK_WATCHDOG";
    case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

class BootDiagnostics {
 public:
  void begin() {
    reason_ = esp_reset_reason();
    lastReport_ = millis();
    Serial.printf("BOOT reset_reason=%d (%s) uptime_ms=%lu\n",
                  static_cast<int>(reason_), resetReasonName(reason_),
                  static_cast<unsigned long>(lastReport_));
  }
  void poll(bool connected, uint32_t attempts) {
    const uint32_t now = millis();
    if (uint32_t(now - lastReport_) < 30000) return;
    lastReport_ = now;
    Serial.printf("ALIVE uptime_ms=%lu wifi_ready=%u attempts=%lu reset_reason=%s\n",
                  static_cast<unsigned long>(now), static_cast<unsigned>(connected),
                  static_cast<unsigned long>(attempts), resetReasonName(reason_));
  }

 private:
  esp_reset_reason_t reason_ = ESP_RST_UNKNOWN;
  uint32_t lastReport_ = 0;
};
