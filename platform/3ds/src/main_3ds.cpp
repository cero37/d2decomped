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
void logs_3ds_start();
void logs_3ds_exit();

// The game is far bigger than libctru's defaults allow for.
extern "C" {
u32 __stacksize__ = 1024 * 1024;
// libctru would set 32 MiB aside as linear (physically contiguous) memory.
// Only sound buffers and the GPU need that; the game's heap needs the rest.
u32 __ctru_linear_heap_size = 8 * 1024 * 1024;

// The heaps, in place of libctru's: all the process may commit, less the
// linear heap above, goes to the ordinary heap, but no more than its
// address range holds (96 MiB). In the New 3DS's 124 MiB mode libctru's own
// sum asks for more than that and the console stops before main(); what
// doesn't fit becomes linear memory instead.
extern char* fake_heap_start;
extern char* fake_heap_end;
extern u32 __ctru_heap;
extern u32 __ctru_heap_size;
extern u32 __ctru_linear_heap;
void mappableInit(u32 addrMin, u32 addrMax);

void __system_allocateHeaps(void) {
    Handle limit = 0;
    if (R_FAILED(svcGetResourceLimit(&limit, CUR_PROCESS_HANDLE))) svcBreak(USERBREAK_PANIC);
    s64 most = 0, now = 0;
    ResourceLimitType type = RESLIMIT_COMMIT;
    svcGetResourceLimitLimitValues(&most, limit, &type, 1);
    svcGetResourceLimitCurrentValues(&now, limit, &type, 1);
    svcCloseHandle(limit);
    const u32 remaining = u32(most - now) & ~0xFFFu;
    const u32 area = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN;
    u32 heap = remaining > __ctru_linear_heap_size ? remaining - __ctru_linear_heap_size : 0;
    if (heap > area) heap = area;
    __ctru_heap_size = heap;
    __ctru_linear_heap_size = remaining - heap;
    if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0, __ctru_heap_size, MEMOP_ALLOC, MemPerm(MEMPERM_READ | MEMPERM_WRITE)))) svcBreak(USERBREAK_PANIC);
    if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0, 0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR, MemPerm(MEMPERM_READ | MEMPERM_WRITE)))) svcBreak(USERBREAK_PANIC);
    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
    fake_heap_start = reinterpret_cast<char*>(__ctru_heap);
    fake_heap_end = fake_heap_start + __ctru_heap_size;
}
}

int main(int, char**) {
    osSetSpeedupEnable(true);   // New 3DS: 804 MHz and the L2 cache
    APT_SetAppCpuTimeLimit(80);  // core 1 is the system's; this much of it is ours (threads_3ds.cpp)
    // Too little memory (launched some way that didn't give the 124 MiB mode): say so, don't crash in a level build.
    if (envGetHeapSize() < 88u * 1024 * 1024) {
        gfxInitDefault();
        consoleInit(GFX_BOTTOM, nullptr);
        std::printf("\n d2d needs the New 3DS's extended\n memory mode and got %lu MiB.\n\n Install the .cia and start it from\n the HOME menu (not from FBI or the\n Homebrew Launcher).\n\n START to exit.\n", static_cast<unsigned long>(envGetHeapSize() >> 20));
        while (aptMainLoop()) { hidScanInput(); if (hidKeysDown() & KEY_START) break; gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank(); }
        gfxExit();
        return 0;
    }
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/d2d", 0777);
    mkdir("sdmc:/3ds/d2d/data", 0777);
    logs_3ds_start();            // the earlier runs' logs kept (and uploaded when a server is named: logs_3ds.cpp)
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
    const int result = d2d_main(int(words.size()), args.data());
    logs_3ds_exit();
    return result;
}

// newlib declares these and devkitARM has no users: the game's home-folder
// lookups fall back to $HOME, set above.
#include <pwd.h>
#include <unistd.h>
extern "C" {
uid_t getuid(void) { return 0; }
struct passwd* getpwuid(uid_t) { return nullptr; }
}
