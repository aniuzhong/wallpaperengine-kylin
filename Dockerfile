# Containerized build of linux-wallpaperengine on Kylin V10 SP1.
#
# Layered design (following how official distro images are organized):
#   kylin:10.1-sp1-hwe-2303   base image, imported from the official install
#                             media by scripts/extract-base.sh
#   deps                      toolchain (with CMake >= 3.22.1)
#   builder                   upstream source + GCC 9.3 C++20 compat patch + build
#   runtime                   slim runtime image
#
# Stage-by-stage verification:
#   docker build --target deps    -t lwpe-deps    .
#   docker build --target builder -t lwpe-builder .
#   docker build --target runtime -t lwpe         .

ARG BASE=ghcr.io/aniuzhong/kylin:10.1-sp1-hwe-2303

# Upstream anchor: the single source of truth for reproducible builds.
ARG LWPE_REF=b016d7d1fdcf4e5fd2f9c9fa420a8aaa07fee02d

# --------------------------------------------------------------------- deps
FROM ${BASE} AS deps

ENV DEBIAN_FRONTEND=noninteractive

# CMake in the Kylin repos is only 3.16.3 while the glslang submodule
# requires >= 3.22.1, so use the official Kitware binary instead of apt.
ARG CMAKE_VERSION=4.4.3
ARG CMAKE_URL=https://cmake.org/files/v4.4

RUN apt-get update \
 && apt-get install -y \
        build-essential pkg-config git ca-certificates wget \
        libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev \
        libglew-dev freeglut3-dev libsdl2-dev liblz4-dev \
        libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
        libxxf86vm-dev libglm-dev libglfw3-dev libmpv-dev \
        libpulse-dev libfftw3-dev libfreetype-dev libdbus-1-dev zlib1g-dev \
        libgmp-dev \
 && rm -rf /var/lib/apt/lists/*

RUN wget -q -O /tmp/cmake.tar.gz \
        "${CMAKE_URL}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" \
 && mkdir -p /opt/cmake \
 && tar -xzf /tmp/cmake.tar.gz -C /opt/cmake --strip-components=1 \
 && rm /tmp/cmake.tar.gz
ENV PATH=/opt/cmake/bin:$PATH

# ------------------------------------------------------------------ builder
FROM deps AS builder

# ARGs do not cross stage boundaries; re-declare to inherit the global value
# (pass --build-arg LWPE_REF=main to try the latest upstream: a patch that no
# longer applies fails the build instead of silently producing a broken
# artifact).
ARG LWPE_REF

# A source checkout that must never prompt for credentials nor hang on a
# stalled connection
ENV GIT_TERMINAL_PROMPT=0 \
    GIT_HTTP_LOW_SPEED_LIMIT=1024 \
    GIT_HTTP_LOW_SPEED_TIME=30

# Fetch upstream at the pinned commit (single path: local and CI builds are
# identical) and apply the Kylin patch series.
# GCC 9.3.0 only accepts -std=c++2a and lacks std::format / std::ranges /
# std::views, hence the compat patch.
RUN git init -q /src \
 && git -C /src remote add origin https://github.com/Almamu/linux-wallpaperengine.git \
 && git -C /src fetch -q --depth 1 origin "${LWPE_REF}" \
 && git -C /src checkout -q FETCH_HEAD \
 && git -C /src submodule update --init --recursive
COPY patches/0001-gcc9-cxx2a-compat.patch /tmp/
RUN cd /src && git apply --verbose /tmp/0001-gcc9-cxx2a-compat.patch

# CEF is fetched from the official CDN by CMake (CMakeModules/DownloadCEF.cmake,
# SHA1-verified); the Release flavor defaults to the minimal variant (~370MB).
RUN cmake -S /src -B /build -DCMAKE_BUILD_TYPE=Release

RUN cmake --build /build -j"$(nproc)" \
 && cmake --install /build

# Smoke check: artifact exists and every dynamic library resolves (the
# runtime base is the same image family as the build base)
RUN test -x /opt/linux-wallpaperengine/linux-wallpaperengine \
 && ! ldd /opt/linux-wallpaperengine/linux-wallpaperengine | grep -q "not found" \
 && ls -l /opt/linux-wallpaperengine/

# ------------------------------------------------------------------ runtime
# The runtime base stays on the base image: libmpv/libSDL2/libGL/ffmpeg are
# already shipped by this desktop system. GLEW and GLFW however are NOT in
# the desktop image (Kylin only distributes them with the -dev packages) and
# must be added or the binary will not start. Nothing else is required.
FROM ${BASE} AS runtime

ARG LWPE_REF

RUN apt-get update \
 && apt-get install -y --no-install-recommends libglew2.1 libglfw3 \
 && rm -rf /var/lib/apt/lists/*

COPY --from=builder /opt/linux-wallpaperengine /opt/linux-wallpaperengine

# Verify once more: with the runtime libs in place nothing may be missing
RUN ! ldd /opt/linux-wallpaperengine/linux-wallpaperengine | grep -q "not found"

WORKDIR /opt/linux-wallpaperengine

# The installed tree carries RPATH $ORIGIN;$ORIGIN/lib;$ORIGIN/lib64, so
# bundled libraries next to the binary resolve automatically
ENTRYPOINT ["/opt/linux-wallpaperengine/linux-wallpaperengine"]

# ---- OCI annotations: visible on the ghcr page and in docker inspect ----
LABEL org.opencontainers.image.title="linux-wallpaperengine (kylin)" \
      org.opencontainers.image.description="linux-wallpaperengine built for Kylin V10 SP1 (upstream commit ${LWPE_REF} + kylin patches)" \
      org.opencontainers.image.source="https://github.com/Almamu/linux-wallpaperengine" \
      org.opencontainers.image.base.name="ghcr.io/aniuzhong/kylin:10.1-sp1-hwe-2303"
