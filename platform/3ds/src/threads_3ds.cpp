// SPDX-License-Identifier: GPL-3.0-or-later
// std::thread on the 3DS. libctru gives a C++ thread a small stack on the
// caller's core, where threads only take turns. The game's threads (the
// loader, the level builders, the floor bands) need a desktop-sized stack
// and are the reason a New 3DS has four cores, so the two calls are wrapped
// (--wrap in CMakeLists.txt): a thread made through pthread gets 768 KiB
// and the next of cores 2 and 3; a core that refuses falls back to the
// caller's. Threads libctru or SDL make for themselves are left alone.
#include <3ds.h>

#include <atomic>
#include <cstddef>
#include <sys/iosupport.h>

extern "C" {

Thread __real_threadCreate(ThreadFunc entrypoint, void* arg, size_t stack_size, int prio, int core_id, bool detached);
int __real___syscall_thread_create(struct __pthread_t** thread, void* (*func)(void*), void* arg, void* stack_addr, size_t stack_size);

static std::atomic<int> g_from_pthread{ 0 };
static std::atomic<unsigned> g_next_core{ 0 };

// The frame's own workers (the floor bands, the screen copy: tile_blit.cpp's pool) are made between
// d2d_urgent_threads(1) and (0). They keep the caller's priority and take a core each; every other
// thread (a level being built, an animation read, the log) runs below them, so a long job there
// gives its core up the moment a frame's worker wants it.
static std::atomic<int> g_urgent{ 0 };
static std::atomic<unsigned> g_next_urgent_core{ 0 };
void d2d_urgent_threads(int on) { g_urgent.store(on); }

Thread __wrap_threadCreate(ThreadFunc entrypoint, void* arg, size_t stack_size, int prio, int core_id, bool detached) {
    if (g_from_pthread.load() > 0) {
        static constexpr int kCores[] = { 2, 3 };   // not 1: the system's own core (HOME menu, input, sound)
        const bool urgent = g_urgent.load() > 0;
        const int core = kCores[(urgent ? g_next_urgent_core : g_next_core).fetch_add(1) % 2];
        const int wanted = urgent ? prio : (prio + 6 > 0x3F ? 0x3F : prio + 6);   // a larger number is a lower priority
        if (Thread made = __real_threadCreate(entrypoint, arg, stack_size, wanted, core, detached)) return made;
    }
    return __real_threadCreate(entrypoint, arg, stack_size, prio, core_id, detached);
}

int __wrap___syscall_thread_create(struct __pthread_t** thread, void* (*func)(void*), void* arg, void* stack_addr, size_t stack_size) {
    constexpr size_t kStack = 768 * 1024;
    g_from_pthread.fetch_add(1);
    const int result = __real___syscall_thread_create(thread, func, arg, stack_addr, stack_size < kStack ? kStack : stack_size);
    g_from_pthread.fetch_sub(1);
    return result;
}

}
