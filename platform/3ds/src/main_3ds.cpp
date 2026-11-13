// SPDX-License-Identifier: GPL-3.0-or-later
// 3DS entry: point the game at its folders on the SD card, then run its main().
//   sdmc:/3ds/d2d/        settings, saves, d2d.log
//   sdmc:/3ds/d2d/data/   your Diablo II MPQs
#include <3ds.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <sys/stat.h>

int d2d_main(int argc, char** argv);

// The game is far bigger than libctru's defaults allow for.
extern "C" {
u32 __stacksize__ = 1024 * 1024;
}

int main(int, char**) {
    osSetSpeedupEnable(true);   // New 3DS: 804 MHz and the L2 cache
    APT_SetAppCpuTimeLimit(80);  // core 1 is the system's; this much of it is ours (threads_3ds.cpp)
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/d2d", 0777);
    mkdir("sdmc:/3ds/d2d/data", 0777);
    setenv("D2D_USER_DIR", "sdmc:/3ds/d2d", 1);
    setenv("D2_MPQ_DIR", "sdmc:/3ds/d2d/data", 1);
    setenv("HOME", "sdmc:/3ds/d2d", 1);
    setenv("D2D_HANDHELD", "1", 1);   // the 400x240 world view and the second screen's HUD
    setenv("D2_PATCH_INSTALLER", "sdmc:/3ds/d2d/data/LODPatch_114d.exe", 0);
    // The game's arguments: --no-video, then any in sdmc:/3ds/d2d/args.txt (one a line; for testing,
    // e.g. --start-screen ingame).
    static std::vector<std::string> words{ "d2d", "--no-video" };
    if (std::ifstream file("sdmc:/3ds/d2d/args.txt"); file)
        for (std::string line; std::getline(file, line);) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty()) words.push_back(line);
        }
    static std::vector<char*> args;
    for (auto& text : words) args.push_back(text.data());
    args.push_back(nullptr);
    return d2d_main(int(words.size()), args.data());
}

// newlib declares these and devkitARM has no users: the game's home-folder
// lookups fall back to $HOME, set above.
#include <pwd.h>
#include <unistd.h>
extern "C" {
uid_t getuid(void) { return 0; }
struct passwd* getpwuid(uid_t) { return nullptr; }
}
