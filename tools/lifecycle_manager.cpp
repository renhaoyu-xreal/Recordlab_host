#include "recordlab_system_nodes/lifecycle_manager/lifecycle_manager.h"
#include "recordlab_core/logger.h"
#include <chrono>
#include <iostream>
#include <thread>
int main() {
  recordlab::setLogComponent("lifecycle_manager");
  recordlab::nodes::LifecycleManager node;
  if (!node.start()) return 1;
  std::cout << "lifecycle_manager running\n";
  while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
