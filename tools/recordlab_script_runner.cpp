#include "recordlab_core/script_runner.h"
#include "recordlab_core/logger.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char **argv) {
  recordlab::setLogComponent("recordlab_script_runner");
  std::string master_endpoint = "tcp://127.0.0.1:5590";
  if (argc >= 3 && std::string(argv[1]) == "--master") {
    master_endpoint = argv[2];
  }

  try {
    recordlab::ScriptRunner runner(master_endpoint);
    if (!runner.start()) return 1;
    std::cout << "recordlab_script_runner running\n";
    while (runner.running()) std::this_thread::sleep_for(std::chrono::seconds(1));
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
  return 0;
}
