#pragma once

#include "Arduino.h"
#include <algorithm>
#include <limits>
#include <map>
#include <memory>

constexpr const char *FILE_READ = "r", *FILE_WRITE = "w";
enum SeekMode { SeekSet, SeekCur, SeekEnd };

namespace testfs {
struct State {
  std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
  bool mountAvailable = true, readOpenAvailable = true, writeOpenAvailable = true;
  bool formatAvailable = true, seekAvailable = true;
  size_t writeLimit = std::numeric_limits<size_t>::max();
  size_t readLimit = std::numeric_limits<size_t>::max();
  unsigned mounts = 0, formats = 0, readOpens = 0, writeOpens = 0, closes = 0;
};
inline State &state() { static State value; return value; }
inline void reset() { state() = State(); }
}

class File {
 public:
  File() = default;
  explicit File(const std::shared_ptr<std::vector<uint8_t>> &bytes)
      : bytes_(bytes) {}
  explicit operator bool() const { return bool(bytes_); }
  size_t size() const { return bytes_ ? bytes_->size() : 0; }
  size_t write(const uint8_t *input, size_t count) {
    if (!bytes_ || bytes_->size() >= testfs::state().writeLimit) return 0;
    count = std::min(count, testfs::state().writeLimit - bytes_->size());
    bytes_->insert(bytes_->end(), input, input + count);
    return count;
  }
  size_t read(uint8_t *output, size_t count) {
    if (!bytes_ || cursor_ >= bytes_->size() || cursor_ >= testfs::state().readLimit)
      return 0;
    count = std::min(count, bytes_->size() - cursor_);
    count = std::min(count, testfs::state().readLimit - cursor_);
    memcpy(output, bytes_->data() + cursor_, count);
    cursor_ += count;
    return count;
  }
  bool seek(size_t offset, SeekMode origin) {
    if (!bytes_ || !testfs::state().seekAvailable || origin != SeekSet || offset > size())
      return false;
    cursor_ = offset;
    return true;
  }
  void close() { if (bytes_) ++testfs::state().closes; bytes_.reset(); }

 private:
  std::shared_ptr<std::vector<uint8_t>> bytes_;
  size_t cursor_ = 0;
};
