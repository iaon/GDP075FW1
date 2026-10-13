#pragma once

#include "vendor_protocol.h"

class FrameStorage {
 public:
  virtual ~FrameStorage() = default;
  virtual bool open() = 0;
  virtual size_t write(const uint8_t *bytes, size_t size) = 0;
  virtual void close() = 0;
};

// Shared by the actual HTTP/TCP handlers and the host-side tests.
class FrameUpload {
 public:
  enum class Owner { None, Http, Tcp };
  enum class Error { None, StorageUnavailable, WriteFailed, InvalidLength,
                     InvalidField, Aborted, Timeout };
  explicit FrameUpload(FrameStorage &storage) : storage_(storage) {}
  Owner owner() const { return owner_; }
  Error error() const { return error_; }
  size_t received() const { return received_; }
  bool begin(Owner owner, uint32_t now) {
    if (owner == Owner::None || owner_ != Owner::None) return false;
    received_ = 0;
    error_ = Error::None;
    if (!storage_.open()) { error_ = Error::StorageUnavailable; return false; }
    owner_ = owner;
    lastByte_ = now;
    return true;
  }
  bool append(Owner owner, const uint8_t *bytes, size_t size, uint32_t now) {
    if (owner == Owner::None || owner != owner_ || error_ != Error::None) return false;
    if (size > vendor::kFrameBytes - received_) {
      fail(Error::InvalidLength);
      return false;
    }
    if (size && storage_.write(bytes, size) != size) {
      fail(Error::WriteFailed);
      return false;
    }
    received_ += size;
    if (size) lastByte_ = now;
    return true;
  }
  void fail(Error error) { if (error_ == Error::None) error_ = error; }
  void abort(Owner owner) {
    if (owner != Owner::None && owner_ == owner) {
      fail(Error::Aborted);
      storage_.close();
      owner_ = Owner::None;
    }
  }
  bool tcpFinished(uint32_t now, bool connected, bool buffered) {
    if (owner_ != Owner::Tcp) return false;
    if (error_ != Error::None || (!connected && !buffered)) return true;
    if (uint32_t(now - lastByte_) >= kTimeoutMs) {
      fail(Error::Timeout);
      return true;
    }
    return false;
  }
  bool finish(Owner owner) {
    if (owner == Owner::None || owner != owner_) return false;
    storage_.close();
    owner_ = Owner::None;
    if (received_ != vendor::kFrameBytes) fail(Error::InvalidLength);
    return error_ == Error::None;
  }

 private:
  static constexpr uint32_t kTimeoutMs = 30000;
  FrameStorage &storage_;
  Owner owner_ = Owner::None;
  Error error_ = Error::None;
  size_t received_ = 0;
  uint32_t lastByte_ = 0;
};

template <typename Client, typename Server>
class TcpFrameReceiver {
 public:
  enum class Result { None, Ready, Rejected };
  explicit TcpFrameReceiver(FrameUpload &upload) : upload_(upload) {}
  void stop() {
    client_.stop();
    upload_.abort(FrameUpload::Owner::Tcp);
  }
  Result poll(Server &server, uint32_t now) {
    if (upload_.owner() == FrameUpload::Owner::Http) return Result::None;
    if (upload_.owner() != FrameUpload::Owner::Tcp) {
      Client candidate = server.available();
      if (!candidate) return Result::None;
      if (!upload_.begin(FrameUpload::Owner::Tcp, now)) {
        candidate.stop();
        return Result::Rejected;
      }
      client_ = candidate;
    }
    uint8_t buffer[1024];
    for (unsigned chunk = 0; chunk < 4 && client_.available(); ++chunk) {
      const int size = client_.read(buffer, sizeof(buffer));
      if (size < 0) { upload_.fail(FrameUpload::Error::Aborted); break; }
      if (!size) break;
      if (!upload_.append(FrameUpload::Owner::Tcp, buffer, size, now)) break;
    }
    if (!upload_.tcpFinished(now, client_.connected(), client_.available() > 0))
      return Result::None;
    client_.stop();
    return upload_.finish(FrameUpload::Owner::Tcp) ? Result::Ready : Result::Rejected;
  }

 private:
  FrameUpload &upload_;
  Client client_;
};
