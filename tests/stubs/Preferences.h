#pragma once
#include "Arduino.h"
#include <map>
namespace testnvs {
struct Value {
  enum Type { Text, I32, U32 } type;
  std::string text;
  int32_t integer;
};
extern std::map<std::string, std::map<std::string, Value>> namespaces;
extern std::string failOpen, failWrite;
extern unsigned writableOpens, writes;
}
class Preferences {
 public:
  bool begin(const char *name, bool readOnly = false) {
    if (testnvs::failOpen == name || (readOnly && !testnvs::namespaces.count(name))) return false;
    name_ = name; readOnly_ = readOnly; started_ = true;
    if (!readOnly) {
      ++testnvs::writableOpens;
      testnvs::namespaces[name_];
    }
    return true;
  }
  void end() { started_ = false; }
  bool isKey(const char *key) const {
    return started_ && testnvs::namespaces.at(name_).count(key);
  }
  String getString(const char *key, const char *fallback) const {
    if (!isKey(key)) return String(fallback);
    const auto &value = testnvs::namespaces.at(name_).at(key);
    return String(value.type == testnvs::Value::Text ? value.text : fallback);
  }
  int32_t getInt(const char *key, int32_t fallback) const {
    if (!isKey(key)) return fallback;
    const auto &value = testnvs::namespaces.at(name_).at(key);
    return value.type == testnvs::Value::I32 ? value.integer : fallback;
  }
  size_t putString(const char *key, const char *text) {
    if (!writable(key)) return 0;
    ++testnvs::writes;
    testnvs::namespaces[name_][key] = {testnvs::Value::Text, text, 0};
    return strlen(text);
  }
  size_t putInt(const char *key, int32_t value) {
    if (!writable(key)) return 0;
    ++testnvs::writes;
    testnvs::namespaces[name_][key] = {testnvs::Value::I32, "", value};
    return 4;
  }
 private:
  bool writable(const char *key) const { return started_ && !readOnly_ && testnvs::failWrite != key; }
  bool started_ = false, readOnly_ = false;
  std::string name_;
};
