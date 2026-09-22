#!/usr/bin/env bash
# Assemble the .deb with CPack's DEB generator: stage this project's install
# rules into a scratch prefix, run the payload preflight against it, then
# hand the version to cpack. The control fields live in the top
# CMakeLists.txt's packaging section; deb/ keeps only the maintainer scripts
# and the copyright file. No compiling happens here: the staged payload must
# mirror the future system root, and the preflight below hard-fails unless
# every shipped artifact is present and its dynamic libraries resolve — a
# deb missing the engine would only disappoint on the user's desktop.
# Building a full payload locally means building the engine too (upstream
# checkout + patches/, needs cmake >= 3.22 — see Dockerfile).
#
# Usage:
#   scripts/build-deb.sh --build DIR [--data DIR] [--version V] [--output FILE]
#     --build    the project's cmake build directory, already configured and
#                built (required; BUILD_ENGINE=ON for a complete payload)
#     --data     where copyright/maintainer scripts live
#                (default: the deb/ directory next to this script; also the
#                default output directory)
#     --version  the deb version (default: 0.1.0-dev+<git short sha>)
#     --output   the .deb to write
set -euo pipefail

build=""
data=""
version=""
output=""

while [ $# -gt 0 ]; do
    case "$1" in
        --build)   build=$2; shift 2 ;;
        --data)    data=$2; shift 2 ;;
        --version) version=$2; shift 2 ;;
        --output)  output=$2; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 1 ;;
    esac
done

script_dir=$(cd "$(dirname "$0")" && pwd)
data=${data:-"$script_dir/../deb"}

[ -n "$build" ] || { echo "--build is required" >&2; exit 1; }
[ -d "$build" ] || { echo "not a build directory: $build" >&2; exit 1; }
[ -f "$build/CPackConfig.cmake" ] || { echo "CPackConfig.cmake not found in: $build (configure the project first)" >&2; exit 1; }

if [ -z "$version" ]; then
    if version=$(git -C "$script_dir/.." rev-parse --short HEAD 2>/dev/null); then
        version="0.1.0-dev+$version"
    else
        version="0.1.0-dev"
    fi
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo ">>> staging install rules -> $work/payload"
cmake --install "$build" --prefix "$work/payload" >/dev/null

echo ">>> preflight: every shipped artifact present, libraries resolved"
prefix="$work/payload/opt/wallpaper-engine"
for binary in "$prefix/bin/wallpaper-engine" "$prefix/linux-wallpaperengine"; do
    if [ ! -x "$binary" ]; then
        echo "payload incomplete: missing or not executable: $binary" >&2
        echo "(the payload must include the engine itself — see Dockerfile, BUILD_ENGINE)" >&2
        exit 1
    fi
done
if [ ! -f "$prefix/lib/libpeony-alpha.so" ]; then
    echo "payload incomplete: missing $prefix/lib/libpeony-alpha.so" >&2
    exit 1
fi
for binary in "$prefix/bin/wallpaper-engine" "$prefix/linux-wallpaperengine" "$prefix/lib/libpeony-alpha.so"; do
    if ldd "$binary" 2>/dev/null | grep -q "not found"; then
        echo "payload incomplete: unresolved libraries in $binary:" >&2
        ldd "$binary" 2>/dev/null | grep "not found" >&2
        exit 1
    fi
done

echo ">>> cpack DEB $version"
cpack --config "$build/CPackConfig.cmake" -G DEB -R "$version" -B "$work/out" >/dev/null

package=$(ls "$work/out"/*.deb)
if [ -z "$output" ]; then
    output="$data/$(basename "$package")"
fi
mkdir -p "$(dirname "$output")"
mv "$package" "$output"
ls -lh "$output"
