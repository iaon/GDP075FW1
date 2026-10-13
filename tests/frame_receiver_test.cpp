#include "frame_receiver.h"
#include <assert.h>
#include <algorithm>
#include <deque>
#include <memory>
#include <stdio.h>
#include <vector>

struct Storage : FrameStorage {
  bool open() override { ++opens; data.clear(); return available; }
  size_t write(const uint8_t *bytes, size_t size) override {
    const size_t count = std::min(size, capacity - data.size());
    data.insert(data.end(), bytes, bytes + count);
    return count;
  }
  void close() override { ++closes; }
  bool available = true;
  size_t capacity = vendor::kFrameBytes;
  unsigned opens = 0, closes = 0;
  std::vector<uint8_t> data;
};
struct Wire {
  std::vector<uint8_t> bytes;
  size_t position = 0, chunk = 1024;
  bool open = false, stopped = false, readError = false;
  unsigned reads = 0;
};
struct Client {
  Client() = default;
  explicit Client(const std::shared_ptr<Wire> &wire) : wire(wire) {}
  int available() const { return wire && !wire->stopped ? wire->bytes.size() - wire->position : 0; }
  bool connected() const { return wire && wire->open && !wire->stopped; }
  explicit operator bool() const { return connected() || available() > 0; }
  int read(uint8_t *out, size_t size) {
    ++wire->reads;
    if (wire->readError) return -1;
    size = std::min(std::min(size, wire->chunk), size_t(available()));
    memcpy(out, wire->bytes.data() + wire->position, size);
    wire->position += size;
    return size;
  }
  void stop() { if (wire) wire->stopped = true; }
  std::shared_ptr<Wire> wire;
};
struct Server {
  Client available() {
    if (clients.empty()) return Client();
    Client client = clients.front(); clients.pop_front(); return client;
  }
  std::shared_ptr<Wire> add(const std::vector<uint8_t> &bytes, bool open = false, size_t chunk = 1024) {
    auto wire = std::make_shared<Wire>();
    wire->bytes = bytes; wire->open = open; wire->chunk = chunk;
    clients.push_back(Client(wire)); return wire;
  }
  std::deque<Client> clients;
};
using Receiver = TcpFrameReceiver<Client, Server>;

static Receiver::Result drain(Receiver &receiver, Server &server, uint32_t &now,
                              const std::shared_ptr<Wire> &wire) {
  for (unsigned iteration = 0; iteration < 10000; ++iteration) {
    const unsigned before = wire->reads;
    const auto result = receiver.poll(server, now++);
    assert(wire->reads - before <= 4);  // UART work is never starved by a full frame.
    if (result != Receiver::Result::None) return result;
  }
  assert(false && "receiver failed to finish");
  return Receiver::Result::None;
}

int main() {
  std::vector<uint8_t> payload(vendor::kFrameBytes);
  for (size_t index = 0; index < payload.size(); ++index) payload[index] = index * 37;
  for (size_t chunk : {size_t(17), size_t(1024)}) {
    Storage storage; FrameUpload upload(storage); Receiver receiver(upload); Server server;
    auto wire = server.add(payload, false, chunk);
    uint32_t now = 0xfffffff0;
    assert(drain(receiver, server, now, wire) == Receiver::Result::Ready);
    assert(storage.data == payload && storage.opens == 1 && storage.closes == 1);
    assert(wire->stopped && upload.owner() == FrameUpload::Owner::None);
  }

  for (size_t length : {size_t(123), vendor::kFrameBytes + 1}) {
    Storage storage; FrameUpload upload(storage); Receiver receiver(upload); Server server;
    auto wire = server.add(std::vector<uint8_t>(length, 0x55)); uint32_t now = 0;
    assert(drain(receiver, server, now, wire) == Receiver::Result::Rejected);
    assert(upload.error() == FrameUpload::Error::InvalidLength && wire->stopped);
    assert(storage.data.size() <= vendor::kFrameBytes);
    wire = server.add(payload);
    assert(drain(receiver, server, now, wire) == Receiver::Result::Ready);
    assert(storage.data == payload && upload.error() == FrameUpload::Error::None);
  }

  for (int fault = 0; fault < 3; ++fault) {
    Storage storage; FrameUpload upload(storage); Receiver receiver(upload); Server server;
    if (fault == 0) storage.available = false;
    if (fault == 1) storage.capacity = 2050;
    auto wire = server.add(payload, true);
    if (fault == 2) wire->readError = true;
    uint32_t now = 0;
    assert(drain(receiver, server, now, wire) == Receiver::Result::Rejected);
    const auto expected = fault == 0 ? FrameUpload::Error::StorageUnavailable :
                          fault == 1 ? FrameUpload::Error::WriteFailed : FrameUpload::Error::Aborted;
    assert(upload.error() == expected && wire->stopped);
  }

  // EOF is required even after a full payload; a stalled connection is rejected.
  for (bool full : {false, true}) {
    Storage storage; FrameUpload upload(storage); Receiver receiver(upload); Server server;
    auto wire = server.add(full ? payload : std::vector<uint8_t>{}, true);
    uint32_t now = 0xfffffff0;
    do { assert(receiver.poll(server, now) == Receiver::Result::None); }
    while (wire->position < wire->bytes.size());
    assert(receiver.poll(server, now + 29999) == Receiver::Result::None);
    assert(receiver.poll(server, now + 30000) == Receiver::Result::Rejected);
    assert(upload.error() == FrameUpload::Error::Timeout && wire->stopped);
  }

  Storage storage; FrameUpload upload(storage); Receiver receiver(upload); Server server;
  assert(upload.begin(FrameUpload::Owner::Http, 0));
  assert(!upload.begin(FrameUpload::Owner::Tcp, 0));
  assert(!upload.append(FrameUpload::Owner::Tcp, payload.data(), 100, 0));
  assert(upload.owner() == FrameUpload::Owner::Http && upload.received() == 0);
  auto wire = server.add(payload);
  assert(receiver.poll(server, 0) == Receiver::Result::None && server.clients.size() == 1);
  assert(upload.append(FrameUpload::Owner::Http, payload.data(), payload.size(), 0));
  assert(upload.finish(FrameUpload::Owner::Http));
  uint32_t now = 1;
  assert(drain(receiver, server, now, wire) == Receiver::Result::Ready);

  wire = server.add(payload, true);
  assert(receiver.poll(server, now) == Receiver::Result::None);
  receiver.stop();
  assert(upload.error() == FrameUpload::Error::Aborted && wire->stopped);
  assert(upload.owner() == FrameUpload::Owner::None);
  assert(upload.begin(FrameUpload::Owner::Http, now));
  upload.fail(FrameUpload::Error::InvalidField);
  assert(!upload.append(FrameUpload::Owner::Http, payload.data(), payload.size(), now));
  assert(!upload.finish(FrameUpload::Owner::Http));
  assert(upload.error() == FrameUpload::Error::InvalidField);
  puts("TCP/HTTP ownership, fragmented frames, EOF, timeout, storage faults and recovery tests passed");
}
