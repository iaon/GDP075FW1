#include "Arduino.h"
#include "SPI.h"
#include "WiFi.h"
#include "Preferences.h"
#include "esp_system.h"
#include <algorithm>

FakeSerial Serial;
FakeEsp ESP;
SPIClass SPI;
FakeWiFi WiFi;
esp_reset_reason_t fakeResetReason = ESP_RST_POWERON;
namespace trace { std::vector<Command> commands; }
namespace testnvs {
std::map<std::string, std::map<std::string, Value>> namespaces;
std::string failOpen, failWrite;
unsigned writableOpens = 0, writes = 0;
}
namespace testhw {
uint64_t clockMs = 0;
int pins[40];
bool busyStuck = false;
int stuckOnCommand = -1;
uint32_t commandBusyMs[256] = {}, spiByteMs = 0;
uint64_t busyUntilMs = 0;
unsigned yields = 0;
std::vector<Event> events;
void reset() {
  clockMs = 0; std::fill(pins, pins + 40, HIGH);
  busyStuck = false; stuckOnCommand = -1; yields = 0; events.clear();
  std::fill(commandBusyMs, commandBusyMs + 256, 0);
  spiByteMs = 0; busyUntilMs = 0;
  trace::commands.clear(); Serial = FakeSerial(); ESP = FakeEsp(); WiFi = FakeWiFi();
  testnvs::namespaces.clear(); testnvs::failOpen.clear(); testnvs::failWrite.clear();
  testnvs::writableOpens = testnvs::writes = 0;
  fakeResetReason = ESP_RST_POWERON;
}
}
