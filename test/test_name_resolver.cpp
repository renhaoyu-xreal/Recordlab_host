#include "recordlab_core/name_resolver.h"
#include <cassert>
#include <map>

int main() {
  using recordlab::NameResolver;
  assert(NameResolver::resolve("/imu", "/bsp") == "/imu");
  assert(NameResolver::resolve("imu", "/bsp") == "/bsp/imu");
  assert(NameResolver::resolve("state", "glasses/left") == "/glasses/left/state");
  std::map<std::string, std::string> remap{{"imu", "/devices/left/imu"}};
  assert(NameResolver::resolve("imu", "/bsp", remap) == "/devices/left/imu");
  return 0;
}
