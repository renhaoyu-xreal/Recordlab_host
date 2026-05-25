#pragma once

#include "recordlab_core/name_resolver.h"

#include <chrono>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace recordlab {

using json = nlohmann::json;

inline int64_t nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

struct RegistryEvent {
  std::string type;
  json data;
};

class RegistryStore {
 public:
  explicit RegistryStore(int64_t lease_timeout_ms = 3000)
      : lease_timeout_ms_(lease_timeout_ms) {}

  std::vector<RegistryEvent> registerNode(const json &data) {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string ns = data.value("namespace", "/");
    std::map<std::string, std::string> remap;
    if (data.contains("remap") && data["remap"].is_object()) {
      for (auto it = data["remap"].begin(); it != data["remap"].end(); ++it) {
        remap[it.key()] = it.value().get<std::string>();
      }
    }
    const std::string name = NameResolver::resolve(data.value("node", data.value("name", "")), ns, remap);
    json node = data;
    node["node"] = name;
    node["state"] = "alive";
    node["last_heartbeat_ms"] = nowMs();
    nodes_[name] = node;
    return {{"node_added", node}};
  }

  std::vector<RegistryEvent> heartbeat(const json &data) {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string name = NameResolver::normalizeAbsolute(data.value("node", ""));
    auto it = nodes_.find(name);
    if (it == nodes_.end()) return {};
    const std::string old = it->second.value("state", "alive");
    it->second["state"] = "alive";
    it->second["last_heartbeat_ms"] = nowMs();
    if (old != "alive") return {{"node_alive", it->second}};
    return {};
  }

  std::vector<RegistryEvent> unregisterNode(const json &data) {
    std::lock_guard<std::mutex> lock(mu_);
    const std::string name = NameResolver::normalizeAbsolute(data.value("node", ""));
    std::vector<RegistryEvent> events;
    auto node = nodes_.find(name);
    if (node != nodes_.end()) {
      events.push_back({"node_removed", node->second});
      nodes_.erase(node);
    }
    eraseOwned(topic_publishers_, "publisher_removed", name, events);
    eraseOwned(services_, "service_removed", name, events);
    eraseOwned(actions_, "action_removed", name, events);
    return events;
  }

  std::vector<RegistryEvent> checkLeases() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<RegistryEvent> events;
    const auto now = nowMs();
    for (auto &kv : nodes_) {
      auto &node = kv.second;
      if (node.value("state", "alive") == "alive" &&
          now - node.value("last_heartbeat_ms", now) > lease_timeout_ms_) {
        node["state"] = "stale";
        events.push_back({"node_stale", node});
      }
    }
    return events;
  }

  json listNodes() const { return values(nodes_); }

  std::vector<RegistryEvent> registerPublisher(const json &data) {
    std::lock_guard<std::mutex> lock(mu_);
    json pub = data;
    pub["topic"] = NameResolver::resolve(data.value("topic", ""), data.value("namespace", "/"));
    pub["node"] = NameResolver::normalizeAbsolute(data.value("node", ""));
    topic_publishers_[pub["topic"].get<std::string>() + "|" + pub["node"].get<std::string>()] = pub;
    return {{"publisher_added", pub}};
  }
  std::vector<RegistryEvent> unregisterPublisher(const json &data) {
    return eraseByName(topic_publishers_, "topic", data.value("topic", ""), "publisher_removed");
  }
  json lookupTopic(const std::string &topic) const {
    std::lock_guard<std::mutex> lock(mu_);
    json out = json::array();
    const std::string resolved = NameResolver::normalizeAbsolute(topic);
    for (const auto &kv : topic_publishers_) {
      if (kv.second.value("topic", "") == resolved) out.push_back(kv.second);
    }
    return out;
  }
  json listTopics() const { return values(topic_publishers_); }

  std::vector<RegistryEvent> registerService(const json &data) {
    std::lock_guard<std::mutex> lock(mu_);
    json svc = data;
    svc["service"] = NameResolver::resolve(data.value("service", ""), data.value("namespace", "/"));
    svc["node"] = NameResolver::normalizeAbsolute(data.value("node", ""));
    services_[svc["service"].get<std::string>()] = svc;
    return {{"service_added", svc}};
  }
  std::vector<RegistryEvent> unregisterService(const json &data) {
    return eraseByName(services_, "service", data.value("service", ""), "service_removed");
  }
  json lookupService(const std::string &service) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = services_.find(NameResolver::normalizeAbsolute(service));
    return it == services_.end() ? json() : it->second;
  }
  json listServices() const { return values(services_); }

  std::vector<RegistryEvent> registerAction(const json &data) {
    std::lock_guard<std::mutex> lock(mu_);
    json action = data;
    action["action"] = NameResolver::resolve(data.value("action", ""), data.value("namespace", "/"));
    action["node"] = NameResolver::normalizeAbsolute(data.value("node", ""));
    const std::string base = action["action"];
    if (!action.contains("endpoints")) {
      action["endpoints"] = {
          {"send_goal", base + "/send_goal"},
          {"cancel", base + "/cancel"},
          {"feedback", base + "/feedback"},
          {"result", base + "/result"}};
    }
    actions_[base] = action;
    return {{"action_added", action}};
  }
  std::vector<RegistryEvent> unregisterAction(const json &data) {
    return eraseByName(actions_, "action", data.value("action", ""), "action_removed");
  }
  json lookupAction(const std::string &action) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = actions_.find(NameResolver::normalizeAbsolute(action));
    return it == actions_.end() ? json() : it->second;
  }
  json listActions() const { return values(actions_); }

  std::vector<RegistryEvent> registerType(const json &data) {
    std::lock_guard<std::mutex> lock(mu_);
    json type = data;
    const std::string type_name = type.value("type_name", "");
    if (type_name.empty()) throw std::invalid_argument("type_name is required");
    const std::string encoding = type.value("encoding", "json");
    if (encoding != "json" && encoding != "raw" &&
        encoding != "shm_ring_buffer" && encoding != "protobuf_reserved") {
      throw std::invalid_argument("unsupported type encoding: " + encoding);
    }
    type["type_name"] = type_name;
    type["version"] = type.value("version", "1");
    type["encoding"] = encoding;
    type["schema_hash"] = type.value("schema_hash", "");
    type["schema_uri"] = type.value("schema_uri", "");
    type["description"] = type.value("description", "");
    types_[type_name] = type;
    return {{"type_added", type}};
  }
  json lookupType(const std::string &type_name) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = types_.find(type_name);
    return it == types_.end() ? json() : it->second;
  }
  json listTypes() const { return values(types_); }

  std::vector<RegistryEvent> setParam(const json &data) {
    std::lock_guard<std::mutex> lock(mu_);
    params_[data.value("name", "")] = data.value("value", json());
    return {{"param_changed", data}};
  }
  json getParam(const std::string &name) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = params_.find(name);
    return it == params_.end() ? json() : it->second;
  }
  json listParams() const {
    std::lock_guard<std::mutex> lock(mu_);
    return params_;
  }
  std::vector<RegistryEvent> deleteParam(const json &data) {
    std::lock_guard<std::mutex> lock(mu_);
    params_.erase(data.value("name", ""));
    return {{"param_changed", data}};
  }

 private:
  static json values(const std::map<std::string, json> &m) {
    json out = json::array();
    for (const auto &kv : m) out.push_back(kv.second);
    return out;
  }

  std::vector<RegistryEvent> eraseByName(std::map<std::string, json> &m,
                                         const std::string &field,
                                         const std::string &name,
                                         const std::string &event) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<RegistryEvent> events;
    const std::string resolved = NameResolver::normalizeAbsolute(name);
    for (auto it = m.begin(); it != m.end();) {
      if (it->second.value(field, "") == resolved) {
        events.push_back({event, it->second});
        it = m.erase(it);
      } else {
        ++it;
      }
    }
    return events;
  }

  static void eraseOwned(std::map<std::string, json> &m, const std::string &event,
                         const std::string &node, std::vector<RegistryEvent> &events) {
    for (auto it = m.begin(); it != m.end();) {
      if (it->second.value("node", "") == node) {
        events.push_back({event, it->second});
        it = m.erase(it);
      } else {
        ++it;
      }
    }
  }

  int64_t lease_timeout_ms_;
  mutable std::mutex mu_;
  std::map<std::string, json> nodes_;
  std::map<std::string, json> topic_publishers_;
  std::map<std::string, json> services_;
  std::map<std::string, json> actions_;
  std::map<std::string, json> types_;
  std::map<std::string, json> params_;
};

}  // namespace recordlab
