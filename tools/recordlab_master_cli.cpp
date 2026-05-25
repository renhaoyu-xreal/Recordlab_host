#include "recordlab_core/master_client.h"

#include <iostream>

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "usage: recordlab_master_cli list <nodes|topics|services|actions|types|params>\n"
                 "       recordlab_master_cli lookup <topic|service|action> <name>\n";
    return 2;
  }
  recordlab::MasterClient client;
  std::string cmd = argv[1];
  std::string kind = argv[2];
  try {
    recordlab::json resp;
    if (cmd == "list") {
      if (kind == "nodes") resp = client.call("list_nodes");
      else if (kind == "topics") resp = client.call("list_topics");
      else if (kind == "services") resp = client.call("list_services");
      else if (kind == "actions") resp = client.call("list_actions");
      else if (kind == "types") resp = client.call("list_types");
      else if (kind == "params") resp = client.call("list_params");
      else throw std::runtime_error("unknown list kind");
    } else if (cmd == "lookup" && argc >= 4) {
      std::string name = argv[3];
      if (kind == "topic") resp = client.call("lookup_topic", {{"topic", name}});
      else if (kind == "service") resp = client.call("lookup_service", {{"service", name}});
      else if (kind == "action") resp = client.call("lookup_action", {{"action", name}});
      else throw std::runtime_error("unknown lookup kind");
    } else {
      throw std::runtime_error("unknown command");
    }
    std::cout << resp.dump(2) << std::endl;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
  return 0;
}
