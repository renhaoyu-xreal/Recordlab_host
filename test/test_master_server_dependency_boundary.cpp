#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

int main() {
  std::ifstream in(std::string(RECORDLAB_MASTER_SOURCE_DIR) + "/src/recordlab_master/master_server.cpp");
  assert(in.good());
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string source = buffer.str();

  assert(source.find("recordlab_nodes") == std::string::npos);
  assert(source.find("device_nodes") == std::string::npos);
  assert(source.find("xreal_runtime") == std::string::npos);
  assert(source.find("start_record") == std::string::npos);
  return 0;
}
