#pragma once

#include <map>
#include <string>

namespace recordlab {

class NameResolver {
 public:
  static std::string normalizeNamespace(const std::string &ns);
  static std::string normalizeAbsolute(const std::string &name);
  static std::string resolve(const std::string &name,
                             const std::string &name_space = "/",
                             const std::map<std::string, std::string> &remap = {});
};

}  // namespace recordlab
