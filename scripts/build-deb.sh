#!/usr/bin/env bash
# Assemble the .deb from a complete payload tree plus the deb/ data files
# (control, copyright, maintainer scripts). No compiling happens here: the
# payload must already mirror the future system root, and the preflight
# below hard-fails unless every shipped artifact is present and its dynamic
# libraries resolve — a deb missing the engine would only disappoint on the
# user's desktop. Building a full payload locally means building the engine
# too (upstream checkout + patches/, needs cmake >= 3.22 — see Dockerfile).
#
# The payload tree gains DEBIAN/ during assembly; point --payload at a
# fresh staging tree per build if that matters.
#
# Usage:
#   scripts/build-deb.sh --payload DIR [--data DIR] [--version V] [--output FILE]
#     --payload  staging tree mirroring the future system root (required)
#     --data     where control/copyright/maintainer scripts live
#                (default: the deb/ directory next to this script)
#     --version  the @VERSION@ substitution in control
#                (default: 0.1.0-dev+<git short sha>)
#     --output   the .deb to write
#                (default: <data>/wallpaper-engine-kylin_<version>_amd64.deb)
set -euo pipefail

payload=""
data=""
version=""
output=""

while [ $# -gt 0 ]; do
    case "$1" in
        --payload) payload=$2; shift 2 ;;
        --data)    data=$2; shift 2 ;;
        --version) version=$2; shift 2 ;;
        --output)  output=$2; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 1 ;;
    esac
done

script_dir=$(cd "$(dirname "$0")" && pwd)
data=${data:-"$script_dir/../deb"}

[ -n "$payload" ] || { echo "--payload is required" >&2; exit 1; }
[ -d "$payload" ] || { echo "payload is not a directory: $payload" >&2; exit 1; }
[ -f "$data/control" ] || { echo "control not found in: $data" >&2; exit 1; }

if [ -z "$version" ]; then
    if version=$(git -C "$script_dir/.." rev-parse --short HEAD 2>/dev/null); then
        version="0.1.0-dev+$version"
    else
        version="0.1.0-dev"
    fi
fi

echo ">>> preflight: every shipped artifact present, libraries resolved"
prefix="$payload/opt/wallpaper-engine"
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

echo ">>> assembling DEBIAN/ from $data"
mkdir -p "$payload/DEBIAN" "$payload/usr/share/doc/wallpaper-engine-kylin"
cp "$data/control" "$payload/DEBIAN/control"
sed -i "s/@VERSION@/$version/" "$payload/DEBIAN/control"
for script in postinst prerm postrm preinst; do
    if [ -f "$data/$script" ]; then
        cp "$data/$script" "$payload/DEBIAN/$script"
        chmod 755 "$payload/DEBIAN/$script"
    fi
done
cp "$data/copyright" "$payload/usr/share/doc/wallpaper-engine-kylin/copyright"
echo "Installed-Size: $(du -sk --apparent-size "$payload" | cut -f1)" >> "$payload/DEBIAN/control"

package=$(sed -n 's/^Package: //p' "$payload/DEBIAN/control")
arch=$(sed -n 's/^Architecture: //p' "$payload/DEBIAN/control")
if [ -z "$output" ]; then
    output="$data/${package}_${version}_${arch}.deb"
fi
mkdir -p "$(dirname "$output")"

echo ">>> dpkg-deb --build -> $output"
dpkg-deb --build --root-owner-group "$payload" "$output"
ls -lh "$output"
