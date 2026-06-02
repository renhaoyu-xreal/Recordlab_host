#include "recordlab_host/ui/main_window.h"

#include <QApplication>

#include <filesystem>

namespace {

std::string defaultAgentsConfig(char** argv) {
    namespace fs = std::filesystem;
    fs::path bin_path = fs::absolute(argv[0]).parent_path();
    fs::path host_root = bin_path.filename() == "build" ? bin_path.parent_path() : bin_path;
    return (host_root / "third_party" / "Recordlab_nodes" / "config" / "agents_config.json").string();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const std::string config_path = argc > 1 ? argv[1] : defaultAgentsConfig(argv);
    recordlab::host::ui::MainWindow window(config_path);
    window.show();
    return app.exec();
}