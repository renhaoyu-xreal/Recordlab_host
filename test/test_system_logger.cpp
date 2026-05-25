#include "recordlab_core/logger.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>

int main() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / "recordlab_system_logger_contract";
  fs::remove_all(root);
  setenv("RECORDLAB_LOG_ROOT", root.string().c_str(), 1);

  recordlab::setLogComponent("logger_test");
  RL_LOG_INFO("system info");
  RL_LOG_WARN("system warn");
  RL_LOG_ERROR("system error");

  bool saw_log_file = false;
  for (const auto &dir : fs::directory_iterator(root)) {
    if (!dir.is_directory()) continue;
    const auto path = dir.path() / "system" / "logger_test.log";
    if (fs::exists(path) && fs::file_size(path) > 0) saw_log_file = true;
  }
  assert(saw_log_file);

  unsetenv("RECORDLAB_LOG_ROOT");
  fs::remove_all(root);
  return 0;
}
