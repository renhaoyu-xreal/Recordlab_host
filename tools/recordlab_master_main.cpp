#include "recordlab_master/master_server.h"
#include "recordlab_core/logger.h"

#include <atomic>
#include <csignal>
#include <chrono>
#include <iostream>
#include <thread>

static std::atomic<bool> g_running{true};
static void onSignal(int) { g_running = false; }

int main(int argc, char **argv) {
  recordlab::setLogComponent("recordlab_master");
  int rpc_port = argc > 1 ? std::stoi(argv[1]) : 5590;
  int graph_port = argc > 2 ? std::stoi(argv[2]) : 5591;
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  recordlab::MasterServer server(rpc_port, graph_port);
  server.start();
  std::cout << "recordlab_master rpc=" << rpc_port << " graph=" << graph_port << std::endl;
  while (g_running) std::this_thread::sleep_for(std::chrono::milliseconds(200));
  server.stop();
  return 0;
}
