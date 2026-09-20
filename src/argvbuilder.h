#pragma once

#include "config.h"

#include <string>
#include <vector>

// Pure mapping from Config to engine argv. Every flag emitted here
// corresponds to one engine CLI argument — the UI never invents options.
std::vector<std::string> buildArgv(const Config& config);
