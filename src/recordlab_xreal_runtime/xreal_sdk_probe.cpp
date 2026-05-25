#include "recordlab_xreal_runtime/xreal_sdk_probe.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace recordlab::xreal_runtime {

namespace {

std::string readCommandOutput(const std::string &command) {
  std::array<char, 512> buffer{};
  std::string output;
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe) throw std::runtime_error("无法启动 SDK probe");
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
    output += buffer.data();
  }
  int rc = pclose(pipe);
  if (rc != 0 && output.empty()) throw std::runtime_error("SDK probe 执行失败");
  return output;
}

std::string quoteForShell(const std::string &value) {
  std::string out = "'";
  for (char c : value) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}

}  // namespace

json probeXrealSdk(const std::string &probe_script) {
  const char *fake = std::getenv("RECORDLAB_BSP_SDK_PROBE_JSON");
  if (fake && *fake) return json::parse(fake);

  const std::string script = probe_script.empty() ? defaultXrealSdkProbeScriptPath() : probe_script;
  if (!std::filesystem::exists(script)) {
    return {{"success", false},
            {"message", "SDK probe 脚本不存在: " + script},
            {"product_ids", json::array()},
            {"device_count", 0},
            {"fsn", ""},
            {"fsn_status", "not_requested"}};
  }
  try {
    const std::string output = readCommandOutput("python3 " + quoteForShell(script) + " --json");
    const auto begin = output.find('{');
    const auto end = output.rfind('}');
    if (begin == std::string::npos || end == std::string::npos || end < begin) {
      throw std::runtime_error("SDK probe 未输出 JSON");
    }
    return json::parse(output.substr(begin, end - begin + 1));
  } catch (const std::exception &e) {
    return {{"success", false},
            {"message", e.what()},
            {"product_ids", json::array()},
            {"device_count", 0},
            {"fsn", ""},
            {"fsn_status", "failed"}};
  }
}

std::string defaultXrealSdkProbeScriptPath() {
  return std::string(RECORDLAB_MASTER_SOURCE_DIR) + "/third_party/xreal/scripts/xreal_sdk_probe.py";
}

}  // namespace recordlab::xreal_runtime
