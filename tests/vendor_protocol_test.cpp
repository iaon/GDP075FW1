#include "vendor_protocol.h"

#include <assert.h>
#include <initializer_list>
#include <stdio.h>

int main() {
  uint8_t packet[64] = {};
  packet[0] = 10;
  memcpy(packet + 1, "Goodisplay", 10);
  packet[30] = 8;
  memcpy(packet + 31, "12345678", 8);
  packet[60] = 1;
  packet[61] = 208;
  packet[62] = '\r';
  packet[63] = '\n';
  vendor::WifiConfig config;
  assert(vendor::decodePacket(packet, sizeof(packet), config) == vendor::DecodeResult::Ok);
  assert(!strcmp(config.ssid, "Goodisplay"));
  assert(!strcmp(config.password, "12345678"));
  assert(config.host == 208);

  // The raw host octet may itself be a CR/LF; only bytes 62/63 terminate.
  packet[61] = 10;
  assert(vendor::decodePacket(packet, sizeof(packet), config) == vendor::DecodeResult::Ok);
  assert(config.host == 10);
  packet[0] = packet[30] = 29;
  memset(packet + 1, 's', 29);
  memset(packet + 31, 'p', 29);
  packet[61] = 254;
  assert(vendor::decodePacket(packet, sizeof(packet), config) == vendor::DecodeResult::Ok);
  assert(strlen(config.ssid) == 29 && strlen(config.password) == 29);
  assert(config.host == 254);

  const vendor::WifiConfig valid = config;
  assert(vendor::decodePacket(packet, 63, config) == vendor::DecodeResult::InvalidLength);
  packet[62] = 0;
  assert(vendor::decodePacket(packet, 64, config) == vendor::DecodeResult::InvalidEndMarker);
  packet[62] = '\r';
  packet[0] = 30;
  assert(vendor::decodePacket(packet, 64, config) == vendor::DecodeResult::InvalidLength);
  packet[0] = 29;
  packet[60] = 2;
  assert(vendor::decodePacket(packet, 64, config) == vendor::DecodeResult::InvalidLength);
  packet[60] = 1;
  packet[61] = 0;
  assert(vendor::decodePacket(packet, 64, config) == vendor::DecodeResult::InvalidHost);
  packet[61] = 255;
  assert(vendor::decodePacket(packet, 64, config) == vendor::DecodeResult::InvalidHost);
  assert(!memcmp(&config, &valid, sizeof(config)));

  assert(vendor::decodeText("WIFI Goodisplay 12345678 208", config));
  assert(!strcmp(config.ssid, "Goodisplay") && config.host == 208);
  assert(vendor::decodeText("WIFI \"Office WiFi\" \"password with spaces\" 42", config));
  assert(!strcmp(config.ssid, "Office WiFi") && config.host == 42);
  assert(vendor::decodeText("WIFI legacy old-password", config));
  assert(config.host == 42);  // Legacy command preserves the selected host.
  assert(vendor::decodeText("WIFI open-network \"\" 208", config));
  assert(config.password[0] == 0);
  const vendor::WifiConfig textValid = config;
  for (const char *invalid : {"WIFI", "WIFI ssid", "WIFI ssid pwd 0",
                             "WIFI ssid pwd 255", "WIFI ssid pwd 1234",
                             "WIFI ssid pwd garbage", "WIFI \"unterminated",
                             "WIFI \"ssid\"junk pwd", "WIFI \"\" pwd 208",
                             "WIFI ssid pwd 208 trailing"}) {
    assert(!vendor::decodeText(invalid, config));
    assert(!memcmp(&config, &textValid, sizeof(config)));
  }
  // 0x1b encodes all four palette indices from left to right.
  for (unsigned index = 0; index < 4; ++index)
    assert(vendor::pixel(0x1b, index) == index);
  static_assert(vendor::kFrameBytes == 96000, "vendor frame size");
  static_assert(vendor::kImagePort == 8000, "vendor TCP port");
  puts("Vendor provisioning and pixel format tests passed");
}
