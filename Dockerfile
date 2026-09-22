# CI build vehicle: gives the GitHub Actions runner a Kylin V10 SP1 userland
# identical to the target desktops. The deliverable is a .deb, exported from
# the `export` stage; the base image itself is provisioned by
# scripts/extract-base.sh and pushed to ghcr from a local machine.
#
# Local builds do NOT need docker — the Kylin host builds natively:
#   cmake -S . -B build && cmake --build build    (controller, shim, tests)
#   (the engine builds the same way from an upstream checkout with the
#    patch series applied; it needs cmake >= 3.22 for the glslang submodule)
#
# CI produces the deb:
#   docker buildx build --target export --output type=local,dest=out .

ARG KYLIN_BASE=ghcr.io/aniuzhong/kylin:10.1-sp1-hwe-2303
# The dev image adds the toolchain on top of the pristine rootfs; both CI
# and local docker builds consume it, so toolchain changes are one image
# push (rebuild command on the deps stage below).
ARG BASE=ghcr.io/aniuzhong/kylin-dev:10.1

# Upstream provenance: cmake fetches the engine at LWPE_REF (CMakeLists)
# and applies patches/ — a patch that no longer applies fails the build.

# ------------------------------------------------------- dev image (deps)
# Definition of the dev image (ARG BASE above). Rebuild and push it only
# when the toolchain changes:
#   docker build --target deps -t ghcr.io/aniuzhong/kylin-dev:10.1 .
#   docker push ghcr.io/aniuzhong/kylin-dev:10.1
FROM ${KYLIN_BASE} AS deps

ENV DEBIAN_FRONTEND=noninteractive

# CMake in the Kylin repos is only 3.16.3 while the glslang submodule
# requires >= 3.22.1, so use the official Kitware binary instead of apt.
# qtbase5-dev builds the peony shim (Qt headers only — the shim resolves Qt
# symbols from the host peony process) and the QtTest-based tests.
ARG CMAKE_VERSION=4.4.3
ARG CMAKE_URL=https://cmake.org/files/v4.4

RUN apt-get update \
 && apt-get install -y \
        build-essential pkg-config git ca-certificates wget \
        libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev \
        libxcb-randr0-dev \
        libglew-dev freeglut3-dev libsdl2-dev liblz4-dev \
        libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
        libxxf86vm-dev libglm-dev libglfw3-dev libmpv-dev \
        libpulse-dev libfftw3-dev libfreetype-dev libdbus-1-dev zlib1g-dev \
        libpng-dev libgmp-dev qtbase5-dev \
        libsystemd-dev \
 && rm -rf /var/lib/apt/lists/*

RUN wget -q -O /tmp/cmake.tar.gz \
        "${CMAKE_URL}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" \
 && mkdir -p /opt/cmake \
 && tar -xzf /tmp/cmake.tar.gz -C /opt/cmake --strip-components=1 \
 && rm /tmp/cmake.tar.gz
ENV PATH=/opt/cmake/bin:$PATH

# ------------------------------------------------------------------ builder
FROM deps AS builder

# Upstream provenance: cmake fetches the engine at the pinned LWPE_REF and
# applies the patches/ series — the same `cmake -DBUILD_ENGINE=ON` a local
# Kylin host runs. A patch that no longer applies fails the build instead of
# producing a broken deb.
ENV GIT_TERMINAL_PROMPT=0 \
    GIT_HTTP_LOW_SPEED_LIMIT=1024 \
    GIT_HTTP_LOW_SPEED_TIME=30

COPY CMakeLists.txt /int/CMakeLists.txt
COPY src/ /int/src/
COPY patches/ /int/patches/

# One cmake entry for everything: engine (seeded tree) + controller + shim.
# The engine's payload joins the deb through the project's own install rules
# (CMakeLists, BUILD_ENGINE branch); scripts/build-deb.sh stages them all
# and hands the tree to cpack.
RUN cmake -S /int -B /build/integration -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_ENGINE=ON \
        -DBUILD_TESTING=OFF \
 && cmake --build /build/integration -j"$(nproc)"

# ----------------------------------------------------------------------- deb
# Assemble the package: stage the install rules, preflight them, cpack. The
# recipe is scripts/build-deb.sh — the same script a Kylin host uses for a
# local build; Docker is not a special case, it only builds the payload.
FROM builder AS deb

ARG DEB_VERSION=0.1.0

# CPackConfig.cmake embeds the source dir, so the deb data must live in it
COPY deb /int/deb
COPY scripts/build-deb.sh /tmp/build-deb.sh
RUN /tmp/build-deb.sh \
        --build /build/integration \
        --data /int/deb \
        --version "${DEB_VERSION}" \
        --output /pkg.deb

# ---------------------------------------------------------------------- test
# Pure-logic unit tests (T0): no bus, no display. The systemd/shim
# integration suites self-skip without a session bus.
FROM builder AS test

COPY tests/ /int/tests/
# T0 suites run everywhere; the systemd/shim integration suites self-skip
# without a session bus. The shim hook suite additionally needs an X server
# for its xcb probe, so it is excluded here.
RUN cmake -S /int -B /build/test -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/deb/opt/wallpaper-engine \
        -DBUILD_TESTING=ON \
 && cmake --build /build/test -j"$(nproc)" \
 && cd /build/test \
 && QT_QPA_PLATFORM=offscreen ctest --output-on-failure -E "shim_hook_test" \
 && echo "all tests passed" > /TESTS_PASSED

# The workflow exports this stage, not `test`: test inherits the whole
# builder filesystem (multi-GB build tree), while only the marker file is
# ever needed — exporting `test` copied 1GB+ of small files per run.
FROM scratch AS test-result
COPY --from=test /TESTS_PASSED /TESTS_PASSED

# -------------------------------------------------------------------- export
# CI exports this stage: `--output type=local,dest=out` yields out/pkg.deb
FROM scratch AS export
COPY --from=deb /pkg.deb /pkg.deb
