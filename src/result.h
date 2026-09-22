#pragma once

#include "error.h"

#include <tl/expected.hpp>

// The return type of every fallible operation in the service layer: the
// value or an Error (src/error.h), never both, never a thrown exception.
// tl::expected is std::expected backported to C++17, which the toolchain
// pins; swap the alias when that changes. Kept out of error.h so the error
// type itself stays dependency-free.
namespace wallpaper_engine {

template <typename T>
using Result = tl::expected<T, Error>;

} // namespace wallpaper_engine
