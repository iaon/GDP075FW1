// Compile the actual application callbacks, rather than a second implementation
// of its HTTP routing. WebServer is a callback harness, not an HTTP wire parser.
#define FIRMWARE_VERSION "host-test"
#define FIRMWARE_GIT_SHA "host-test"
#include "../src/main.cpp"

#include <assert.h>
#include <algorithm>
#include <map>
#include <sstream>
#include <string>

FakeSPIFFS SPIFFS;

namespace {

void resetApp() {
  tcpReceiver.stop();
  discardHttpUpload();
  frameFile.close();
  testhw::reset();
  testfs::reset();
  testpartition::state() = testpartition::State();
  filesystem = DeferredMount(mountFilesystem);
  server = WebServer(80);
  imageServer = WiFiServer(vendor::kImagePort);
  wifi = WifiManager();
  diagnostics = BootDiagnostics();
  servicesStarted = pendingStatus = false;
  display.init(0, true, 10, false);
  configureRoutes();
}

void clearTrace() { trace::commands.clear(); testhw::events.clear(); }

unsigned commandCount(uint8_t command) {
  unsigned result = 0;
  for (const auto &entry : trace::commands)
    if (entry.command == command) ++result;
  return result;
}

const trace::Command &lastCommand(uint8_t command) {
  for (auto it = trace::commands.rbegin(); it != trace::commands.rend(); ++it)
    if (it->command == command) return *it;
  assert(false && "expected controller command missing");
  return trace::commands.front();
}

std::vector<uint8_t> nativePayload() {
  std::vector<uint8_t> bytes(96000);
  for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = uint8_t(i * 37 + i / 200);
  return bytes;
}

void fileEvents(const std::vector<uint8_t> &bytes, const char *field = "frame",
                bool finish = true, unsigned chunkDelay = 0) {
  server.emitUpload(UPLOAD_FILE_START, field);
  for (size_t offset = 0; offset < bytes.size(); offset += 997) {
    testhw::clockMs += chunkDelay;
    server.emitUpload(UPLOAD_FILE_WRITE, field, bytes.data() + offset,
                      std::min(size_t(997), bytes.size() - offset));
  }
  if (finish) server.emitUpload(UPLOAD_FILE_END, field);
}

void post(const std::string &path, const std::vector<uint8_t> &bytes,
          const char *field = "frame", unsigned chunkDelay = 0) {
  assert(server.beginRequest(HTTP_POST, path));
  fileEvents(bytes, field, true, chunkDelay);
  assert(server.response.code == 0);  // No success before completion/refresh.
  server.finishRequest();
  assert(server.sendCount == 1);
  assert(!httpFrame.active && upload.owner() == FrameUpload::Owner::None);
}

using Report = std::map<std::string, std::string>;
Report report(int code) {
  if (server.response.code != code)
    fprintf(stderr, "HTTP expected %d, got %d:\n%s", code, server.response.code,
            server.response.body.c_str());
  assert(server.response.code == code);
  Report values;
  std::istringstream input(server.response.body.value);
  for (std::string line; std::getline(input, line);) {
    const size_t equal = line.find('=');
    if (equal != std::string::npos)
      assert(values.emplace(line.substr(0, equal), line.substr(equal + 1)).second);
  }
  for (const char *key : {"result", "mode", "experimental", "window_x", "window_y",
                           "window_w", "window_h", "frame_bytes", "received_bytes",
                           "refresh_started", "upload_ms", "init_ms", "write_ms",
                           "refresh_ms", "sleep_ms", "total_ms", "error"})
    assert(values.count(key));
  assert(values.at("frame_bytes") == "96000");
  assert(values.at("result") == (code == 200 ? "ok" : "error"));
  if (code == 200) {
    assert(server.response.body.value.find("display refreshed\n") == 0);
    assert(values.at("error").empty());
    assert(values.at("refresh_started") == "true");
    assert(values.at("received_bytes") == "96000");
    assert(trace::commands.back().command == 0x07);  // Response follows hibernate.
  } else {
    assert(!values.at("error").empty());
    assert(server.response.body.value.find("display refreshed") == std::string::npos);
  }
  return values;
}

uint32_t number(const Report &values, const char *key) {
  return static_cast<uint32_t>(std::stoul(values.at(key)));
}

void verifyNativeTransfer(const std::vector<uint8_t> &bytes, bool fast, bool window) {
  assert(commandCount(0x10) == 1 && lastCommand(0x10).data == bytes);
  assert(commandCount(0x12) == 1);
  assert(commandCount(0xe0) == unsigned(fast));
  assert(commandCount(0xe6) == unsigned(fast));
  assert(commandCount(0xa5) == unsigned(fast));
  assert(commandCount(0x83) == (window ? 2u : 0u));
}

void connectForLoop() {
  wifi.begin();
  const IPAddress gateway(192, 168, 1, 1);
  WiFi.connected(IPAddress(192, 168, 1, 100), gateway);
  wifi.poll();
  WiFi.connected(IPAddress(192, 168, 1, 208), gateway);
  wifi.poll();
  assert(wifi.ready());
  wifi.takeStatusUpdate();
  servicesStarted = true;
  server.begin(); imageServer.begin();
}

void testDiscovery() {
  resetApp();
  assert(server.beginRequest(HTTP_GET, "/display/modes"));
  server.finishRequest();
  assert(server.response.code == 200);
  const auto &body = server.response.body.value;
  for (const char *expected : {"gxepd2.hasPartialUpdate=true\n",
      "gxepd2.hasFastPartialUpdate=false\n", "gxepd2.useFastFullUpdate=false\n",
      "vendor.hasPartialUpdate=false\n", "default_mode=full\n",
      "fast_partial=not advertised", "fast_window_experimental=/frame/native/fast-window"})
    assert(body.find(expected) != std::string::npos);
  assert(trace::commands.empty() && testfs::state().mounts == 0);
  assert(!server.beginRequest(HTTP_GET, "/frame/native/fast-full"));
  assert(server.beginRequest(HTTP_GET, "/status"));
  server.finishRequest();
  assert(server.response.code == 200);
  assert(server.response.body.value.find("frame_bytes=96000\n") != std::string::npos);
}

void testRoutesAndIsolation(const std::vector<uint8_t> &bytes) {
  resetApp();
  struct ModeCase { const char *path, *mode; bool fast, window; };
  const ModeCase cases[] = {
      {"/frame/native/fast-full", "fast_full", true, false},
      {"/frame/native", "full", false, false},
      {"/frame/native/partial?x=200&y=120&w=400&h=240", "partial", false, true},
      {"/frame/native/fast-window?x=200&y=120&w=400&h=240", "fast_window_experimental", true, true},
      {"/frame/native?old=argument", "full", false, false}};
  for (const auto &entry : cases) {
    clearTrace();
    post(entry.path, bytes);
    const auto values = report(200);
    assert(values.at("mode") == entry.mode);
    assert(values.at("experimental") == (std::string(entry.mode) == "full" ? "false" : "true"));
    assert(values.at("window_x") == (entry.window ? "200" : "0"));
    assert(values.at("window_y") == (entry.window ? "120" : "0"));
    assert(values.at("window_w") == (entry.window ? "400" : "800"));
    assert(values.at("window_h") == (entry.window ? "240" : "480"));
    verifyNativeTransfer(bytes, entry.fast, entry.window);
    if (entry.window)
      assert(lastCommand(0x83).data ==
             (std::vector<uint8_t>{0, 200, 2, 0x57, 0, 120, 1, 0x67, 1}));
  }
  clearTrace();
  post("/frame/native/partial?x=0&y=0&w=800&h=480", bytes);
  assert(report(200).at("mode") == "partial");
  assert(lastCommand(0x83).data ==
         (std::vector<uint8_t>{0, 0, 3, 0x1f, 0, 0, 1, 0xdf, 1}));

  clearTrace();
  std::vector<uint8_t> legacy(96000, 0xff);
  std::fill(legacy.begin(), legacy.begin() + 48000, 0x55);
  post("/frame?old=argument", legacy);
  assert(report(200).at("mode") == "full");
  verifyNativeTransfer(std::vector<uint8_t>(96000, 0x11), false, false);

  // A later TCP transfer and network status must not inherit experimental mode.
  clearTrace();
  const auto wire = imageServer.enqueue(bytes);
  for (unsigned i = 0; !wire->stopped && i < 100; ++i) handleTcpImage();
  assert(wire->stopped);
  verifyNativeTransfer(bytes, false, false);
  clearTrace();
  showNetworkStatus();
  assert(commandCount(0x12) == 1 && commandCount(0x83) == 0 && commandCount(0xa5) == 0);
}

void testInvalidRequests(const std::vector<uint8_t> &bytes) {
  const char *invalid[] = {
      "/frame/native/partial", "/frame/native/partial?x=0&y=0&w=4",
      "/frame/native/partial?x=1&y=0&w=4&h=2",
      "/frame/native/partial?x=796&y=0&w=8&h=2",
      "/frame/native/partial?x=0&y=0&w=4&h=1",
      "/frame/native/partial?x=0&y=0&w=65536&h=2",
      "/frame/native/partial?x=0&y=0&w=4px&h=2",
      "/frame/native/partial?x=0&y=0&w=4&h=2&x=0",
      "/frame/native/partial?x=0&y=0&w=4&h=2&foo=bar",
      "/frame/native/fast-full?x=0",
      "/frame/native/fast-window?x=0&y=0&w=4&h=2&mode=full"};
  for (const char *path : invalid) {
    resetApp();
    post(path, std::vector<uint8_t>(128, 0x55));
    const auto values = report(400);
    assert(values.at("refresh_started") == "false");
    assert(trace::commands.empty() && testfs::state().writeOpens == 0);
  }
  resetApp();
  assert(server.beginRequest(HTTP_POST, "/frame/native/fast-full"));
  server.finishRequest();
  const auto missingFast = report(400);
  assert(missingFast.at("mode") == "fast_full" && missingFast.at("experimental") == "true");
  assert(trace::commands.empty());
  assert(server.beginRequest(HTTP_POST, "/frame/native/partial?x=200&y=120&w=400&h=240"));
  server.finishRequest();
  const auto missingWindow = report(400);
  assert(missingWindow.at("mode") == "partial" && missingWindow.at("experimental") == "true");
  assert(missingWindow.at("window_x") == "200" && missingWindow.at("window_y") == "120" &&
         missingWindow.at("window_w") == "400" && missingWindow.at("window_h") == "240");
  assert(trace::commands.empty());

  for (size_t count : {size_t(0), size_t(95999), size_t(96001)}) {
    resetApp();
    post("/frame/native/fast-full", std::vector<uint8_t>(count, 0x55));
    const auto values = report(400);
    assert(values.at("refresh_started") == "false" && values.at("refresh_ms") == "0");
    assert(trace::commands.empty());
  }
  resetApp();
  post("/frame/native/fast-full", bytes, "picture");
  report(400);
  assert(trace::commands.empty() && testfs::state().writeOpens == 0);

  resetApp();
  assert(server.beginRequest(HTTP_POST, "/frame/native/fast-full"));
  fileEvents(bytes);
  fileEvents(bytes);  // Two file fields, even with the same correct name.
  server.finishRequest();
  report(400);
  assert(trace::commands.empty() && testfs::state().writeOpens == 1);
  assert(upload.owner() == FrameUpload::Owner::None);

  resetApp();
  assert(server.beginRequest(HTTP_POST, "/frame/native/fast-full"));
  fileEvents(bytes, "frame", false);
  server.finishRequest();
  report(400);
  assert(trace::commands.empty());

  // A failed request must not poison the next valid request or retain metrics.
  post("/frame/native", bytes);
  assert(report(200).at("mode") == "full");
  verifyNativeTransfer(bytes, false, false);
}

void testLateFields(const std::vector<uint8_t> &bytes) {
  const std::string path = "/frame/native/partial?x=200&y=120&w=400&h=240";
  for (int variation = 0; variation < 3; ++variation) {
    resetApp();
    assert(server.beginRequest(HTTP_POST, path));
    const auto query = server.arguments();
    fileEvents(bytes);
    auto finalArgs = query;
    if (variation == 0) finalArgs.insert(finalArgs.begin(), {"x", "200"});
    if (variation == 1) finalArgs.insert(finalArgs.begin(), {"unexpected", "field"});
    if (variation == 2) finalArgs[0].second = "204";
    server.setFinalArguments(finalArgs);
    server.finishRequest();
    report(400);
    assert(trace::commands.empty() && upload.owner() == FrameUpload::Owner::None);
  }
  resetApp();
  assert(server.beginRequest(HTTP_POST, "/frame/native/partial"));
  fileEvents(bytes);
  server.setFinalArguments({{"x", "200"}, {"y", "120"}, {"w", "400"}, {"h", "240"}});
  server.finishRequest();
  report(400);  // Geometry must have been validated from the query at START.
  assert(trace::commands.empty() && testfs::state().writeOpens == 0);
}

void testAbortAndOwnership(const std::vector<uint8_t> &bytes) {
  resetApp();
  assert(server.beginRequest(HTTP_POST, "/frame/native/fast-full"));
  server.emitUpload(UPLOAD_FILE_START, "frame");
  server.emitUpload(UPLOAD_FILE_WRITE, "frame", bytes.data(), 100);
  server.emitUpload(UPLOAD_FILE_ABORTED, "frame");
  // Arduino skips completion after ABORTED; cleanup must already have happened.
  assert(!httpFrame.active && upload.owner() == FrameUpload::Owner::None);
  assert(!frameFile && server.response.code == 0 && trace::commands.empty());
  post("/frame/native", bytes);
  report(200);

  resetApp();
  connectForLoop();
  server.handleClientAction = [&bytes] {
    assert(server.beginRequest(HTTP_POST, "/frame/native/fast-full"));
    server.emitUpload(UPLOAD_FILE_START, "frame");
    server.emitUpload(UPLOAD_FILE_WRITE, "frame", bytes.data(), 100);
    // Model a multipart parser failure with neither ABORTED nor completion.
  };
  loop();
  assert(!httpFrame.active && upload.owner() == FrameUpload::Owner::None && !frameFile);
  assert(trace::commands.empty() && server.response.code == 0);
  post("/frame/native", bytes);
  report(200);

  resetApp();
  connectForLoop();
  const auto tcp = imageServer.enqueue({}, true);
  handleTcpImage();
  assert(upload.owner() == FrameUpload::Owner::Tcp);
  bool httpHandled = false;
  server.handleClientAction = [&httpHandled] { httpHandled = true; };
  loop();
  assert(!httpHandled && upload.owner() == FrameUpload::Owner::Tcp);
  assert(trace::commands.empty());
  tcpReceiver.stop();
  assert(tcp->stopped);
}

void testStorageFailures(const std::vector<uint8_t> &bytes) {
  for (int fault = 0; fault < 5; ++fault) {
    resetApp();
    if (fault == 0) testfs::state().mountAvailable = false;
    if (fault == 1) testfs::state().writeOpenAvailable = false;
    if (fault == 2) testfs::state().writeLimit = 1500;
    if (fault == 3) testfs::state().readOpenAvailable = false;
    if (fault == 4) testfs::state().readLimit = 200;
    post("/frame/native/fast-full", bytes);
    const auto values = report(fault < 3 ? 503 : 500);
    assert(values.at("refresh_started") == "false" && commandCount(0x12) == 0);
    assert(values.at("refresh_ms") == "0");
    if (fault == 4) {
      assert(lastCommand(0x10).data.size() == 200);
      assert(trace::commands.back().command == 0x07);
    } else assert(trace::commands.empty());
  }
}

void testMetricsAndDisplayFailures(const std::vector<uint8_t> &bytes) {
  resetApp();
  testhw::clockMs = 0xfffffff0;  // Upload and display timings cross millis wrap.
  testhw::spiByteMs = 1;
  testhw::commandBusyMs[0xa5] = 13;
  testhw::commandBusyMs[0x04] = 7;
  testhw::commandBusyMs[0x12] = 29;
  testhw::commandBusyMs[0x02] = 11;
  const uint32_t started = millis();
  post("/frame/native/fast-full", bytes, "frame", 2);
  const auto values = report(200);
  assert(number(values, "upload_ms") == 2 * ((96000 + 996) / 997));
  assert(number(values, "init_ms") >= 120 + 13 + 7);
  assert(number(values, "write_ms") == 96000);
  assert(number(values, "refresh_ms") >= 29);
  assert(number(values, "sleep_ms") >= 11);
  assert(number(values, "total_ms") == uint32_t(server.response.sentAt - started));
  assert(number(values, "total_ms") == number(values, "upload_ms") + number(values, "init_ms") +
         number(values, "write_ms") + number(values, "refresh_ms") + number(values, "sleep_ms"));

  struct Failure { int command; const char *message; bool refresh; };
  const Failure failures[] = {
      {0xa5, "display fast setup timeout", false},
      {0x12, "display refresh timeout", true},
      {0x02, "display power-off timeout", true}};
  for (const auto &fault : failures) {
    resetApp();
    testhw::stuckOnCommand = fault.command;
    post("/frame/native/fast-full", bytes);
    const auto failed = report(500);
    assert(failed.at("error") == fault.message);
    assert(failed.at("mode") == "fast_full");
    assert(failed.at("refresh_started") == (fault.refresh ? "true" : "false"));
    assert(commandCount(0x12) == unsigned(fault.refresh));
    assert(number(failed, "total_ms") >= 50000);
    assert(trace::commands.back().command == fault.command);  // No commands during BUSY.
    testhw::busyStuck = false; testhw::stuckOnCommand = -1;
    clearTrace();
    post("/frame/native", bytes);
    const auto recovered = report(200);
    assert(recovered.at("mode") == "full" && number(recovered, "total_ms") < 1000);
    verifyNativeTransfer(bytes, false, false);
  }
}

void postExperiment(const std::string &path, const std::vector<uint8_t> &bytes,
                    const char *header = "1") {
  assert(server.beginRequest(HTTP_POST, path));
  if (header) server.headers.push_back({"X-EPD-Experimental", header});
  fileEvents(bytes);
  server.finishRequest();
  assert(!httpFrame.active && upload.owner() == FrameUpload::Owner::None);
}

void assertOnlyKnownCommands() {
  // Exact allowlist excludes PGM 90, APG 91, RMTP 92, PGM_CFG A2 and
  // unknown LUT commands. PSR must always select factory MTP, not register LUT.
  const std::vector<uint8_t> allowed = {0x4d,0x00,0xe3,0x50,0x61,0x65,0xe9,0xf0,
                                       0x30,0xe0,0xe6,0xa5,0x04,0x83,0x10,0x12,0x02,0x07};
  for (const auto &entry : trace::commands) {
    assert(std::find(allowed.begin(), allowed.end(), entry.command) != allowed.end());
    if (entry.command == 0x00) assert(entry.data == (std::vector<uint8_t>{0x2f,0x29}));
  }
}

void testPllParsing() {
  const char *rates[] = {"12.5","25","50","65","75","85","100","120"};
  for (unsigned value = 0; value < 256; ++value) {
    char hex[8]; snprintf(hex, sizeof(hex), "0x%02x", value);
    for (bool extended : {false, true}) {
      panel::PllParameters args;
      args.add("pll", hex); args.add("base", "fast-full");
      if (extended) args.add("extended", "1");
      panel::RefreshRequest request;
      const bool initial = value == 8 || value == 9 || value == 10 || value == 2 || value == 3 || value == 4;
      assert((args.finish(request) == nullptr) == (value < 16 && (initial || extended)));
      if (value < 16) assert(!strcmp(panel::nominalHz(value), rates[value & 7]));
    }
  }
}

void testPllHttp(const std::vector<uint8_t> &bytes) {
  resetApp();
  assert(server.beginRequest(HTTP_GET, "/display/experiments"));
  server.finishRequest();
  assert(server.response.code == 200 && trace::commands.empty());
  assert(server.response.body.value.find("lut_available=false") != std::string::npos);
  assert(testfs::state().mounts == 0);

  const std::string pllPath = "/frame/native/pll?base=fast-full&pll=0x09";
  for (const char *header : {static_cast<const char *>(nullptr), "0", "1, 1"}) {
    resetApp(); postExperiment(pllPath, bytes, header);
    const auto rejected = report(400);
    assert(rejected.at("pll_requested") == "unknown" && rejected.at("pll_written") == "unknown");
    assert(rejected.at("FR") == "-1" && rejected.at("nominal_hz") == "unknown");
    assert(trace::commands.empty() && testfs::state().writeOpens == 0);
  }
  for (const char *query : {"base=bad&pll=0x09", "base=fast-full", "pll=0x09",
      "base=full&pll=09", "base=full&pll=0x009", "base=full&pll=0x0g", "base=full&pll=0x19",
      "base=full&pll=0x05", "base=full&pll=0x0f", "base=full&pll=0x09&x=0",
      "base=partial&pll=0x09", "base=fast-window&pll=0x09&x=1&y=0&w=4&h=2",
      "base=full&base=full&pll=0x09", "base=full&pll=0x09&pll=0x09",
      "base=full&pll=0x09&extended=0", "base=full&pll=0x09&extended=1&extended=1",
      "base=full&pll=0x09&profile=foo", "base=partial&pll=0x09&x=0&y=0&w=4&h=2&x=0"}) {
    resetApp(); postExperiment(std::string("/frame/native/pll?") + query, bytes); report(400);
    assert(trace::commands.empty() && testfs::state().writeOpens == 0);
  }
  for (const char *base : {"full", "fast-full", "partial", "fast-window"}) {
    resetApp();
    testhw::commandBusyMs[0x12] = 17;
    std::string path = std::string("/frame/native/pll?base=") + base + "&pll=0x0A";
    const bool window = std::string(base) == "partial" || std::string(base) == "fast-window";
    if (window) path += "&h=20&x=200&w=40&y=120";
    postExperiment(path, bytes);
    const auto values = report(200);
    assert(values.at("pll_requested") == "0x0a" && values.at("pll_written") == "0x0a");
    assert(values.at("Dyna") == "1" && values.at("FR") == "2" && values.at("nominal_hz") == "50");
    assert(values.at("busy_assert_seen") == "true" && values.at("refresh_count") == "1");
    assert(values.at("firmware_version") == "host-test" && values.at("lut_load_ms") == "0");
    assert(number(values, "refresh_ms") == 17);
    assert(lastCommand(0x10).data == bytes && commandCount(0x12) == 1);
    // Two PLL writes: stock before fast factory setup, selected after PON.
    assert(commandCount(0x30) == 2 && lastCommand(0x30).data == std::vector<uint8_t>{10});
    size_t pon = 0, selected = 0, dtm = 0;
    for (size_t i = 0; i < trace::commands.size(); ++i) {
      if (trace::commands[i].command == 0x04) pon = i;
      if (trace::commands[i].command == 0x30) selected = i;
      if (trace::commands[i].command == 0x10) dtm = i;
    }
    assert(pon < selected && selected < dtm);
    assertOnlyKnownCommands();
    clearTrace();
    post("/frame/native", bytes); report(200);
    assert(commandCount(0x30) == 1 && lastCommand(0x30).data == std::vector<uint8_t>{8});
    assert(commandCount(0xe0) == 0 && commandCount(0x83) == 0);
  }
  resetApp(); testhw::commandBusyMs[0x12] = 3;
  postExperiment("/frame/native/pll?base=full&pll=0x0f&extended=1", bytes); report(200);
  assert(lastCommand(0x30).data == std::vector<uint8_t>{15});
  for (size_t size : {size_t(95999), size_t(96001)}) {
    resetApp(); postExperiment(pllPath, std::vector<uint8_t>(size, 0x55)); report(400);
    assert(trace::commands.empty());
  }
  resetApp(); assert(server.beginRequest(HTTP_POST, pllPath));
  server.headers.push_back({"X-EPD-Experimental", "1"});
  fileEvents(bytes); fileEvents(bytes); server.finishRequest(); report(400);
  assert(trace::commands.empty());
  resetApp(); assert(server.beginRequest(HTTP_POST, pllPath));
  server.headers.push_back({"X-EPD-Experimental", "1"});
  fileEvents(bytes); server.setFinalArguments({{"base","fast-full"},{"pll","0x0a"}});
  server.finishRequest(); report(400); assert(trace::commands.empty());
}

void testBlockedLutAndRecovery(const std::vector<uint8_t> &bytes) {
  for (const char *profile : {"unknown", "bw-short", "mtp-dump"}) {
    resetApp();
    postExperiment(std::string("/frame/native/lut?profile=") + profile, bytes);
    const auto values = report(501);
    assert(values.at("blocked_reason") == panel::kLutBlockedReason);
    assert(trace::commands.empty() && testfs::state().writeOpens == 0 && testfs::state().mounts == 0);
  }
  resetApp(); postExperiment("/frame/native/lut?profile=x&profile=x", bytes); report(400);
  assert(trace::commands.empty());
  resetApp(); postExperiment("/frame/native/lut?profile=x", bytes, nullptr); report(400);
  assert(trace::commands.empty());
  resetApp(); assert(server.beginRequest(HTTP_POST, "/display/recover"));
  server.finishRequest(); report(400); assert(trace::commands.empty());
  for (bool abort : {false, true}) {
    resetApp();
    assert(server.beginRequest(HTTP_POST, "/display/recover"));
    server.headers.push_back({"X-EPD-Experimental", "1"});
    server.emitUpload(UPLOAD_FILE_START, "frame");
    if (abort) server.emitUpload(UPLOAD_FILE_ABORTED, "frame");
    else { server.emitUpload(UPLOAD_FILE_END, "frame"); server.finishRequest(); report(400); }
    assert(trace::commands.empty() && !httpFrame.active && testfs::state().writeOpens == 0);
    testhw::commandBusyMs[0x12] = 7;
    assert(server.beginRequest(HTTP_POST, "/display/recover"));
    server.headers.push_back({"X-EPD-Experimental", "1"}); server.finishRequest();
    assert(server.response.code == 200 && commandCount(0x12) == 1);
  }
  resetApp(); testhw::commandBusyMs[0x12] = 7;
  assert(server.beginRequest(HTTP_POST, "/display/recover"));
  server.headers.push_back({"X-EPD-Experimental", "1"});
  server.finishRequest();
  // Recovery generates white directly, without staging a file.
  assert(server.response.code == 200);
  const auto body = server.response.body.value;
  assert(body.find("operation=recover\n") != std::string::npos);
  assert(commandCount(0x12) == 1 && commandCount(0x30) == 1);
  assert(lastCommand(0x10).data == std::vector<uint8_t>(96000, 0x55));
  assert(testfs::state().mounts == 0);
  assertOnlyKnownCommands();
  clearTrace(); post("/frame/native", bytes); report(200);
}

void testExperimentalBusy(const std::vector<uint8_t> &bytes) {
  const std::string path = "/frame/native/pll?base=fast-full&pll=0x02";
  resetApp(); postExperiment(path, bytes);
  auto values = report(500);
  assert(values.at("busy_assert_seen") == "false");
  assert(values.at("error").find("did not assert") != std::string::npos);
  assert(commandCount(0x12) == 1 && number(values, "refresh_ms") == 100);
  assert(commandCount(0x02) == 0 && commandCount(0x07) == 0);
  assert(values.at("cleanup_error") == "missing_busy_outcome_unknown");
  resetApp(); testhw::clockMs = 0xfffffff0;
  testhw::stuckOnCommand = 0x12;
  postExperiment(path, bytes); values = report(500);
  assert(number(values, "refresh_ms") == 180000);
  assert(values.at("busy_assert_seen") == "true" && values.at("error") == "display refresh timeout");
  assert(commandCount(0x02) == 0 && commandCount(0x07) == 0);
  assert(values.at("cleanup_state").find("skipped_busy") != std::string::npos);
  testhw::busyStuck = false; testhw::stuckOnCommand = -1;
  clearTrace(); testhw::commandBusyMs[0x12] = 5;
  assert(server.beginRequest(HTTP_POST, "/display/recover"));
  server.headers.push_back({"X-EPD-Experimental", "1"}); server.finishRequest();
  assert(server.response.code == 200 && commandCount(0x12) == 1);
  assert(lastCommand(0x30).data == std::vector<uint8_t>{8});
  resetApp(); testhw::commandBusyMs[0x12] = 3; testhw::stuckOnCommand = 0x02;
  postExperiment(path, bytes); values = report(500);
  assert(values.at("error") == "display power-off timeout");
  assert(values.at("cleanup_state") == "power_off_timeout" && commandCount(0x07) == 0);
}

}  // namespace

int main() {
  const auto bytes = nativePayload();
  testDiscovery();
  testRoutesAndIsolation(bytes);
  testInvalidRequests(bytes);
  testLateFields(bytes);
  testAbortAndOwnership(bytes);
  testStorageFailures(bytes);
  testMetricsAndDisplayFailures(bytes);
  testPllParsing();
  testPllHttp(bytes);
  testBlockedLutAndRecovery(bytes);
  testExperimentalBusy(bytes);
  puts("Actual HTTP routes: modes, multipart lifecycle, validation, isolation, faults and timings passed");
}
