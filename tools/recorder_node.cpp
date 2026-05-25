#include "recordlab_system_nodes/recorder/recorder_node.h"
#include "recordlab_core/logger.h"

#include <iostream>
#include <chrono>
#include <thread>

int main() {
  recordlab::setLogComponent("recorder_node");
  recordlab::nodes::system_nodes::recorder::RecorderNode node;
  if (!node.start()) return 1;
  std::cout << "recorder_node running\n";
  while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
