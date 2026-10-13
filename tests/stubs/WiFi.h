#pragma once
#include "IPAddress.h"
constexpr int WIFI_STA = 1, WL_IDLE_STATUS = 0, WL_CONNECTED = 3;
class FakeWiFi {
 public:
  struct Config { IPAddress local, gateway, subnet, dns1, dns2; };
  int status() const { return statusValue; }
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
  IPAddress ip, gateway;
  bool persistentValue = true, autoReconnect = false;
  bool failDhcp = false, failStatic = false, staleConnectedEvent = false;
  unsigned begins = 0, disconnects = 0;
  std::string ssid, secret;
  std::vector<Config> configurations;
};
extern FakeWiFi WiFi;
