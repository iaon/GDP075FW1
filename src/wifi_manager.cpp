#include "wifi_manager.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>

namespace {
constexpr uint32_t kConnectTimeout = 30000;
constexpr uint32_t kSerialTimeout = 2000;
}

void WifiManager::loadConfig() {
  Preferences preferences;
  if (!preferences.begin("wifi_config", true)) {
    Serial.println("NVS open failed! Using default configuration.");
    return;
  }
  const String ssid = preferences.getString("ssid", "");
  const String password = preferences.getString("password", "");
  const bool hasPassword = preferences.isKey("password");
  // The old open replacement saved only ssid/password. Keep these settings
  // and migrate its missing host_number to the recovered factory default.
  const int32_t host = preferences.isKey("host_number")
                           ? preferences.getInt("host_number", 0)
                           : vendor::kDefaultHost;
  preferences.end();
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 64 ||
      !hasPassword || !vendor::validHost(host)) {
    Serial.println("Invalid NVS configuration! Using defaults.");
    return;
  }
  ssid.toCharArray(config_.ssid, sizeof(config_.ssid));
  password.toCharArray(config_.password, sizeof(config_.password));
  config_.host = host;
  Serial.println("ESP32 Saved WiFi DATA");
  Serial.printf("1.WiFi-Name: %s\n2.WiFi-Password: %s\n3.WiFi-Device IP: %u\n",
                config_.ssid, config_.password, config_.host);
}

void WifiManager::loadRestartCount() {
  Preferences preferences;
  if (!preferences.begin("restart_counter", false)) return;
  // The vendor code clears count on power-on, reads it on other resets, and
  // clears it after a successful connection. No increment was recovered.
  if (esp_reset_reason() == ESP_RST_POWERON) preferences.putInt("count", 0);
  const int32_t count = preferences.getInt("count", 0);
  preferences.end();
  Serial.printf("Software restart count:\n%ld\n", static_cast<long>(count));
}

void WifiManager::clearRestartCount() {
  Preferences preferences;
  if (preferences.begin("restart_counter", false)) {
    preferences.putInt("count", 0);
    preferences.end();
  }
}

void WifiManager::begin() {
  loadConfig();
  loadRestartCount();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  startConnection();
}

void WifiManager::startConnection() {
  connected_ = false;
  WiFi.disconnect(false, false);
  if (!WiFi.config(IPAddress(), IPAddress(), IPAddress())) {
    Serial.println("Configuration failed.");
    finishConnection(false);
    return;
  }
  WiFi.begin(config_.ssid, config_.password);
  phase_ = Phase::Dhcp;
  phaseStarted_ = lastDot_ = millis();
}

void WifiManager::finishConnection(bool success) {
  connected_ = success;
  phase_ = Phase::Idle;
  lastCheck_ = millis();
  statusUpdate_ = true;
  Serial.println();
  if (success) {
    clearRestartCount();
    Serial.println("WiFi Connected");
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.println("--WiFi connection successful--");
    Serial.println("WiFi Configuration Successful");
  } else {
    Serial.println("WiFi Connection Failed");
    Serial.println("--WiFi connection failed !!!--");
    Serial.println("--Please check the WiFi configuration !!!--");
    Serial.println("WiFi Configuration Failed!!!");
  }
  Serial.printf("1.WiFi-Name: %s\n2.WiFi-Password: %s\n"
                "3.WiFi-StaticHostNumber: %u\n",
                config_.ssid, config_.password, config_.host);
}

bool WifiManager::takeStatusUpdate() {
  const bool update = statusUpdate_;
  statusUpdate_ = false;
  return update;
}

void WifiManager::poll() {
  pollSerial();
  const uint32_t now = millis();
  if (phase_ != Phase::Idle) {
    if (WiFi.status() == WL_CONNECTED &&
        (phase_ == Phase::Dhcp || WiFi.localIP() == staticAddress_)) {
      if (phase_ == Phase::Dhcp) {
        // Vendor routine 0x400d2bc0 learns the gateway with DHCP, then uses
        // its first three octets and host_number on a /24 network.
        const IPAddress gateway = WiFi.gatewayIP();
        const IPAddress local(gateway[0], gateway[1], gateway[2], config_.host);
        if (uint32_t(gateway) == 0 || local == gateway) {
          Serial.println("Configuration failed.");
          finishConnection(false);
          return;
        }
        Serial.println("WiFi setup successful");
        WiFi.disconnect(false, false);
        if (!WiFi.config(local, gateway, IPAddress(255, 255, 255, 0),
                         gateway, gateway)) {
          Serial.println("Configuration failed.");
          finishConnection(false);
          return;
        }
        WiFi.begin(config_.ssid, config_.password);
        staticAddress_ = local;
        phase_ = Phase::Static;
        phaseStarted_ = lastDot_ = now;
      } else {
        finishConnection(true);
      }
    } else if (now - phaseStarted_ >= kConnectTimeout) {
      finishConnection(false);
    } else if (now - lastDot_ >= 1000) {
      Serial.print('.');
      lastDot_ = now;
    }
    return;
  }
  if (WiFi.status() != WL_CONNECTED) connected_ = false;
  else if (!connected_ && uint32_t(staticAddress_) != 0 &&
           WiFi.localIP() == staticAddress_) {
    // SDK auto-reconnect can restore the static connection before our retry.
    finishConnection(true);
    return;
  }
  if (now - lastCheck_ >= kConnectTimeout) {
    lastCheck_ = now;
    Serial.print("Check WiFi status...");
    if (!connected_) {
      Serial.println("WiFi has been disconnected, try to reconnect...");
      startConnection();
    } else {
      Serial.println("WiFi connection is ok");
    }
  }
}

void WifiManager::applyConfig(const vendor::WifiConfig &config) {
  Preferences preferences;
  if (!preferences.begin("wifi_config", false)) {
    Serial.println("ERR: cannot open WiFi NVS");
    return;
  }
  // Match the vendor NVS types: strings plus signed 32-bit host_number.
  const size_t ssidSaved = preferences.putString("ssid", config.ssid);
  const size_t passwordSaved = preferences.putString("password", config.password);
  const size_t hostSaved = preferences.putInt("host_number", config.host);
  // putString returns zero for a successfully saved empty password too.
  const bool saved = ssidSaved == strlen(config.ssid) &&
                     passwordSaved == strlen(config.password) && hostSaved == 4 &&
                     preferences.isKey("password") &&
                     preferences.getString("password", "") == config.password;
  preferences.end();
  if (!saved) {
    Serial.println("ERR: cannot save WiFi configuration");
    return;
  }
  Serial.printf("1.Received WiFi-Name: %s\n2.Received WiFi-Password: %s\n"
                "3.Received WiFi-Device IP: %u\n",
                config.ssid, config.password, config.host);
  for (int remaining = 3; remaining > 0; --remaining) {
    Serial.printf("--%d--\n", remaining);
    delay(1000);
  }
  ESP.restart();
}

void WifiManager::pollSerial() {
  if (serialBytes_ && millis() - serialLastByte_ >= kSerialTimeout) {
    serialBytes_ = 0;
    serialOverflow_ = false;
    Serial.println("Error: Invalid data length!");
  }
  while (Serial.available()) {
    const uint8_t value = Serial.read();
    serialLastByte_ = millis();
    const bool text = serialBytes_ ? serialBuffer_[0] == 'W' : value == 'W';
    if (text && value == '\r') continue;
    if (serialBytes_ < sizeof(serialBuffer_) - 1)
      serialBuffer_[serialBytes_++] = value;
    else
      serialOverflow_ = true;
    if (text && value == '\n') {
      serialBuffer_[serialBytes_ - 1] = 0;
      vendor::WifiConfig candidate = config_;
      if (!serialOverflow_ && vendor::decodeText(
                                 reinterpret_cast<char *>(serialBuffer_), candidate))
        applyConfig(candidate);
      else
        Serial.println("ERR: WIFI <ssid> <password> [host_number] (quotes allowed)");
      serialBytes_ = 0;
      serialOverflow_ = false;
    } else if (!text && serialBytes_ == vendor::kProvisioningBytes) {
      vendor::WifiConfig candidate;
      const auto result = vendor::decodePacket(serialBuffer_, serialBytes_, candidate);
      serialBytes_ = 0;
      if (result == vendor::DecodeResult::Ok)
        applyConfig(candidate);
      else if (result == vendor::DecodeResult::InvalidEndMarker)
        Serial.println("Error: Invalid end marker!");
      else if (result == vendor::DecodeResult::InvalidHost)
        Serial.println("Error: host_number must be 1..254");
      else
        Serial.println("Error: Invalid data length!");
    }
  }
}
