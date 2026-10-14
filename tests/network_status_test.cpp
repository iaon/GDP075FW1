#include "network_status.h"
#include "vendor_display.h"
#include <assert.h>
#include <vector>

// Separate full-frame reference transcription of app0's Paint calls. Unlike
// production's row renderer, this stamps every glyph into a 240-row canvas.
static void originalText(std::vector<uint8_t> &frame, int startX, int startY,
                         const char *text, bool large, int first, int second) {
  const int w = large ? 17 : 14, h = large ? 24 : 20, stride = (w + 7) / 8;
  const auto *font = large ? vendor::fonts::kFont24 : vendor::fonts::kFont20;
  int x = startX, y = startY;
  for (; *text; ++text) {
    if (x + w > 800) { x = startX; y += h; }
    if (y + h > 240) { x = startX; y = startY; }
    unsigned c = static_cast<uint8_t>(*text);
    if (c < 32 || c > 126) c = '?';
    const auto *glyph = font + (c - 32) * stride * h;
    for (int dy = 0; dy < h; ++dy) {
      for (int dx = 0; dx < w; ++dx) {
        const bool bit = glyph[dy * stride + dx / 8] & (128 >> (dx & 7));
        // 0x400d3500: first == 1 skips unset bits; set bits use second.
        if (first == 1 && !bit) continue;
        const unsigned colour = bit ? second : first;
        const unsigned pixel = (y + dy) * 800 + x + dx;
        const unsigned shift = (3 - (pixel % 4)) * 2;
        frame[pixel / 4] = (frame[pixel / 4] & ~(3 << shift)) | (colour << shift);
      }
    }
    x += w;
  }
}

static std::vector<uint8_t> original(const vendor::WifiConfig &config, bool success) {
  std::vector<uint8_t> frame(96000, 0x55);
  if (success) originalText(frame, 10, 40, "--WiFi connection successful--", false, 1, 0);
  else {
    originalText(frame, 10, 20, "--WiFi connection failed !!!--", false, 1, 0);
    originalText(frame, 10, 40, "--Please check the WiFi configuration !!!--", false, 1, 0);
  }
  originalText(frame, 10, 85, "1.WiFi-Name:", false, 1, 0);
  originalText(frame, 180, 80, config.ssid, true, 3, 1);
  originalText(frame, 10, 115, "2.WiFi-Password:", false, 1, 0);
  originalText(frame, 240, 110, config.password, true, 3, 1);
  originalText(frame, 10, 145, "3.WiFi-StaticHostNumber:", false, 1, 0);
  char number[4]; snprintf(number, sizeof(number), "%u", config.host);
  // Paint_DrawNum swaps its two colour arguments before Paint_DrawString.
  originalText(frame, 360, 140, number, true, 3, 1);
  if (!success) {
    for (unsigned i = 0; i < 48000; ++i) frame[i] ^= 255;
    std::fill(frame.begin() + 48000, frame.end(), 0xaa);
  }
  return frame;
}

int main() {
  for (unsigned variant = 0; variant < 4; ++variant) {
    vendor::WifiConfig config;
    if (variant == 1) { config.ssid[0] = 0; config.password[0] = 0; config.host = 1; }
    if (variant == 2) {
      memset(config.ssid, 'W', 32); config.ssid[32] = 0;
      memset(config.password, 'g', 64); config.password[64] = 0; config.host = 254;
    }
    if (variant == 3) { strcpy(config.ssid, "\xff\x01~ "); strcpy(config.password, "\x80~ !"); }
    for (bool success : {false, true}) {
      const auto reference = original(config, success);
      std::vector<uint8_t> actual(96000);
      for (uint16_t y = 0; y < 480; ++y)
        vendor::NetworkStatus::renderRow(config, success, y, actual.data() + y * 200);
      assert(actual == reference);
      for (unsigned i = 48000; i < 96000; ++i) assert(actual[i] == (success ? 0x55 : 0xaa));

      // End-to-end driver transfer: one command 0x10, exactly one native frame.
      testhw::reset(); VendorGDEM075F52 display(27, 14, 12, 13); display.init();
      display.setPaged();
      uint8_t row[200];
      for (uint16_t y = 0; y < 480; ++y) {
        vendor::NetworkStatus::renderRow(config, success, y, row);
        display.writeNative(row, nullptr, 0, y, 800, 1);
      }
      assert(trace::commands.back().command == 0x10);
      assert(trace::commands.back().data == reference);
      unsigned writes = 0;
      for (const auto &command : trace::commands) if (command.command == 0x10) ++writes;
      assert(writes == 1);
      display.refresh(); display.hibernate(); assert(display.ok());
    }
  }
  puts("Vendor status fonts/layout/colours, wrapping, safe glyphs and native driver transfer passed");
}
