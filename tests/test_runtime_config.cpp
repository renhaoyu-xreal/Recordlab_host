#include "recordlab_host/common/runtime_config.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path hostRoot() {
    const auto cwd = std::filesystem::current_path();
    return cwd.filename() == "build" ? cwd.parent_path() : cwd;
}

}  // namespace

int main() {
    namespace fs = std::filesystem;

    const fs::path root = hostRoot();
    const fs::path temp_root = root / "build" / "test_runtime_config_tmp";
    const fs::path nodes_config_dir = temp_root / "nodes" / "config";
    fs::create_directories(nodes_config_dir);

    const fs::path product_config = nodes_config_dir / "recordlab_product_config.jsonc";
    {
        std::ofstream out(product_config);
        out << R"({
  // ui metadata for host
  "version": "v2.3.4",
  "update_info": "line1\nline2",
  "usb_product_catalog": [
    { "vid": "0x3318", "pid": "0x043a", "name": "Hylla" }
  ]
})";
    }

    const fs::path runtime_config = temp_root / "host_runtime.json";
    {
        std::ofstream out(runtime_config);
        out << "{\n"
               "  \"nodes_root\": \"" << (temp_root / "nodes").string() << "\",\n"
               "  \"product_config_path\": \"" << product_config.string() << "\"\n"
               "}\n";
    }

    const auto cfg = recordlab::host::RuntimeConfigLoader::load(
        (root / "build" / "recordlab_host_app").string(),
        {},
        runtime_config.string());

    assert(cfg.product_config_path == product_config.string());
    assert(cfg.app_version == "v2.3.4");
    assert(cfg.update_info == "line1\nline2");

    std::cout << "runtime config ok\n";
    return 0;
}
