#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

constexpr int ESP_OK = 0, ESP_PARTITION_TYPE_DATA = 1;
constexpr int ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 0x82;
struct esp_partition_t { size_t size; };
namespace testpartition {
struct State {
  bool available = false, readable = true;
  uint8_t fill = 0xff;
  esp_partition_t partition{4096};
};
inline State &state() { static State value; return value; }
}
inline const esp_partition_t *esp_partition_find_first(int, int, const char *) {
  return testpartition::state().available ? &testpartition::state().partition : nullptr;
}
inline int esp_partition_read(const esp_partition_t *, size_t, void *out, size_t size) {
  if (!testpartition::state().readable) return -1;
  memset(out, testpartition::state().fill, size);
  return ESP_OK;
}
