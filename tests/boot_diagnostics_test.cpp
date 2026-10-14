#include "boot_diagnostics.h"
#include "Preferences.h"
#include <assert.h>

int main() {
  for (auto reason : {ESP_RST_POWERON, ESP_RST_SW, ESP_RST_PANIC,
                     ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT,
                     ESP_RST_BROWNOUT, ESP_RST_DEEPSLEEP}) {
    testhw::reset(); fakeResetReason = reason;
    testhw::clockMs = 0xfffffff0;
    BootDiagnostics diagnostic; diagnostic.begin();
    assert(Serial.output.find("BOOT reset_reason=") != std::string::npos);
    assert(Serial.output.find(resetReasonName(reason)) != std::string::npos);
    const auto initial = Serial.output;
    testhw::clockMs += 29999; diagnostic.poll(false, 1);
    assert(Serial.output == initial);
    ++testhw::clockMs; diagnostic.poll(false, 2);
    assert(Serial.output.find("ALIVE uptime_ms=29984 wifi_ready=0 attempts=2") != std::string::npos);
    const auto report = Serial.output;
    diagnostic.poll(true, 2); assert(Serial.output == report);
    testhw::clockMs += 30000; diagnostic.poll(true, 2);
    assert(Serial.output.find("wifi_ready=1 attempts=2") != std::string::npos);
    assert(testnvs::namespaces.empty() && !ESP.restarts);
  }
  assert(!strcmp(resetReasonName(ESP_RST_UNKNOWN), "UNKNOWN"));
  assert(!strcmp(resetReasonName(ESP_RST_BROWNOUT), "BROWNOUT"));
  assert(!strcmp(resetReasonName(ESP_RST_PANIC), "PANIC"));
  puts("Boot/reset diagnostics, heartbeat, clock wrap and zero NVS writes passed");
}
