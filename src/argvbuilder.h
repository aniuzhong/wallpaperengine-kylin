#pragma once

#include "config.h"

#include <string>
#include <vector>

namespace argvbuilder {

// Pure mapping from config::Config to engine argv. Every flag emitted here
// corresponds to one engine CLI argument — the CLI never invents options.
std::vector<std::string> BuildArgv(const config::Config& config);

} // namespace argvbuilder
