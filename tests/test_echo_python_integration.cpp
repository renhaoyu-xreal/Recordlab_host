#include "recordlab_host/common/process_handle.h"
#include "recordlab_host/communication/echo_action_client.h"
#include "recordlab_host/communication/echo_topic_subscriber.h"

#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

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
    auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    std::ofstream out(path);
    out << "timestamp,onsensor_timestamp_us,timestamp_ns,type,data0,data1,data2,data3,data4,data5\n";
    for (int i = 0; i < 20; ++i) {
        out << (i * 0.001) << "," << (i * 1000) << "," << (now + i * 1000000LL)
            << ",1," << i << "," << i + 1 << "," << i + 2 << "," << i + 3 << "," << i + 4
            << "," << i + 5 << "\n";
    }
}

int main() {
    fs::path tmp = fs::temp_directory_path() / ("recordlab_host_test_" + std::to_string(::getpid()));
    fs::create_directories(tmp);
    fs::path csv = tmp / "imu.csv";
    fs::path config = tmp / "agents_config.json";
    fs::path data_root = tmp / "data";
    writeCsv(csv);

    int goal_port = freePort();
    int feedback_port = freePort();
    int imu_port = freePort();
    int record_port = freePort();
    int delay_port = freePort();
    int motion_port = freePort();

    std::ofstream cfg(config);
    cfg << R"({
      "agents": {
        "imu_cpp": {
          "name": "imu_cpp",
          "node_class": "recordlab_nodes.nodes.imu_sim.imu_sim_node.ImuSimNode",
          "process_type": "python_node",
          "subnode_host": "127.0.0.1",
          "action_name": "imu_cpp_actions",
          "goal_port": )" << goal_port << R"(,
          "feedback_port": )" << feedback_port << R"(,
          "root_path": ")" << data_root.string() << R"(",
          "topics": [
            {"name": "imu_data", "port": )" << imu_port << R"(, "encoding": "json"},
            {"name": "record_timer", "port": )" << record_port << R"(, "encoding": "json"},
            {"name": "time_delay", "port": )" << delay_port << R"(, "encoding": "json"},
            {"name": "motion_status", "port": )" << motion_port << R"(, "encoding": "json"}
          ],
          "custom_params": {}
        }
      },
      "primary_agents": ["imu_cpp"]
    })";
    cfg.close();

    recordlab::host::ProcessHandle node;
    const std::string py_path = "/home/hyren/Recordlab_nodes:/home/hyren/echo_message_system/python";
    node.start({"python3", "-m", "recordlab_nodes.core.node_runtime", "--config", config.string(), "--agent", "imu_cpp"},
               "/home/hyren/Recordlab_nodes",
               py_path);

    try {
        recordlab::host::EchoActionClient client("127.0.0.1", goal_port, feedback_port, 3000);
        bool ready = false;
        for (int i = 0; i < 50 && !ready; ++i) {
            ready = client.waitForServer(300);
            if (!ready) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        assert(ready);

        std::vector<nlohmann::json> imu_messages;
        recordlab::host::EchoTopicSubscriber sub("127.0.0.1", imu_port, "imu_data",
            [&](const nlohmann::json& data) { imu_messages.push_back(data); });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        auto init = client.sendCommand("init_device", {{"read_path", csv.string()}}, 5000);
        assert(init.success);
        auto unknown = client.sendCommand("not_a_configured_command", nlohmann::json::object(), 5000);
        assert(!unknown.success);
        assert(unknown.result.value("success", true) == false);
        auto record = client.sendCommand("start_record", {{"dataset_name", "case"}}, 5000);
        assert(record.success);
        auto start = client.sendCommand("start_device", nlohmann::json::object(), 5000);
        assert(start.success);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline && imu_messages.empty()) {
            sub.pollOnce(200);
        }
        assert(!imu_messages.empty());

        auto stop = client.sendCommand("stop_device", nlohmann::json::object(), 5000);
        assert(stop.success);
        assert(fs::exists(data_root / "case" / "imu_data.csv"));
    } catch (...) {
        node.terminate();
        throw;
    }

    node.terminate();
    fs::remove_all(tmp);
    std::cout << "echo python integration ok\n";
    return 0;
}
