#pragma once

#include "Arduino.h"
#include <assert.h>
#include <functional>
#include <utility>

enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST };
enum HTTPUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END,
                        UPLOAD_FILE_ABORTED };
struct HTTPUpload {
  HTTPUploadStatus status = UPLOAD_FILE_START;
  String name, filename;
  uint8_t *buf = nullptr;
  size_t currentSize = 0, totalSize = 0;
};

// Exercises production route registration and callbacks. It intentionally does
// not reimplement Arduino's HTTP wire parser. Query arguments are visible during
// upload callbacks; tests may replace them with post+query arguments at completion.
class WebServer {
 public:
  using Handler = std::function<void()>;
  using Args = std::vector<std::pair<std::string, std::string>>;
  struct Response {
    int code = 0;
    String type, body;
    uint32_t sentAt = 0;
  };
  struct Route { std::string path; HTTPMethod method; Handler complete, upload; };

  explicit WebServer(uint16_t) {}
  void on(const char *path, HTTPMethod method, Handler complete, Handler upload = Handler()) {
    routes.push_back({path, method, complete, upload});
  }
  void begin() { started = true; }
  void stop() { started = false; }
  void handleClient() {
    if (handleClientAction) {
      const auto action = handleClientAction;
      handleClientAction = Handler();
      action();
    }
  }
  HTTPUpload &upload() { return upload_; }
  String uri() const { return uri_; }
  void collectHeaders(const char **, size_t) {}
  String header(const String &name) const {
    for (const auto &item : headers)
      if (item.first == name.value) return item.second;
    return String();
  }
  int args() const { return arguments_.size(); }
  String argName(int index) const { return arguments_.at(index).first; }
  String arg(int index) const { return arguments_.at(index).second; }
  String arg(const String &name) const {
    for (const auto &argument : arguments_)
      if (argument.first == name.value) return argument.second;
    return String();
  }
  bool hasArg(const String &name) const {
    for (const auto &argument : arguments_)
      if (argument.first == name.value) return true;
    return false;
  }
  void send(int code, const char *type, const String &body) {
    response.code = code; response.type = type; response.body = body;
    response.sentAt = millis();
    ++sendCount;
  }

  bool beginRequest(HTTPMethod method, const std::string &url) {
    response = Response(); sendCount = 0; upload_ = HTTPUpload();
    headers.clear();
    currentRoute_ = nullptr;
    const size_t query = url.find('?');
    uri_ = url.substr(0, query);
    arguments_.clear();
    if (query != std::string::npos) {
      size_t start = query + 1;
      while (start < url.size()) {
        const size_t end = url.find('&', start);
        const std::string field = url.substr(start, end == std::string::npos ? end : end - start);
        const size_t equal = field.find('=');
        arguments_.push_back({field.substr(0, equal),
                             equal == std::string::npos ? "" : field.substr(equal + 1)});
        if (end == std::string::npos) break;
        start = end + 1;
      }
    }
    for (const auto &route : routes) {
      if (route.path == uri_.value && (route.method == method || route.method == HTTP_ANY)) {
        currentRoute_ = &route;
        return true;
      }
    }
    return false;
  }
  void setFinalArguments(const Args &arguments) { arguments_ = arguments; }
  const Args &arguments() const { return arguments_; }
  void emitUpload(HTTPUploadStatus status, const char *name,
                  const uint8_t *bytes = nullptr, size_t count = 0) {
    assert(currentRoute_ && currentRoute_->upload);
    upload_.status = status; upload_.name = name;
    if (status == UPLOAD_FILE_START) upload_.totalSize = 0;
    upload_.currentSize = count;
    uploadBytes_.clear();
    if (count) uploadBytes_.assign(bytes, bytes + count);
    upload_.buf = count ? uploadBytes_.data() : nullptr;
    if (status == UPLOAD_FILE_WRITE) upload_.totalSize += count;
    currentRoute_->upload();
  }
  void finishRequest() {
    assert(currentRoute_ && currentRoute_->complete);
    currentRoute_->complete();
  }

  bool started = false;
  std::vector<Route> routes;
  Response response;
  unsigned sendCount = 0;
  Handler handleClientAction;
  Args headers;

 private:
  const Route *currentRoute_ = nullptr;
  String uri_;
  Args arguments_;
  HTTPUpload upload_;
  std::vector<uint8_t> uploadBytes_;
};
