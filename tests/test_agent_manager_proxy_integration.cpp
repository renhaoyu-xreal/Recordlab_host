#include "recordlab_host/agents/agent_manager.h"
#include "recordlab_host/bus/message_types.h"

#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

fs::path hostRoot() {
    const auto cwd = fs::current_path();
    return cwd.filename() == "build" ? cwd.parent_path() : cwd;
}

std::string envOrDefault(const char* name, const fs::path& fallback) {
    if (const char* value = std::getenv(name)) {
        return value;
    }
    return fallback.string();
}

int freePort() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = 0;
    bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    socklen_t len = sizeof(addr);
    getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    int port = ntohs(addr.sin_port);
    close(fd);
    return port;
}

void writeCsv(const fs::path& path) {
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::ofstream out(path);
    out << "timestamp,onsensor_timestamp_us,timestamp_ns,type,data0,data1,data2,data3,data4,data5\n";
    for (int i = 0; i < 5; ++i) {
        out << (i * 0.001) << "," << (i * 1000) << "," << (now + i * 1000000LL)
            << ",1," << i << "," << i + 1 << "," << i + 2 << "," << i + 3 << ","
            << i + 4 << "," << i + 5 << "\n";
    }
}

std::optional<recordlab::host::HostMessage> waitForType(
    recordlab::host::HostMessageBus& bus,
    const std::string& target,
    const std::string& type,
    int attempts = 80) {
    for (int i = 0; i < attempts; ++i) {
        auto msg = bus.waitFor(target, 500);
        if (msg && msg->type == type) return msg;
    }
    return std::nullopt;
}

}  // namespace

int main() {
    const fs::path tmp = fs::temp_directory_path() / ("recordlab_agent_manager_proxy_" + std::to_string(::getpid()));
    fs::create_directories(tmp);
    const fs::path config = tmp / "agents_config.json";
    const fs::path csv = tmp / "imu.csv";
    writeCsv(csv);

    const int goal_port = freePort();
    const int feedback_port = freePort();
    const int imu_port = freePort();

    std::ofstream cfg(config);
    cfg << R"({
      "agents": {
        "imu_proxy": {
          "name": "imu_proxy",
          "node_class": "recordlab_nodes.nodes.imu_sim.imu_sim_node.ImuSimNode",
          "process_type": "python_node",
          "subnode_host": "127.0.0.1",
          "action_name": "imu_proxy_actions",
          "goal_port": )" << goal_port << R"(,
          "feedback_port": )" << feedback_port << R"(,
          "root_path": ")" << (tmp / "data").string() << R"(",
          "init_device_params": {
            "read_path": ")" << csv.string() << R"("
          },
          "topics": [
            {"name": "imu_data", "port": )" << imu_port << R"(, "encoding": "json"}
          ],
          "custom_params": {}
        }
      },
      "primary_agents": ["imu_proxy"]
    })";
    cfg.close();

    const fs::path root = hostRoot();
    const std::string nodes_root = envOrDefault("RECORDLAB_NODES_ROOT", root / "third_party" / "Recordlab_nodes");
    const std::string echo_python_root = envOrDefault(
        "ECHO_MESSAGE_SYSTEM_PYTHON_ROOT", root / "third_party" / "echo_message_system" / "python");

    recordlab::host::HostMessageBus bus;
    bus.registerConsumer(recordlab::host::msg::UI);
    bus.registerConsumer(recordlab::host::msg::WATCHDOG);
    recordlab::host::AgentManager manager(bus, config.string(), nodes_root, echo_python_root);
    manager.start();

    try {
        bus.publish({
            .source = recordlab::host::msg::UI,
            .target = recordlab::host::msg::AGENT_MANAGER,
            .type = recordlab::host::msg::ACTIVATE_AGENT,
            .payload = {{"agent_name", "imu_proxy"}},
        });

        auto activated = waitForType(bus, recordlab::host::msg::UI, recordlab::host::msg::AGENT_ACTIVATED);
        assert(activated.has_value());
        assert(activated->payload.value("agent_name", std::string{}) == "imu_proxy");
        assert(activated->payload.value("success", false));

        bus.publish({
            .request_id = "test_check_1",
            .source = recordlab::host::msg::UI,
            .target = recordlab::host::msg::AGENT_MANAGER,
            .type = recordlab::host::msg::CMD_REQUEST,
            .payload = {
                {"request_id", "test_check_1"},
                {"agent_name", "imu_proxy"},
                {"cmd", "check"},
                {"params", nlohmann::json::object()},
                {"priority", "normal"},
                {"silent", true},
            },
        });

        auto result = waitForType(bus, recordlab::host::msg::UI, recordlab::host::msg::CMD_RESULT);
        assert(result.has_value());
        assert(result->payload.value("agent_name", std::string{}) == "imu_proxy");
        assert(result->payload.value("cmd", std::string{}) == "check");
        assert(result->payload.value("success", false));

        bus.publish({
            .request_id = "test_init_1",
            .source = recordlab::host::msg::WATCHDOG,
            .target = recordlab::host::msg::AGENT_MANAGER,
            .type = recordlab::host::msg::INIT_DEVICE,
            .payload = {
                {"request_id", "test_init_1"},
                {"agent_name", "imu_proxy"},
            },
        });

        auto init = waitForType(bus, recordlab::host::msg::WATCHDOG, recordlab::host::msg::CMD_RESULT);
        assert(init.has_value());
        assert(init->request_id == "test_init_1");
        assert(init->payload.value("request_id", std::string{}) == "test_init_1");
        assert(init->payload.value("agent_name", std::string{}) == "imu_proxy");
        assert(init->payload.value("cmd", std::string{}) == "init_device");
        assert(init->payload.value("success", false));
    } catch (...) {
        manager.stop();
        fs::remove_all(tmp);
        throw;
    }

    manager.stop();
    fs::remove_all(tmp);
    std::cout << "agent manager proxy integration ok\n";
    return 0;
}
