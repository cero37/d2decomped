#!/bin/sh
# Fetches and builds what the 3DS build needs beside devkitARM's own portlibs, into DEPS
# (default ../d2d-3ds-deps, where build.sh looks): SDL3 built for the 3DS into DEPS/prefix,
# and the sources of StormLib and CLI11 (the build compiles them itself). Each is pinned and
# patched (platform/3ds/patches). Run it inside devkitpro/devkitarm, as CI does, or on a host
# with DEVKITPRO set:
#
#   docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD/..:$PWD/.." -w "$PWD" \
#       devkitpro/devkitarm sh platform/3ds/deps.sh
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
deps=${DEPS:-$root/../d2d-3ds-deps}
patches=$root/platform/3ds/patches
jobs=${JOBS:-2}
export PATH=$PATH:/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin
mkdir -p "$deps"
deps=$(cd "$deps" && pwd)

# fetch DIR URL REF PATCH: a clean checkout of REF with PATCH applied (again if it's there already).
fetch() {
    if [ ! -d "$deps/$1/.git" ]; then git clone -q "$2" "$deps/$1"; fi
    git -C "$deps/$1" fetch -q --tags origin
    git -C "$deps/$1" checkout -q --force "$3"
    git -C "$deps/$1" clean -qfdx
    git -C "$deps/$1" apply --ignore-whitespace "$patches/$4"
}

# SDL3 (release-3.4.x line; the n3ds software keyboard hands over its text).
fetch SDL https://github.com/libsdl-org/SDL eac186a84c693d246f6d1eeed51c73dfd3fa7f30 sdl-n3ds-swkbd-text.patch
cmake -S "$deps/SDL" -B "$deps/build-sdl" -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/3DS.cmake \
    -DCMAKE_BUILD_TYPE=Release -DSDL_STATIC=ON -DSDL_SHARED=OFF -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF \
    -DCMAKE_INSTALL_PREFIX="$deps/prefix" >/dev/null
cmake --build "$deps/build-sdl" -j "$jobs" >/dev/null
cmake --install "$deps/build-sdl" >/dev/null

# StormLib 9.30 (a read-only archive's file table sized by its block table: memory).
fetch StormLib https://github.com/ladislav-zezula/StormLib v9.30 stormlib-readonly-file-table.patch
# CLI11 2.6.2 (an internal compiler error in GCC 16).
fetch CLI11 https://github.com/CLIUtils/CLI11 v2.6.2 cli11-gcc16-ice.patch
echo "3ds deps ready in $deps"
