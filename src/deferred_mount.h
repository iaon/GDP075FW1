#pragma once

// Construction and status drawing never mount/format writable storage.
// Only an explicit upload requests it; failed mounts may be retried safely.
class DeferredMount {
 public:
  explicit DeferredMount(bool (*mount)()) : mount_(mount) {}
  bool ensureReady() {
    if (!ready_ && mount_) ready_ = mount_();
    return ready_;
  }
  bool ready() const { return ready_; }

 private:
  bool (*mount_)();
  bool ready_ = false;
};
