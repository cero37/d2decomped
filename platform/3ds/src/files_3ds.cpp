// SPDX-License-Identifier: GPL-3.0-or-later
// rename() on the SD card. The 3DS's file system won't rename a file onto one
// that exists, and the game saves that way everywhere (write a temporary, rename
// it over the old file): a character saved once could never be saved again.
// rename is wrapped (--wrap in CMakeLists.txt): when it fails and both files
// are there, the old one goes and the rename is tried again.
#include <cstdio>
#include <sys/stat.h>

extern "C" {

int __real_rename(const char* from, const char* to);

int __wrap_rename(const char* from, const char* to) {
    const int result = __real_rename(from, to);
    if (result == 0) return 0;
    struct stat info;
    if (stat(from, &info) != 0 || stat(to, &info) != 0) return result;
    if (std::remove(to) != 0) return result;
    return __real_rename(from, to);
}

}
