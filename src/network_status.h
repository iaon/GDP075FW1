#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "vendor_protocol.h"
#include "vendor_fonts.h"

namespace vendor {

// Recovered from app0 0x400d2718/0x400d27dc and Paint at 0x400d3500.
// Render one packed native row, so status needs only 200 bytes of RAM.
class NetworkStatus {
 public:
  static void renderRow(const WifiConfig &config, bool connected,
                        uint16_t row, uint8_t *output) {
    memset(output, connected || row < 240 ? 0x55 : 0xaa, 200);
    if (row >= 240) return;
    if (connected) {
      text(output, row, 10, 40, "--WiFi connection successful--", false);
    } else {
      text(output, row, 10, 20, "--WiFi connection failed !!!--", false);
      text(output, row, 10, 40, "--Please check the WiFi configuration !!!--", false);
    }
    text(output, row, 10, 85, "1.WiFi-Name:", false);
    text(output, row, 180, 80, config.ssid, true);
    text(output, row, 10, 115, "2.WiFi-Password:", false);
    text(output, row, 240, 110, config.password, true);
    text(output, row, 10, 145, "3.WiFi-StaticHostNumber:", false);
    char host[4];
    snprintf(host, sizeof(host), "%u", static_cast<unsigned>(config.host));
    text(output, row, 360, 140, host, true);
    // Original failure transfer at 0x400d2684 XORs the upper 48,000 bytes.
    if (!connected)
      for (unsigned i = 0; i < 200; ++i) output[i] ^= 0xff;
  }

 private:
  static void text(uint8_t *output, unsigned row, unsigned left, unsigned top,
                   const char *value, bool large) {
    const unsigned width = large ? 17 : 14, height = large ? 24 : 20;
    const unsigned stride = (width + 7) / 8;
    const uint8_t *font = large ? fonts::kFont24 : fonts::kFont20;
    unsigned x = left, y = top;
    for (; *value; ++value, x += width) {
      // Match vendor string wrapping, but bound non-ASCII glyphs to '?' rather
      // than reading beyond its ASCII 32..126 font tables.
      if (x + width > 800) { x = left; y += height; }
      if (y + height > 240) { x = left; y = top; }
      const uint8_t character = static_cast<uint8_t>(*value);
      const unsigned glyph = (character >= 32 && character <= 126 ? character : '?') - 32;
      if (row < y || row >= y + height) continue;
      const uint8_t *bits = font + (glyph * height + row - y) * stride;
      for (unsigned column = 0; column < width && x + column < 800; ++column) {
        const bool set = (bits[column / 8] & (0x80 >> (column % 8))) != 0;
        // Labels: black ink, transparent white background. Values (including
        // the number): white ink on a red cell, as passed by vendor Paint.
        if (!large && !set) continue;
        const uint8_t colour = large ? (set ? 1 : 3) : 0;
        const unsigned pixel = x + column, shift = 6 - 2 * (pixel % 4);
        output[pixel / 4] = (output[pixel / 4] & ~(3 << shift)) | (colour << shift);
      }
    }
  }
};

}  // namespace vendor
