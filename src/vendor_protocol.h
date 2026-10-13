#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace vendor {

constexpr uint16_t kImagePort = 8000;
constexpr size_t kFrameBytes = 800 * 480 / 4;
constexpr size_t kProvisioningBytes = 64;
constexpr uint8_t kDefaultHost = 208;

struct WifiConfig {
  char ssid[33] = "Goodisplay";
  char password[65] = "12345678";
  uint8_t host = kDefaultHost;
};

enum class DecodeResult { Ok, InvalidLength, InvalidEndMarker, InvalidHost };

inline bool validHost(unsigned host) { return host >= 1 && host <= 254; }

// Recovered from app0 at 0x400d290c. Host is a raw octet, not ASCII digits.
inline DecodeResult decodePacket(const uint8_t *packet, size_t size,
                                 WifiConfig &config) {
  if (size != kProvisioningBytes) return DecodeResult::InvalidLength;
  if (packet[62] != '\r' || packet[63] != '\n')
    return DecodeResult::InvalidEndMarker;
  if (packet[0] == 0 || packet[0] > 29 || packet[30] > 29 || packet[60] != 1)
    return DecodeResult::InvalidLength;
  if (!validHost(packet[61])) return DecodeResult::InvalidHost;
  if (memchr(packet + 1, 0, packet[0]) ||
      memchr(packet + 31, 0, packet[30]))
    return DecodeResult::InvalidLength;
  WifiConfig candidate;
  memset(candidate.ssid, 0, sizeof(candidate.ssid));
  memset(candidate.password, 0, sizeof(candidate.password));
  memcpy(candidate.ssid, packet + 1, packet[0]);
  memcpy(candidate.password, packet + 31, packet[30]);
  candidate.host = packet[61];
  config = candidate;
  return DecodeResult::Ok;
}

inline bool readToken(const char *&input, char *output, size_t capacity) {
  while (*input == ' ') ++input;
  if (!*input) return false;
  const bool quoted = *input == '"';
  if (quoted) ++input;
  size_t length = 0;
  while (*input && (quoted ? *input != '"' : *input != ' ')) {
    if (length + 1 >= capacity) return false;
    output[length++] = *input++;
  }
  if (quoted && *input++ != '"') return false;
  output[length] = 0;
  return !quoted || !*input || *input == ' ';
}

// Retain the project's text command as a convenience, with quoted fields.
inline bool decodeText(const char *input, WifiConfig &config) {
  if (strncmp(input, "WIFI ", 5)) return false;
  input += 5;
  WifiConfig candidate;
  candidate.host = config.host;
  if (!readToken(input, candidate.ssid, sizeof(candidate.ssid)) ||
      !candidate.ssid[0] ||
      !readToken(input, candidate.password, sizeof(candidate.password)))
    return false;
  while (*input == ' ') ++input;
  if (*input) {
    unsigned host = 0;
    size_t digits = 0;
    while (*input >= '0' && *input <= '9') {
      if (++digits > 3) return false;
      host = host * 10 + (*input++ - '0');
    }
    while (*input == ' ') ++input;
    if (!digits || *input || !validHost(host)) return false;
    candidate.host = host;
  }
  config = candidate;
  return true;
}

// MSB-first: black=00, white=01, yellow=10, red=11 (four pixels per byte).
inline uint8_t pixel(uint8_t packed, unsigned index) {
  return (packed >> (6 - 2 * index)) & 3;
}

}  // namespace vendor
