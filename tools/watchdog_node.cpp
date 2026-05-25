#include "recordlab_system_nodes/watchdog/watchdog_node.h"
#include "recordlab_core/logger.h"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
  recordlab::setLogComponent("watchdog_node");
  recordlab::nodes::WatchdogNode node;
  if (!node.start()) return 1;
  std::cout << "watchdog_node running\n";
  while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
