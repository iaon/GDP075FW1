#include "deferred_mount.h"
#include <assert.h>
#include <stdio.h>

static unsigned mounts = 0;
static bool available = false;
static bool mount() { ++mounts; return available; }

int main() {
  DeferredMount storage(mount);
  // Boot/status queries must never mount, format or open storage.
  for (unsigned i = 0; i < 100; ++i) assert(!storage.ready());
  assert(mounts == 0);
  // A failed first upload does not claim storage is ready; a later one retries.
  assert(!storage.ensureReady() && mounts == 1);
  available = true;
  assert(storage.ensureReady() && mounts == 2);
  for (unsigned i = 0; i < 100; ++i) assert(storage.ensureReady());
  assert(mounts == 2 && storage.ready());
  DeferredMount unavailable(nullptr);
  assert(!unavailable.ensureReady());
  puts("Deferred filesystem: no boot mount, explicit upload, retry and one-time mount passed");
}
