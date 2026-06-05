#include "recordlab_host/common/runtime_config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace recordlab::host {
namespace {

std::string envOrEmpty(const char* name) {
    const char* value = std::getenv(name);
    return value && *value ? std::string(value) : std::string{};
}

std::filesystem::path absPath(const std::filesystem::path& base, const std::string& value) {
    if (value.empty()) {
        return {};
    }
    std::filesystem::path p(value);
    if (p.is_absolute()) {
        return p.lexically_normal();
    }
    return (base / p).lexically_normal();
}

std::string jsonString(const nlohmann::json& doc, const char* key, const std::string& fallback = {}) {
    if (!doc.is_object()) {
        return fallback;
    }
    return doc.value(key, fallback);
}

}  // namespace

nlohmann::json RuntimeConfigLoader::loadJsonIfExists(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return nlohmann::json::object();
    }
    nlohmann::json doc;
    input >> doc;
    return doc;
}

RuntimeConfig RuntimeConfigLoader::load(const std::string& app_path,
                                        const std::string& override_agents_config,
                                        const std::string& runtime_config_path) {
    namespace fs = std::filesystem;
    fs::path bin_path = fs::absolute(app_path).parent_path();
    fs::path host_root = bin_path.filename() == "build" ? bin_path.parent_path() : bin_path;
    host_root = host_root.lexically_normal();

    const fs::path runtime_path = runtime_config_path.empty()
        ? host_root / "config" / "host_runtime.json"
        : absPath(host_root, runtime_config_path);
    const auto doc = loadJsonIfExists(runtime_path.string());

    RuntimeConfig cfg;
    cfg.host_root = host_root.string();
    cfg.agents_config_path = absPath(host_root, override_agents_config.empty()
        ? envOrEmpty("RECORDLAB_AGENTS_CONFIG").empty()
            ? jsonString(doc, "agents_config_path", "third_party/Recordlab_nodes/config/agents_config.json")
            : envOrEmpty("RECORDLAB_AGENTS_CONFIG")
        : override_agents_config).string();

    fs::path config_base = fs::path(cfg.agents_config_path).parent_path();
    fs::path default_nodes_root = config_base.filename() == "config"
        ? config_base.parent_path()
        : config_base;

    const std::string nodes_root = !envOrEmpty("RECORDLAB_NODES_ROOT").empty()
        ? envOrEmpty("RECORDLAB_NODES_ROOT")
        : jsonString(doc, "nodes_root", default_nodes_root.string());
    cfg.nodes_root = absPath(host_root, nodes_root).string();

    const std::string echo_root = !envOrEmpty("ECHO_MESSAGE_SYSTEM_PYTHON_ROOT").empty()
        ? envOrEmpty("ECHO_MESSAGE_SYSTEM_PYTHON_ROOT")
        : jsonString(doc, "echo_python_root", "third_party/echo_message_system/python");
    cfg.echo_python_root = absPath(host_root, echo_root).string();

    cfg.data_root = absPath(host_root, !envOrEmpty("RECORDLAB_DATA_ROOT").empty()
        ? envOrEmpty("RECORDLAB_DATA_ROOT")
        : jsonString(doc, "data_root", (fs::path(cfg.nodes_root) / "data").string())).string();
    cfg.logs_root = absPath(host_root, !envOrEmpty("RECORDLAB_LOG_DIR").empty()
        ? envOrEmpty("RECORDLAB_LOG_DIR")
        : jsonString(doc, "logs_root", "logs")).string();
    cfg.python_bin = !envOrEmpty("RECORDLAB_PYTHON_BIN").empty()
        ? envOrEmpty("RECORDLAB_PYTHON_BIN")
        : jsonString(doc, "python_bin", "python3.10");
    cfg.node_runtime_module = !envOrEmpty("RECORDLAB_NODE_RUNTIME_MODULE").empty()
        ? envOrEmpty("RECORDLAB_NODE_RUNTIME_MODULE")
        : jsonString(doc, "node_runtime_module", "recordlab_nodes.core.node_runtime");
    cfg.data_registry_host = !envOrEmpty("RECORDLAB_DATA_REGISTRY_HOST").empty()
        ? envOrEmpty("RECORDLAB_DATA_REGISTRY_HOST")
        : jsonString(doc, "data_registry_host", "127.0.0.1");
    const std::string registry_port_env = envOrEmpty("RECORDLAB_DATA_REGISTRY_PORT");
    cfg.data_registry_port = !registry_port_env.empty()
        ? std::stoi(registry_port_env)
        : doc.value("data_registry_port", 16600);
    return cfg;
}

}  // namespace recordlab::host
