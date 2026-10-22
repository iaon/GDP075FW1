#pragma once
#include "IPAddress.h"
#include <algorithm>
#include <memory>
constexpr int WIFI_STA = 1, WL_IDLE_STATUS = 0, WL_CONNECTED = 3;
class FakeWiFi {
 public:
  struct Config { IPAddress local, gateway, subnet, dns1, dns2; };
  int status() const { return statusValue; }
  int RSSI() const { return rssi; }
  void persistent(bool value) { persistentValue = value; }
  void mode(int) {}
  void setAutoReconnect(bool value) { autoReconnect = value; }
  bool disconnect(bool, bool) {
    ++disconnects;
    if (!staleConnectedEvent) { statusValue = WL_IDLE_STATUS; ip = IPAddress(); }
    return true;
  }
  bool config(IPAddress local, IPAddress gateway, IPAddress subnet,
              IPAddress dns1 = IPAddress(), IPAddress dns2 = IPAddress()) {
    configurations.push_back({local, gateway, subnet, dns1, dns2});
    return uint32_t(local) == 0 ? !failDhcp : !failStatic;
  }
  void begin(const char *name, const char *password) {
    ++begins; ssid = name; secret = password;
    if (!staleConnectedEvent) statusValue = WL_IDLE_STATUS;
  }
  IPAddress localIP() const { return ip; }
  IPAddress gatewayIP() const { return gateway; }
  void connected(IPAddress address, IPAddress router) { ip = address; gateway = router; statusValue = WL_CONNECTED; }
  int statusValue = WL_IDLE_STATUS;
  int rssi = -45;
  IPAddress ip, gateway;
  bool persistentValue = true, autoReconnect = false;
  bool failDhcp = false, failStatic = false, staleConnectedEvent = false;
  unsigned begins = 0, disconnects = 0;
  std::string ssid, secret;
  std::vector<Config> configurations;
};
extern FakeWiFi WiFi;

namespace testnet {
struct Connection {
  std::vector<uint8_t> bytes;
  size_t cursor = 0;
  bool open = false, stopped = false;
};
}
class WiFiClient {
 public:
  WiFiClient() = default;
  explicit WiFiClient(const std::shared_ptr<testnet::Connection> &wire) : wire_(wire) {}
  int available() const {
    return wire_ && !wire_->stopped ? wire_->bytes.size() - wire_->cursor : 0;
  }
  bool connected() const { return wire_ && wire_->open && !wire_->stopped; }
  explicit operator bool() const { return connected() || available() > 0; }
  int read(uint8_t *output, size_t count) {
    count = std::min(count, size_t(available()));
    if (count) memcpy(output, wire_->bytes.data() + wire_->cursor, count);
    if (wire_) wire_->cursor += count;
    return count;
  }
  void stop() { if (wire_) wire_->stopped = true; }
 private:
  std::shared_ptr<testnet::Connection> wire_;
};
class WiFiServer {
 public:
  explicit WiFiServer(uint16_t) {}
  void begin() { started = true; }
  void end() { started = false; }
  WiFiClient available() {
    if (pending.empty()) return WiFiClient();
    WiFiClient client = pending.front();
    pending.pop_front();
    return client;
  }
  std::shared_ptr<testnet::Connection> enqueue(const std::vector<uint8_t> &bytes,
                                             bool open = false) {
    auto wire = std::make_shared<testnet::Connection>();
    wire->bytes = bytes; wire->open = open;
    pending.push_back(WiFiClient(wire));
    return wire;
  }
  bool started = false;
  std::deque<WiFiClient> pending;
};
