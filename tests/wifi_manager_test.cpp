#include "wifi_manager.h"
#include "WiFi.h"
#include "Preferences.h"
#include "esp_system.h"
#include <assert.h>

static void save(const char *ssid, const char *password, int host = 42) {
  Preferences p;
  assert(p.begin("wifi_config"));
  p.putString("ssid", ssid); p.putString("password", password); p.putInt("host_number", host);
  p.end();
}
static void connect(WifiManager &manager, unsigned host = 208) {
  WiFi.connected(IPAddress(10, 20, 30, 90), IPAddress(10, 20, 30, 1));
  manager.poll();
  assert(!manager.ready());
  const auto &config = WiFi.configurations.back();
  assert(config.local == IPAddress(10, 20, 30, host));
  assert(config.gateway == IPAddress(10, 20, 30, 1));
  assert(config.subnet == IPAddress(255, 255, 255, 0));
  assert(config.dns1 == config.gateway && config.dns2 == config.gateway);
  WiFi.connected(config.local, config.gateway);
  manager.poll();
  assert(manager.ready());
}

int main() {
  testhw::reset();
  WifiManager defaults;
  defaults.begin();
  assert(!strcmp(defaults.config().ssid, "Goodisplay"));
  assert(!strcmp(defaults.config().password, "12345678"));
  assert(defaults.config().host == 208);
  assert(WiFi.autoReconnect && !WiFi.persistentValue);
  assert(uint32_t(WiFi.configurations.front().local) == 0);
  connect(defaults);
  assert(defaults.takeStatusUpdate() && !defaults.takeStatusUpdate());
  assert(testnvs::namespaces["restart_counter"]["count"].integer == 0);

  // Drop and restore a connection through SDK auto-reconnect.
  WiFi.statusValue = WL_IDLE_STATUS;
  defaults.poll();
  assert(!defaults.ready());
  const unsigned beginCalls = WiFi.begins;
  WiFi.connected(IPAddress(10, 20, 30, 208), IPAddress(10, 20, 30, 1));
  defaults.poll();
  assert(defaults.ready() && WiFi.begins == beginCalls);
  WiFi.statusValue = WL_IDLE_STATUS;
  defaults.poll();
  testhw::clockMs += 30000;
  defaults.poll();
  assert(!defaults.ready() && WiFi.begins == beginCalls + 1);
  connect(defaults);

  testhw::reset();
  save("Saved SSID", "Saved password", 77);
  WifiManager saved;
  saved.begin();
  assert(!strcmp(saved.config().ssid, "Saved SSID") && saved.config().host == 77);
  connect(saved, 77);

  testhw::reset();
  save("Legacy SSID", "Legacy password");
  testnvs::namespaces["wifi_config"].erase("host_number");
  WifiManager legacy;
  legacy.begin();
  assert(!strcmp(legacy.config().ssid, "Legacy SSID") && legacy.config().host == 208);

  for (int badHost : {0, 255, -1}) {
    testhw::reset(); save("Bad", "Bad", badHost);
    WifiManager bad; bad.begin();
    assert(!strcmp(bad.config().ssid, "Goodisplay") && bad.config().host == 208);
  }
  testhw::reset(); save("Typed", "Typed");
  testnvs::namespaces["wifi_config"]["host_number"].type = testnvs::Value::U32;
  WifiManager badType; badType.begin();
  assert(!strcmp(badType.config().ssid, "Goodisplay"));

  // Neither a stale connected event nor the DHCP address proves static setup.
  testhw::reset(); WifiManager stale; stale.begin();
  WiFi.connected(IPAddress(10, 20, 30, 90), IPAddress(10, 20, 30, 1));
  WiFi.staleConnectedEvent = true;
  stale.poll(); stale.poll();
  assert(!stale.ready());
  testhw::clockMs += 30000;
  stale.poll();
  assert(!stale.ready() && stale.takeStatusUpdate());

  for (int failure = 0; failure < 4; ++failure) {
    testhw::reset();
    if (failure == 0) WiFi.failDhcp = true;
    WifiManager bad; bad.begin();
    if (failure == 1) { testhw::clockMs += 30000; bad.poll(); }
    if (failure == 2) {
      WiFi.failStatic = true;
      WiFi.connected(IPAddress(10, 20, 30, 90), IPAddress(10, 20, 30, 1)); bad.poll();
    }
    if (failure == 3) {
      WiFi.connected(IPAddress(10, 20, 30, 90), IPAddress(10, 20, 30, 208)); bad.poll();
    }
    assert(!bad.ready() && bad.takeStatusUpdate());
  }

  // Regression: failed retries requested a full refresh every ~60 seconds,
  // mimicking a bootloop. Only the first failure and state changes redraw.
  testhw::reset();
  testhw::clockMs = 0xffff0000;  // Include millis wrap during retry cycles.
  WifiManager retries; retries.begin();
  for (unsigned attempt = 1; attempt <= 5; ++attempt) {
    testhw::clockMs += 30000; retries.poll();
    assert(!retries.ready() && retries.attempts() == attempt);
    assert(retries.takeStatusUpdate() == (attempt == 1));
    assert(!retries.takeStatusUpdate() && !ESP.restarts);
    if (attempt != 5) { testhw::clockMs += 30000; retries.poll(); }
  }
  assert(Serial.output.find("WIFI timeout phase=DHCP") != std::string::npos);
  testhw::clockMs += 30000; retries.poll(); connect(retries);
  assert(retries.takeStatusUpdate());
  WiFi.statusValue = WL_IDLE_STATUS; retries.poll();
  testhw::clockMs += 30000; retries.poll();
  testhw::clockMs += 30000; retries.poll();
  assert(!retries.ready() && retries.takeStatusUpdate() && !ESP.restarts);

  // Serial remains usable during connection attempts and saves actual I32 NVS.
  testhw::reset(); WifiManager serial; serial.begin();
  Serial.feed("WIFI \"New network\" \"\" 93\n");
  bool restarted = false;
  try { serial.poll(); } catch (const Restarted &) { restarted = true; }
  assert(restarted && ESP.restarts == 1);
  assert(testnvs::namespaces["wifi_config"]["ssid"].text == "New network");
  assert(testnvs::namespaces["wifi_config"]["password"].text.empty());
  assert(testnvs::namespaces["wifi_config"]["host_number"].type == testnvs::Value::I32);
  assert(testnvs::namespaces["wifi_config"]["host_number"].integer == 93);
  WifiManager reboot; reboot.begin();
  assert(reboot.config().host == 93 && reboot.config().password[0] == 0);

  testhw::reset(); WifiManager fragmented; fragmented.begin();
  std::vector<uint8_t> packet(64, 0);
  packet[0] = 4; memcpy(packet.data() + 1, "Test", 4);
  packet[30] = 8; memcpy(packet.data() + 31, "password", 8);
  packet[60] = 1; packet[61] = 71; packet[62] = 13; packet[63] = 10;
  Serial.feed(std::vector<uint8_t>(packet.begin(), packet.begin() + 17));
  fragmented.poll();
  assert(!ESP.restarts && !testnvs::namespaces.count("wifi_config"));
  Serial.feed(std::vector<uint8_t>(packet.begin() + 17, packet.end()));
  try { fragmented.poll(); } catch (const Restarted &) {}
  assert(ESP.restarts == 1 && testnvs::namespaces["wifi_config"]["host_number"].integer == 71);

  for (bool failWrite : {false, true}) {
    testhw::reset(); WifiManager bad; bad.begin();
    if (failWrite) testnvs::failWrite = "host_number";
    else packet[62] = 0;
    Serial.feed(packet); bad.poll();
    assert(ESP.restarts == 0);
    packet[62] = 13;
  }
  testhw::reset(); WifiManager timeout; timeout.begin();
  Serial.feed(std::vector<uint8_t>(packet.begin(), packet.begin() + 4)); timeout.poll();
  testhw::clockMs += 2000; timeout.poll();
  assert(ESP.restarts == 0 && !testnvs::namespaces.count("wifi_config"));
  assert(Serial.output.find("Invalid data length") != std::string::npos);
  assert(Serial.output.find("received=4 expected=64") != std::string::npos);

  for (const char *ending : {"\n", "\r", "\r\n"}) {
    testhw::reset(); WifiManager terminal; terminal.begin();
    Serial.feed(ending); terminal.poll();
    testhw::clockMs += 2000; terminal.poll();
    assert(Serial.output.find("Invalid data length") == std::string::npos);
    assert(Serial.output.find("ignored standalone line ending") != std::string::npos);
    assert(!ESP.restarts && !testnvs::namespaces.count("wifi_config"));
  }

  // CR/LF are also valid binary SSID lengths. Do not discard them at receive
  // time, or a continuation buffered during a slow display refresh.
  for (unsigned length : {10u, 13u}) {
    testhw::reset(); WifiManager binary; binary.begin();
    auto longPacket = packet;
    longPacket[0] = length;
    memset(longPacket.data() + 1, 0, 29);
    memset(longPacket.data() + 1, 'A', length);
    Serial.feed(std::vector<uint8_t>{longPacket[0]}); binary.poll();
    testhw::clockMs += 25000;
    Serial.feed(std::vector<uint8_t>(longPacket.begin() + 1, longPacket.end()));
    try { binary.poll(); } catch (const Restarted &) {}
    assert(ESP.restarts == 1);
    assert(testnvs::namespaces["wifi_config"]["ssid"].text == std::string(length, 'A'));
  }

  testhw::reset(); fakeResetReason = ESP_RST_SW;
  testnvs::namespaces["restart_counter"]["count"] = {testnvs::Value::I32, "", 7};
  WifiManager counter; counter.begin();
  assert(testnvs::namespaces["restart_counter"]["count"].integer == 7);
  connect(counter);
  assert(testnvs::namespaces["restart_counter"]["count"].integer == 0);
  puts("Wi-Fi DHCP/static, reconnect, serial, saved NVS and failure tests passed");
}
