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
bool httpRejected = false;
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

bool showFrame(bool packedNative) {
  File source = SPIFFS.open(kFramePath, FILE_READ);
  if (!source || source.size() != vendor::kFrameBytes) return false;
  // Write exactly one native frame, without an extra blank controller write.
  display.epd2.setPaged();
  if (!display.epd2.ok()) { display.epd2.hibernate(); return false; }
  for (uint16_t y = 0; y < 480; ++y) {
    if (packedNative) {
      if (source.read(nativeLine, sizeof(nativeLine)) != sizeof(nativeLine)) {
        display.epd2.hibernate();
        return false;
      }
      display.epd2.writeNative(nativeLine, nullptr, 0, y, 800, 1);
    } else {
      // Compatibility with the previously documented HTTP two-plane format.
      if (!source.seek(size_t(y) * sizeof(blackLine), SeekSet) ||
          source.read(blackLine, sizeof(blackLine)) != sizeof(blackLine) ||
          !source.seek(kPlaneBytes + size_t(y) * sizeof(chromaticLine), SeekSet) ||
          source.read(chromaticLine, sizeof(chromaticLine)) != sizeof(chromaticLine)) {
        display.epd2.hibernate();
        return false;
      }
      display.epd2.writeImage(blackLine, chromaticLine, 0, y, 800, 1);
    }
  }
  source.close();
  display.epd2.refresh(false);
  display.epd2.hibernate();
  return display.epd2.ok();
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

void handleUpload() {
  HTTPUpload &part = server.upload();
  if (part.status == UPLOAD_FILE_START) {
    if (part.name != "frame" || upload.owner() == FrameUpload::Owner::Http) {
      httpRejected = true;
      if (upload.owner() == FrameUpload::Owner::Http)
        upload.fail(FrameUpload::Error::InvalidField);
    } else {
      httpRejected = !upload.begin(FrameUpload::Owner::Http, millis());
    }
  } else if (part.status == UPLOAD_FILE_WRITE) {
    if (!httpRejected)
      upload.append(FrameUpload::Owner::Http, part.buf, part.currentSize, millis());
  } else if (part.status == UPLOAD_FILE_ABORTED) {
    upload.abort(FrameUpload::Owner::Http);
    httpRejected = true;
  }
}

void configureRoutes() {
  server.on("/", HTTP_GET, [] {
    sendText(200,
      "GDP075FW1 firmware\n\n"
      "TCP :8000: 96000-byte packed native 4-colour frame, close after sending\n"
      "POST /frame/native: same packed frame (multipart field: frame)\n"
      "POST /frame: legacy two-plane black/white/red frame\nGET /status\n");
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
  for (const char *path : {"/frame", "/frame/native"}) {
    server.on(path, HTTP_POST, [] {
      if (upload.owner() != FrameUpload::Owner::Http) {
        sendText(upload.error() == FrameUpload::Error::StorageUnavailable ? 503 : 400,
                 "frame field missing, aborted or storage unavailable\n");
        return;
      }
      if (httpRejected) upload.fail(FrameUpload::Error::InvalidField);
      if (!upload.finish(FrameUpload::Owner::Http)) {
        sendText(upload.error() == FrameUpload::Error::WriteFailed ? 503 : 400,
                 "frame rejected: expected exactly 96000 bytes in field frame\n");
        return;
      }
      const bool packedNative = server.uri() == "/frame/native";
      if (showFrame(packedNative))
        sendText(200, "display refreshed\n");
      else
        sendText(500, display.epd2.ok() ? String("frame read failed\n") :
                     String(display.epd2.errorText()) + "\n");
    }, handleUpload);
  }
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
    upload.abort(FrameUpload::Owner::Http);
    servicesStarted = false;
  }
  if (pendingStatus && upload.owner() == FrameUpload::Owner::None) {
    showNetworkStatus();
    pendingStatus = false;
  }
  if (servicesStarted) {
    // Serialize uploads to the single staging file.
    if (upload.owner() != FrameUpload::Owner::Tcp) server.handleClient();
    handleTcpImage();
  }
  delay(1);
}
