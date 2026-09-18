#!/usr/bin/env bash
# L0: provision the Kylin V10 SP1 base image from the official install media.
#
# casper/filesystem.squashfs on the install USB is the pristine rootfs of the
# official image — the cleanest Kylin root filesystem obtainable, with no
# user modifications.
#
# Naming follows the convention of official distro images (think
# debian:bookworm-slim):
#   ghcr.io/aniuzhong/kylin:10.1-sp1-hwe-2303   version-pinned
#   ghcr.io/aniuzhong/kylin:10.1                rolling alias
#
# Must run as root: the squashfs contains device nodes under /dev and Kylin's
# security.kysec xattrs; as a non-root user unsquashfs hangs on those
# entries (verified the hard way).
#
# Usage: sudo ./scripts/extract-base.sh
set -euo pipefail

[ "$(id -u)" -eq 0 ] || { echo "must run as root: sudo $0" >&2; exit 1; }

USB_DIR=${USB_DIR:-/media/hido/KYLINV10}
WORK_DIR=${WORK_DIR:-/home/hido/.cache/kylin-v10-base}
ROOTFS=$WORK_DIR/rootfs
IMAGE=${IMAGE:-ghcr.io/aniuzhong/kylin:10.1-sp1-hwe-2303}
IMAGE_ALIAS=${IMAGE_ALIAS:-ghcr.io/aniuzhong/kylin:10.1}
SQUASHFS=$USB_DIR/casper/filesystem.squashfs

[ -f "$SQUASHFS" ] || { echo "cannot find $SQUASHFS — is the USB mounted?" >&2; exit 1; }

# ------------------------------------------------------------ 1. unpack
# /usr existing is the idempotency marker; avoids re-unpacking 8 GB.
if [ ! -d "$ROOTFS/usr" ]; then
    echo ">>> unpacking $SQUASHFS -> $ROOTFS"
    mkdir -p "$WORK_DIR"
    unsquashfs -d "$ROOTFS" "$SQUASHFS"
else
    echo ">>> already unpacked, skipping"
fi

echo ">>> unpacked size: $(du -sh "$ROOTFS" | cut -f1)"

# ------------------------------------------------------------ 2. live/installer leftovers
echo ">>> removing live-system and installer leftovers"
rm -rf "$ROOTFS"/etc/casper.conf \
       "$ROOTFS"/usr/share/ubiquity \
       "$ROOTFS"/usr/lib/ubiquity

# ------------------------------------------------------------ 3. runtime state & machine identity
# A shared image must not carry machine-id or logs, or every container
# instance collides.
rm -rf "$ROOTFS"/run/* "$ROOTFS"/tmp/* "$ROOTFS"/var/log/* \
       "$ROOTFS"/var/cache/apt/archives/*.deb
: > "$ROOTFS/etc/machine-id"

# ------------------------------------------------------------ 4. prune
# Containers neither boot a system, load firmware, nor run a desktop; none
# of the below is useful for building or running. NOTE: Docker layers are
# additive — deletions must happen BEFORE import; a later `rm` layer never
# shrinks the image.
echo ">>> pruning desktop components irrelevant to containers"
# third-party desktop software preinstalled on Kylin
# (CrossOver ~1.5G, Sogou IME, Qianxin, ...)
rm -rf "$ROOTFS"/opt/cxoffice \
       "$ROOTFS"/opt/crossover-depend \
       "$ROOTFS"/opt/sogouimebs \
       "$ROOTFS"/opt/qianxin.com \
       "$ROOTFS"/opt/certaide.kylin \
       "$ROOTFS"/opt/third \
       "$ROOTFS"/opt/castplayer \
       "$ROOTFS"/opt/kylin-os-manager \
       "$ROOTFS"/opt/small-plugin
# kernel/firmware/modules: containers use the host kernel; firmware is
# loaded by the host
rm -rf "$ROOTFS"/boot \
       "$ROOTFS"/usr/lib/firmware \
       "$ROOTFS"/usr/lib/modules \
       "$ROOTFS"/usr/src \
       "$ROOTFS"/usr/lib32
# Kylin desktop content and documentation
rm -rf "$ROOTFS"/usr/share/kylin-user-guide \
       "$ROOTFS"/usr/share/kylin-software-center \
       "$ROOTFS"/usr/share/backgrounds \
       "$ROOTFS"/usr/share/doc \
       "$ROOTFS"/usr/share/man \
       "$ROOTFS"/usr/share/locale

echo ">>> pruned size: $(du -sh "$ROOTFS" | cut -f1)"

# ------------------------------------------------------------ 5. fix apt sources
# The image ships with Kylin's internal build proxy
# (*-archive-proxy.internal:8001), unreachable from the outside; swap in the
# public mirror or the build layers cannot install dependencies.
echo ">>> replacing apt sources with the public mirror"
cat > "$ROOTFS/etc/apt/sources.list" <<'EOF'
deb http://archive.kylinos.cn/kylin/KYLIN-ALL 10.1-2303-hwe-updates main universe multiverse restricted
deb http://archive.kylinos.cn/kylin/KYLIN-ALL 10.1-2303-updates main universe multiverse restricted
deb http://archive.kylinos.cn/kylin/KYLIN-ALL 10.1 main restricted universe multiverse
EOF
rm -f "$ROOTFS"/etc/apt/sources.list.d/kylin.list \
      "$ROOTFS"/etc/apt/preferences.d/kylin.pref

# ------------------------------------------------------------ 6. import
# docker import produces a single-layer base image with no upstream layers.
echo ">>> importing as $IMAGE"
tar -C "$ROOTFS" -c . | docker import - "$IMAGE"
docker tag "$IMAGE" "$IMAGE_ALIAS"

echo
echo ">>> done, verifying:"
docker run --rm "$IMAGE" cat /etc/os-release
docker run --rm "$IMAGE" ldd --version | head -1
docker image ls ghcr.io/aniuzhong/kylin
