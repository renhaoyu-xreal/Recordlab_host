#include "recordlab_core/name_resolver.h"

#include <sstream>
#include <vector>

namespace recordlab {
namespace {
std::string collapseSlashes(const std::string &input) {
  std::string out;
  bool last_slash = false;
  for (char c : input) {
    if (c == '/') {
      if (!last_slash) out.push_back(c);
      last_slash = true;
    } else {
      out.push_back(c);
      last_slash = false;
    }
  }
  while (out.size() > 1 && out.back() == '/') out.pop_back();
  return out.empty() ? "/" : out;
}
}  // namespace

std::string NameResolver::normalizeNamespace(const std::string &ns) {
  if (ns.empty()) return "/";
  std::string value = ns.front() == '/' ? ns : "/" + ns;
  return collapseSlashes(value);
}

std::string NameResolver::normalizeAbsolute(const std::string &name) {
  if (name.empty()) return "/";
  std::string value = name.front() == '/' ? name : "/" + name;
  return collapseSlashes(value);
}

std::string NameResolver::resolve(const std::string &name,
                                  const std::string &name_space,
                                  const std::map<std::string, std::string> &remap) {
  auto exact = remap.find(name);
  if (exact != remap.end()) return normalizeAbsolute(exact->second);

  std::string resolved;
  if (!name.empty() && name.front() == '/') {
    resolved = normalizeAbsolute(name);
  } else {
    std::string ns = normalizeNamespace(name_space);
    resolved = normalizeAbsolute(ns == "/" ? "/" + name : ns + "/" + name);
  }

  auto absolute = remap.find(resolved);
  if (absolute != remap.end()) return normalizeAbsolute(absolute->second);
  return resolved;
}

}  // namespace recordlab
