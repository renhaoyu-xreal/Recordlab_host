#pragma once

#include "recordlab_master/registries.h"

#include <string>

namespace recordlab::xreal_runtime {

json probeXrealSdk(const std::string &probe_script = "");
std::string defaultXrealSdkProbeScriptPath();

}  // namespace recordlab::xreal_runtime
