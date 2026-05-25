#include "recordlab_system_nodes/health_monitor/health_monitor.h"
#include "recordlab_core/logger.h"
#include <chrono>
#include <iostream>
#include <thread>
int main() {
  recordlab::setLogComponent("health_monitor");
  recordlab::nodes::HealthMonitor node;
  if (!node.start()) return 1;
  std::cout << "health_monitor running\n";
  while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
