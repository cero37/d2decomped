// SPDX-License-Identifier: GPL-3.0-or-later
// std::thread on the 3DS. libctru gives a C++ thread a small stack on the
// caller's core, where threads only take turns. The game's threads (the
// loader, the level builders, the floor bands) need a desktop-sized stack
// and are the reason a New 3DS has four cores, so the two calls are wrapped
// (--wrap in CMakeLists.txt): a thread made through pthread gets 768 KiB
// and the next of cores 2, 3 and 1; a core that refuses falls back to the
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

Thread __wrap_threadCreate(ThreadFunc entrypoint, void* arg, size_t stack_size, int prio, int core_id, bool detached) {
    if (g_from_pthread.load() > 0) {
        static constexpr int kCores[] = { 2, 3, 1 };
        const int core = kCores[g_next_core.fetch_add(1) % 3];
        if (Thread made = __real_threadCreate(entrypoint, arg, stack_size, prio, core, detached)) return made;
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
