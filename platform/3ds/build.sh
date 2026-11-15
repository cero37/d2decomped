#!/bin/sh
# Builds build-3ds/d2d.3dsx in the devkitARM container. DEPS: see CMakeLists.txt.
# The container is capped (JOBS compilers, MEM of RAM, no swap): the big C++23
# files take 1-2 GB each to compile, and six at once froze the host.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
deps=${DEPS:-$root/../d2d-3ds-deps}
jobs=${JOBS:-3}
mem=${MEM:-6g}
touch "$root/platform/3ds/src/present_3ds.cpp"   # its compile time is the build's stamp in the log
exec nice -n 15 docker run --rm --memory "$mem" --memory-swap "$mem" --cpus "$jobs" -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$root:$root" -v "$deps:$deps" -w "$root" devkitpro/devkitarm sh -c "
  export PATH=\$PATH:/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin
  # Configured every time: the build stamp (git hash, date) is written at configure, and a stale one
  # had every build since 2026-11-14 call itself 0949085 in the console's log.
  cmake -S platform/3ds -B build-3ds -G Ninja -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/3DS.cmake -DD2D_3DS_DEPS=$deps
  cmake --build build-3ds -j $jobs $*"
