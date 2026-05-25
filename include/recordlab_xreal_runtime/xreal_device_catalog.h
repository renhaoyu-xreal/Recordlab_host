#pragma once

#include "recordlab_master/registries.h"

#include <optional>
#include <string>
#include <vector>

namespace recordlab::xreal_runtime {

struct GlassesDeviceCatalogEntry {
  int vid{0};
  int pid{0};
  std::vector<std::string> names;
  std::string display_name;
  std::string access_mode{"bsp_sdk"};
};

struct DetectedGlassesUsbDevice {
  GlassesDeviceCatalogEntry catalog;
  std::string usb_line;
  std::string vid_hex;
  std::string pid_hex;
};

std::vector<GlassesDeviceCatalogEntry> loadGlassesDeviceCatalog(const std::string &path);
std::string defaultGlassesDeviceCatalogPath();
std::vector<DetectedGlassesUsbDevice> detectGlassesUsbDevicesFromLsusb(
    const std::vector<GlassesDeviceCatalogEntry> &catalog, const std::string &lsusb_output);
std::string formatUsbHex(int value);
int parseUsbId(const std::string &value);

}  // namespace recordlab::xreal_runtime
