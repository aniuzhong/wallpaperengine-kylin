#pragma once

#include "config.h"
#include "engine.h"

#include <string>
#include <vector>

// The projection from config::Config to the engine invocation. Domain
// mapping only: which config field feeds which engine option. The grammar
// itself — choices, mutual exclusion, binding order, argv flattening —
// lives in engine::, so this layer stays ordering-free and infallible.
namespace argvbuilder {

// Pure: config fields -> engine::Invocation. Properties of wallpapers that
// are not being launched are dropped here (a shared property name like
// schemecolor must not leak from one wallpaper into another).
engine::Invocation Project(const config::Config& config);

// Project + engine::Emit with the engine binary prepended as argv[0] — the
// unit file's ExecStart shape. Unvalidated: callers that persist the argv
// (unit_file::Install) run engine::Validate first and refuse to write a
// unit the engine would refuse to run.
std::vector<std::string> BuildArgv(const config::Config& config);

} // namespace argvbuilder
