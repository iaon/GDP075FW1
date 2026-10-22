#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <GxEPD2_4C.h>
#include <esp_partition.h>

#include "vendor_protocol.h"
#include "vendor_display.h"
#include "wifi_manager.h"
#include "frame_receiver.h"
#include "network_status.h"
#include "boot_diagnostics.h"
#include "deferred_mount.h"
#include "refresh_request.h"

namespace {

// Confirmed by the vendor's GPIO setup and SPI calls in app0.
constexpr uint8_t kEpdCs = 27;
constexpr uint8_t kEpdDc = 14;
constexpr uint8_t kEpdReset = 12;
constexpr uint8_t kEpdBusy = 13;
constexpr size_t kPlaneBytes = 800 * 480 / 8;
constexpr char kFramePath[] = "/frame.native";

// A 16-row drawing page consumes only 3200 bytes. Native uploads go straight
// to the controller a row at a time and don't need a full DRAM framebuffer.
GxEPD2_4C<VendorGDEM075F52, 16> display(
    VendorGDEM075F52(kEpdCs, kEpdDc, kEpdReset, kEpdBusy));
WebServer server(80);
WiFiServer imageServer(vendor::kImagePort);
WifiManager wifi;
BootDiagnostics diagnostics;
File frameFile;
uint8_t nativeLine[800 / 4];
uint8_t blackLine[800 / 8];
uint8_t chromaticLine[800 / 8];
bool mountFilesystem();
DeferredMount filesystem(mountFilesystem);
bool servicesStarted = false;
bool pendingStatus = false;
struct DisplayTimings {
  uint32_t initMs = 0, writeMs = 0, refreshMs = 0, sleepMs = 0;
  bool refreshStarted = false;
  const char *error = nullptr;
};
struct HttpFrameState {
  bool active = false, fileStarted = false, fileEnded = false, packedNative = true;
  panel::RefreshRequest request;
  uint32_t startedAt = 0, uploadMs = 0;
  size_t received = 0;
  int errorCode = 400;
  const char *error = nullptr;
};
HttpFrameState httpFrame;
class StagingStorage : public FrameStorage {
 public:
  bool open() override {
    if (!filesystem.ensureReady()) return false;
    frameFile = SPIFFS.open(kFramePath, FILE_WRITE);
    return bool(frameFile);
  }
  size_t write(const uint8_t *data, size_t size) override {
    return frameFile.write(data, size);
  }
  void close() override { frameFile.close(); }
};
StagingStorage storage;
FrameUpload upload(storage);
TcpFrameReceiver<WiFiClient, WiFiServer> tcpReceiver(upload);

void sendText(int code, const String &text) {
  server.send(code, "text/plain; charset=utf-8", text);
}

bool mountFilesystem() {
  Serial.printf("STORAGE mount SPIFFS uptime_ms=%lu\n", static_cast<unsigned long>(millis()));
  if (SPIFFS.begin(false)) {
    Serial.println("STORAGE SPIFFS mounted without formatting");
    return true;
  }
  // Format only a blank partition, never vendor data or a damaged filesystem.
  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  if (!partition) return false;
  uint8_t block[256];
  for (size_t offset = 0; offset < partition->size; offset += sizeof(block)) {
    if (esp_partition_read(partition, offset, block, sizeof(block)) != ESP_OK)
      return false;
    for (uint8_t value : block)
      if (value != 0xff) return false;
  }
  Serial.println("STORAGE formatting erased SPIFFS partition at 0x290000 (not application flash)");
  const bool mounted = SPIFFS.format() && SPIFFS.begin(false);
  Serial.println(mounted ? "STORAGE SPIFFS format/mount complete" : "STORAGE SPIFFS format/mount failed");
  return mounted;
}

bool showFrame(bool packedNative,
               const panel::RefreshRequest &request = panel::RefreshRequest(),
               DisplayTimings *measurements = nullptr) {
  DisplayTimings timing;
  File source = SPIFFS.open(kFramePath, FILE_READ);
  if (!source || source.size() != vendor::kFrameBytes) {
    timing.error = "frame read failed";
    if (measurements) *measurements = timing;
    return false;
  }
  // Write exactly one native frame, without an extra blank controller write.
  uint32_t started = millis();
  display.epd2.beginFrame(request);
  timing.initMs = uint32_t(millis() - started);
  started = millis();
  for (uint16_t y = 0; y < 480 && display.epd2.ok(); ++y) {
    if (packedNative) {
      if (source.read(nativeLine, sizeof(nativeLine)) != sizeof(nativeLine)) {
        timing.error = "frame read failed";
        break;
      }
      display.epd2.writeNative(nativeLine, nullptr, 0, y, 800, 1);
    } else {
      // Compatibility with the previously documented HTTP two-plane format.
      if (!source.seek(size_t(y) * sizeof(blackLine), SeekSet) ||
          source.read(blackLine, sizeof(blackLine)) != sizeof(blackLine) ||
          !source.seek(kPlaneBytes + size_t(y) * sizeof(chromaticLine), SeekSet) ||
          source.read(chromaticLine, sizeof(chromaticLine)) != sizeof(chromaticLine)) {
        timing.error = "frame read failed";
        break;
      }
      display.epd2.writeImage(blackLine, chromaticLine, 0, y, 800, 1);
    }
  }
  timing.writeMs = uint32_t(millis() - started);
  source.close();
  if (!timing.error && display.epd2.ok()) {
    timing.refreshStarted = true;
    started = millis();
    display.epd2.refresh(false);
    timing.refreshMs = uint32_t(millis() - started);
  }
  started = millis();
  display.epd2.hibernate();
  timing.sleepMs = uint32_t(millis() - started);
  if (!timing.error && !display.epd2.ok()) timing.error = display.epd2.errorText();
  if (measurements) *measurements = timing;
  return timing.error == nullptr;
}

void showNetworkStatus() {
  const auto &config = wifi.config();
  const bool connected = wifi.ready();
  Serial.printf("DISPLAY status start connected=%u uptime_ms=%lu\n",
                static_cast<unsigned>(connected), static_cast<unsigned long>(millis()));
  display.epd2.setPaged();
  for (uint16_t y = 0; y < 480 && display.epd2.ok(); ++y) {
    vendor::NetworkStatus::renderRow(config, connected, y, nativeLine);
    display.epd2.writeNative(nativeLine, nullptr, 0, y, 800, 1);
  }
  display.epd2.refresh(false);
  display.epd2.hibernate();
  Serial.printf("DISPLAY status end result=%s uptime_ms=%lu\n",
                display.epd2.errorText(), static_cast<unsigned long>(millis()));
}

const char *readHttpRefresh(panel::RefreshRequest &request, bool &packedNative) {
  if (!panel::requestForPath(server.uri().c_str(), request, packedNative))
    return "unknown frame endpoint";
  // Preserve the two original endpoints' handling of unrelated query args.
  if (request.mode == panel::RefreshMode::Full) return nullptr;
  panel::RefreshParameters parameters(request.mode);
  for (int i = 0; i < server.args(); ++i)
    parameters.add(server.argName(i).c_str(), server.arg(i).c_str());
  return parameters.finish(request);
}

void rejectHttpFrame(const char *error, int code = 400) {
  if (!httpFrame.error) { httpFrame.error = error; httpFrame.errorCode = code; }
}

void discardHttpUpload() {
  upload.abort(FrameUpload::Owner::Http);
  httpFrame = HttpFrameState();
}

void sendFrameReport(int code, const DisplayTimings &timing = DisplayTimings()) {
  const char *error = httpFrame.error ? httpFrame.error : timing.error;
  const auto &request = httpFrame.request;
  char report[768];
  snprintf(report, sizeof(report),
           "%sresult=%s\nmode=%s\nexperimental=%s\n"
           "window_x=%u\nwindow_y=%u\nwindow_w=%u\nwindow_h=%u\n"
           "frame_bytes=%lu\nreceived_bytes=%lu\nrefresh_started=%s\n"
           "upload_ms=%lu\ninit_ms=%lu\nwrite_ms=%lu\nrefresh_ms=%lu\n"
           "sleep_ms=%lu\ntotal_ms=%lu\nerror=%s\n",
           code == 200 ? "display refreshed\n" : "", code == 200 ? "ok" : "error",
           panel::modeName(request.mode),
           request.mode == panel::RefreshMode::Full ? "false" : "true",
           unsigned(request.x), unsigned(request.y), unsigned(request.w), unsigned(request.h),
           static_cast<unsigned long>(vendor::kFrameBytes),
           static_cast<unsigned long>(httpFrame.received),
           timing.refreshStarted ? "true" : "false",
           static_cast<unsigned long>(httpFrame.uploadMs),
           static_cast<unsigned long>(timing.initMs),
           static_cast<unsigned long>(timing.writeMs),
           static_cast<unsigned long>(timing.refreshMs),
           static_cast<unsigned long>(timing.sleepMs),
           static_cast<unsigned long>(httpFrame.active ? uint32_t(millis() - httpFrame.startedAt) : 0),
           error ? error : "");
  Serial.printf("DISPLAY HTTP status=%d\n%s", code, report);
  sendText(code, report);
  discardHttpUpload();
}

void handleUpload() {
  HTTPUpload &part = server.upload();
  if (part.status == UPLOAD_FILE_START) {
    if (!httpFrame.active) {
      httpFrame = HttpFrameState();
      httpFrame.active = true;
      httpFrame.startedAt = millis();
      // During upload callbacks Arduino WebServer exposes the query args;
      // multipart text fields are merged into args only before completion.
      const char *error = readHttpRefresh(httpFrame.request, httpFrame.packedNative);
      if (error) rejectHttpFrame(error);
    }
    if (httpFrame.fileStarted || !(part.name == "frame"))
      rejectHttpFrame("expected exactly one file in multipart field frame");
    httpFrame.fileStarted = true;
    if (!httpFrame.error && !upload.begin(FrameUpload::Owner::Http, millis()))
      rejectHttpFrame(upload.error() == FrameUpload::Error::StorageUnavailable ?
                      "storage unavailable" : "another upload is active", 503);
  } else if (part.status == UPLOAD_FILE_WRITE) {
    if (httpFrame.active && !httpFrame.error) {
      upload.append(FrameUpload::Owner::Http, part.buf, part.currentSize, millis());
      httpFrame.received = upload.received();
    }
  } else if (part.status == UPLOAD_FILE_END) {
    if (httpFrame.active) httpFrame.fileEnded = true;
  } else if (part.status == UPLOAD_FILE_ABORTED) {
    // The actual parser does not call the completion handler after aborting.
    discardHttpUpload();
  }
}

void handleFrameRequest() {
  if (!httpFrame.active) {
    // Even a request without a file should report the route it addressed.
    const char *error = readHttpRefresh(httpFrame.request, httpFrame.packedNative);
    if (error) rejectHttpFrame(error);
  }
  if (httpFrame.active) httpFrame.uploadMs = uint32_t(millis() - httpFrame.startedAt);
  if (!httpFrame.active || !httpFrame.fileStarted || !httpFrame.fileEnded)
    rejectHttpFrame("frame field missing or upload aborted");
  if (!httpFrame.error) {
    panel::RefreshRequest checked;
    bool packedNative = true;
    const char *error = readHttpRefresh(checked, packedNative);
    if (error) rejectHttpFrame(error);
    else if (!panel::sameRequest(checked, httpFrame.request) || packedNative != httpFrame.packedNative)
      rejectHttpFrame("refresh parameters changed during upload");
  }
  if (httpFrame.error) { sendFrameReport(httpFrame.errorCode); return; }
  if (!upload.finish(FrameUpload::Owner::Http)) {
    const bool storageError = upload.error() == FrameUpload::Error::WriteFailed ||
                              upload.error() == FrameUpload::Error::StorageUnavailable;
    rejectHttpFrame(storageError ? "frame staging failed" :
                    "frame rejected: expected exactly 96000 bytes in field frame",
                    storageError ? 503 : 400);
    sendFrameReport(httpFrame.errorCode);
    return;
  }
  httpFrame.uploadMs = uint32_t(millis() - httpFrame.startedAt);
  DisplayTimings timing;
  const bool ok = showFrame(httpFrame.packedNative, httpFrame.request, &timing);
  sendFrameReport(ok ? 200 : 500, timing);
}

void configureRoutes() {
  server.on("/", HTTP_GET, [] {
    sendText(200,
      "GDP075FW1 firmware\n\n"
      "TCP :8000: 96000-byte packed native 4-colour frame, close after sending\n"
      "POST /frame/native: same packed frame (multipart field: frame)\n"
      "POST /frame: legacy two-plane black/white/red frame\n"
      "POST /frame/native/fast-full: diagnostic fast full refresh\n"
      "POST /frame/native/partial?x=200&y=120&w=400&h=240: diagnostic window refresh\n"
      "POST /frame/native/fast-window?x=200&y=120&w=400&h=240: experimental fast waveform + window\n"
      "GET /display/modes: capabilities and test protocol\nGET /status\n");
  });
  server.on("/status", HTTP_GET, [] {
    sendText(200, "ip=" + WiFi.localIP().toString() + "\nrssi=" +
                      String(WiFi.RSSI()) + "\nhost_number=" +
                      String(wifi.config().host) + "\ntcp_port=" +
                      String(vendor::kImagePort) + "\nversion=" + FIRMWARE_VERSION +
                      "\nframe_bytes=" + String(vendor::kFrameBytes) +
                      "\nuptime_ms=" + String(millis()) +
                      "\nreset_reason=" + resetReasonName(esp_reset_reason()) +
                      "\nwifi_attempts=" + String(wifi.attempts()) + "\n");
  });
  server.on("/display/modes", HTTP_GET, [] {
    char modes[1400];
    snprintf(modes, sizeof(modes),
      "gxepd2.hasPartialUpdate=%s\ngxepd2.hasFastPartialUpdate=%s\n"
      "gxepd2.useFastFullUpdate=%s\nvendor.hasPartialUpdate=%s\n"
      "default_mode=full\nframe_bytes=96000\n"
      "full=/frame/native\nfast_full=/frame/native/fast-full\n"
      "partial=/frame/native/partial?x=200&y=120&w=400&h=240\n"
      "fast_window_experimental=/frame/native/fast-window?x=200&y=120&w=400&h=240\n"
      "window_x_alignment=4\nwindow_w_alignment=4\nwindow_min_h=2\n"
      "window_requires=x,y,w,h query parameters; no rounding or fallback\n"
      "window_upload=full 96000-byte native frame; entire controller RAM is written\n"
      "fast_partial=not advertised by GxEPD2; fast-window is an experimental combination\n"
      "vendor_partial_flag=paged API only; explicit diagnostic window path bypasses it\n"
      "validation=command sequence tested on host; speed, colour and outside-window effects need panel testing\n",
      GxEPD2_750c_GDEM075F52::hasPartialUpdate ? "true" : "false",
      GxEPD2_750c_GDEM075F52::hasFastPartialUpdate ? "true" : "false",
      GxEPD2_750c_GDEM075F52::useFastFullUpdate ? "true" : "false",
      VendorGDEM075F52::hasPartialUpdate ? "true" : "false");
    sendText(200, modes);
  });
  for (const char *path : {"/frame", "/frame/native", "/frame/native/fast-full",
                           "/frame/native/partial", "/frame/native/fast-window"})
    server.on(path, HTTP_POST, handleFrameRequest, handleUpload);
}

void handleTcpImage() {
  using Receiver = TcpFrameReceiver<WiFiClient, WiFiServer>;
  const auto result = tcpReceiver.poll(imageServer, millis());
  if (result == Receiver::Result::Rejected)
    Serial.println("TCP frame rejected");
  else if (result == Receiver::Result::Ready && !showFrame(true)) {
    Serial.println("TCP frame read or display operation failed");
    Serial.println(display.epd2.errorText());
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.printf("GDP075FW1 %s (%s)\n", FIRMWARE_VERSION, FIRMWARE_GIT_SHA);
  diagnostics.begin();
  Serial.println("STORAGE SPIFFS deferred until image upload; no filesystem writes at boot");
  display.init(115200, true, 10, false);
  configureRoutes();
  wifi.begin();
}

void loop() {
  wifi.poll();
  diagnostics.poll(wifi.ready(), wifi.attempts());
  if (wifi.takeStatusUpdate()) pendingStatus = true;
  if (wifi.ready() && !servicesStarted) {
    server.begin();
    imageServer.begin();
    servicesStarted = true;
    Serial.printf("Image receiver: TCP port %u\n", vendor::kImagePort);
  } else if (!wifi.ready() && servicesStarted) {
    server.stop();
    imageServer.end();
    tcpReceiver.stop();
    discardHttpUpload();
    servicesStarted = false;
  }
  if (pendingStatus && upload.owner() == FrameUpload::Owner::None) {
    showNetworkStatus();
    pendingStatus = false;
  }
  if (servicesStarted) {
    // Serialize uploads to the single staging file.
    if (upload.owner() != FrameUpload::Owner::Tcp) {
      server.handleClient();
      // WebServer parses a whole multipart request synchronously. A parser
      // failure may skip both completion and ABORTED; do not retain its file.
      if (httpFrame.active) discardHttpUpload();
    }
    handleTcpImage();
  }
  delay(1);
}
