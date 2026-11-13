# SPDX-License-Identifier: GPL-3.0-or-later
# d2d for the Nintendo 3DS (New 3DS), built with devkitARM:
#   cmake -S platform/3ds -B build-3ds -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake -DD2D_3DS_DEPS=<deps dir>
# <deps dir> holds prefix/ (SDL3 built for the 3DS), StormLib/ and CLI11/ sources.
# The game's own sources are used as they are; what the 3DS lacks (crash
# reports, Bink video, OpenAL) is in shim/ and src/.
cmake_minimum_required(VERSION 3.28)
project(d2d3ds LANGUAGES C CXX)

set(D2D_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(D2D_3DS_DEPS "" CACHE PATH "Folder with prefix/ (SDL3), StormLib/ and CLI11/")
if(NOT EXISTS "${D2D_3DS_DEPS}/prefix/lib/libSDL3.a")
    message(FATAL_ERROR "D2D_3DS_DEPS: no prefix/lib/libSDL3.a under '${D2D_3DS_DEPS}'")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)   # newlib hides POSIX names (strcasecmp, fileno...) under strict ISO
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g")

# No cmake/no_near_far.hpp here: libctru's own headers use those names.
add_compile_options(-Wall -Wextra -Wno-missing-field-initializers -Wno-psabi -ffunction-sections -fdata-sections)
include_directories(SYSTEM "${CMAKE_CURRENT_SOURCE_DIR}/shim")

list(APPEND CMAKE_MODULE_PATH "${D2D_ROOT}/cmake")
set(D2D_CHECK_HEADERS OFF CACHE BOOL "" FORCE)
include(HeaderCheck)

set(D2D_VERSION "3ds-dev")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD WORKING_DIRECTORY ${D2D_ROOT}
        OUTPUT_VARIABLE git_hash OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT git_hash)
    set(git_hash unknown)
endif()

# StormLib from source, the game's own way (components/mpq/CMakeLists.txt).
set(D2D_BUNDLED_STORMLIB ON CACHE BOOL "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_STORMLIB "${D2D_3DS_DEPS}/StormLib" CACHE PATH "" FORCE)
set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "" FORCE)
set(STORM_SKIP_INSTALL ON CACHE BOOL "" FORCE)

add_library(CLI11 INTERFACE)
target_include_directories(CLI11 SYSTEM INTERFACE "${D2D_3DS_DEPS}/CLI11/include")
add_library(CLI11::CLI11 ALIAS CLI11)

add_subdirectory("${D2D_ROOT}/components" components)
# StormLib's Linux port on newlib: int32_t is long, and unistd.h isn't pulled in.
target_compile_definitions(storm PRIVATE _GNU_SOURCE)
target_compile_options(storm PRIVATE -fpermissive "SHELL:-include unistd.h")

add_library(SDL3_3ds STATIC IMPORTED)
set_target_properties(SDL3_3ds PROPERTIES
    IMPORTED_LOCATION "${D2D_3DS_DEPS}/prefix/lib/libSDL3.a"
    INTERFACE_INCLUDE_DIRECTORIES "${D2D_3DS_DEPS}/prefix/include")

set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(CONFIGURE OUTPUT "${generated}/d2d_icon.hpp" CONTENT
    "#pragma once\ninline constexpr unsigned char kIconBmp[] = {0};\n")
string(TIMESTAMP build_date "%Y-%m-%dT%H:%M:%SZ" UTC)
file(CONFIGURE OUTPUT "${generated}/build_info.hpp" CONTENT
    "#pragma once\nnamespace d2d::build {\ninline constexpr auto kVersion = \"${D2D_VERSION}\";\ninline constexpr auto kGitHash = \"${git_hash}\";\ninline constexpr auto kGitBranch = \"3ds\";\ninline constexpr auto kGitTag = \"null\";\ninline constexpr auto kDate = \"${build_date}\";\n}\n")

set(d2d_dir "${D2D_ROOT}/apps/d2d")
file(GLOB d2d_sources CONFIGURE_DEPENDS
    "${d2d_dir}/*.cpp" "${d2d_dir}/devctl/*.cpp" "${d2d_dir}/scene/*.cpp" "${d2d_dir}/town/*.cpp")
file(GLOB port_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(d2d ${d2d_sources} ${port_sources})
# The game's main() runs under ours (src/main_3ds.cpp), which sets up the SD card's folders first.
set_source_files_properties("${d2d_dir}/main.cpp" PROPERTIES COMPILE_DEFINITIONS "main=d2d_main")
target_compile_definitions(d2d PRIVATE D2D_NO_VIDEO D2D_NO_MUSIC)
target_include_directories(d2d PRIVATE "${generated}" "${d2d_dir}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(d2d SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/shim")
target_link_libraries(d2d PRIVATE
    game net mpq d2s rules drlg txt userdir cof compcode dc6 dcc ds1 dt1 font palette tbl devctl screenshot switches
    SDL3_3ds CLI11 citro3d ctru m)
# std::thread: a real stack and the other cores (src/threads_3ds.cpp).
target_link_options(d2d PRIVATE -Wl,--wrap=threadCreate -Wl,--wrap=__syscall_thread_create
    -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=memalign -Wl,--wrap=free)   # src/memory_3ds.cpp
target_link_options(d2d PRIVATE -Wl,--gc-sections)

ctr_generate_smdh(d2d.smdh NAME "D2Decomp" DESCRIPTION "Diablo II engine (bring your own game data)" AUTHOR "psi29a and contributors; 3DS port")
ctr_create_3dsx(d2d SMDH d2d.smdh)
