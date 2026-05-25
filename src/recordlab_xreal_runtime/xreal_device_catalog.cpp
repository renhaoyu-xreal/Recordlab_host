#include "recordlab_xreal_runtime/xreal_device_catalog.h"

#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace recordlab::xreal_runtime {

namespace {
using recordlab::json;

std::vector<std::string> readNames(const json &item) {
  std::vector<std::string> names;
  if (item.contains("names") && item["names"].is_array()) {
    for (const auto &name : item["names"]) names.push_back(name.get<std::string>());
  }
  if (item.contains("name") && item["name"].is_string()) {
    names.push_back(item["name"].get<std::string>());
  }
  return names;
}

}  // namespace

std::string formatUsbHex(int value) {
  std::ostringstream out;
  out << "0x" << std::hex << std::nouppercase << std::setw(4) << std::setfill('0')
      << (value & 0xffff);
  return out.str();
}

int parseUsbId(const std::string &value) {
  std::string text = value;
  int base = 10;
  if (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0) {
    text = text.substr(2);
    base = 16;
  }
  return std::stoi(text, nullptr, base);
}

std::vector<GlassesDeviceCatalogEntry> loadGlassesDeviceCatalog(const std::string &path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("无法打开眼镜设备 catalog: " + path);
  json root = json::parse(in);
  const json items = root.contains("devices") ? root["devices"] : root;
  if (!items.is_array()) throw std::runtime_error("眼镜设备 catalog 必须是数组或包含 devices 数组");

  std::vector<GlassesDeviceCatalogEntry> catalog;
  for (const auto &item : items) {
    GlassesDeviceCatalogEntry entry;
    entry.vid = parseUsbId(item.value("vid", "0"));
    entry.pid = parseUsbId(item.value("pid", "0"));
    entry.names = readNames(item);
    entry.display_name = item.value("display_name", "");
    entry.access_mode = item.value("access_mode", "bsp_sdk");
    if (entry.display_name.empty() && !entry.names.empty()) entry.display_name = entry.names.front();
    if (entry.vid > 0 && entry.pid > 0 && !entry.display_name.empty()) {
      catalog.push_back(std::move(entry));
    }
  }
  return catalog;
}

std::string defaultGlassesDeviceCatalogPath() {
  return std::string(RECORDLAB_MASTER_SOURCE_DIR) + "/config/glasses_device_catalog.json";
}

std::vector<DetectedGlassesUsbDevice> detectGlassesUsbDevicesFromLsusb(
    const std::vector<GlassesDeviceCatalogEntry> &catalog, const std::string &lsusb_output) {
  std::vector<DetectedGlassesUsbDevice> out;
  const std::regex id_pattern(R"(\bID\s+([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})\b)");
  std::stringstream lines(lsusb_output);
  std::string line;
  while (std::getline(lines, line)) {
    std::smatch match;
    if (!std::regex_search(line, match, id_pattern)) continue;
    const int vid = std::stoi(match[1].str(), nullptr, 16);
    const int pid = std::stoi(match[2].str(), nullptr, 16);
    for (const auto &entry : catalog) {
      if (entry.vid == vid && entry.pid == pid) {
        out.push_back({entry, line, formatUsbHex(vid), formatUsbHex(pid)});
      }
    }
  }
  return out;
}

}  // namespace recordlab::xreal_runtime
