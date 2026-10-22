#pragma once

#include "FS.h"

class FakeSPIFFS {
 public:
  bool begin(bool) { ++testfs::state().mounts; return testfs::state().mountAvailable; }
  bool format() {
    ++testfs::state().formats;
    if (!testfs::state().formatAvailable) return false;
    testfs::state().files.clear();
    testfs::state().mountAvailable = true;
    return true;
  }
  File open(const char *path, const char *mode) {
    auto &state = testfs::state();
    if (!strcmp(mode, FILE_WRITE)) {
      ++state.writeOpens;
      if (!state.writeOpenAvailable) return File();
      auto data = std::make_shared<std::vector<uint8_t>>();
      state.files[path] = data;
      return File(data);
    }
    ++state.readOpens;
    const auto found = state.files.find(path);
    return state.readOpenAvailable && found != state.files.end()
               ? File(found->second) : File();
  }
};
extern FakeSPIFFS SPIFFS;
