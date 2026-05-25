#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readFile(const std::filesystem::path &path) {
  std::ifstream in(path);
  assert(in.good());
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

int main() {
  const auto root = std::filesystem::path(RECORDLAB_MASTER_SOURCE_DIR);
  const auto main_source = readFile(root / "tools/recordlab_gui.cpp");
  const auto window_source = readFile(root / "src/recordlab_tool_nodes/recordlab_gui/recordlab_gui_window.cpp");
  const auto script_source = readFile(root / "src/recordlab_tool_nodes/recordlab_gui/script_page.cpp");
  const auto command_source = readFile(root / "src/recordlab_tool_nodes/recordlab_gui/command_page.cpp");

  assert(main_source.find("RecordlabGuiWindow") != std::string::npos);
  assert(main_source.find("class RecordlabGuiWindow") == std::string::npos);
  assert(window_source.find("脚本执行") != std::string::npos);
  assert(window_source.find("数据 + 命令") != std::string::npos);
  assert(window_source.find("addTab") != std::string::npos);
  assert(script_source.find("脚本批量执行") == std::string::npos);
  assert(script_source.find("DataMonitorWidget") == std::string::npos);
  assert(command_source.find("一键启动") == std::string::npos);
  assert(command_source.find("DataMonitorWidget") == std::string::npos);
  return 0;
}
