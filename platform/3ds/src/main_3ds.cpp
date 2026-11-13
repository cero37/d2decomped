// SPDX-License-Identifier: GPL-3.0-or-later
// 3DS entry: point the game at its folders on the SD card, then run its main().
//   sdmc:/3ds/d2d/        settings, saves, d2d.log
//   sdmc:/3ds/d2d/data/   your Diablo II MPQs
#include <3ds.h>

#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

int d2d_main(int argc, char** argv);

// The game is far bigger than libctru's defaults allow for.
extern "C" {
u32 __stacksize__ = 1024 * 1024;
}

int main(int, char**) {
    osSetSpeedupEnable(true);   // New 3DS: 804 MHz and the L2 cache
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/d2d", 0777);
    mkdir("sdmc:/3ds/d2d/data", 0777);
    setenv("D2D_USER_DIR", "sdmc:/3ds/d2d", 1);
    setenv("D2_MPQ_DIR", "sdmc:/3ds/d2d/data", 1);
    setenv("HOME", "sdmc:/3ds/d2d", 1);
    setenv("D2_PATCH_INSTALLER", "sdmc:/3ds/d2d/data/LODPatch_114d.exe", 0);
    static char arg0[] = "d2d";
    static char arg1[] = "--no-video";
    static char* args[] = { arg0, arg1, nullptr };
    return d2d_main(2, args);
}

// newlib declares these and devkitARM has no users: the game's home-folder
// lookups fall back to $HOME, set above.
#include <pwd.h>
#include <unistd.h>
extern "C" {
uid_t getuid(void) { return 0; }
struct passwd* getpwuid(uid_t) { return nullptr; }
}
