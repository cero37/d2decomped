// SPDX-License-Identifier: GPL-3.0-or-later
// The whole of a New 3DS's memory for the game. In the 124 MiB mode the
// ordinary heap's address range holds 96 MiB; the rest can only be linear
// memory (main_3ds.cpp's __system_allocateHeaps), which nothing but sound
// buffers asks for. So when malloc has nothing left, the allocation comes
// from the linear heap instead (malloc, calloc, realloc, memalign and free
// are wrapped: --wrap in CMakeLists.txt; operator new goes through malloc).
// A few MiB of linear memory stay back for the sound.
#include <3ds.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <malloc.h>

extern "C" {

extern u32 __ctru_linear_heap;
extern u32 __ctru_linear_heap_size;

void* __real_malloc(size_t size);
void* __real_calloc(size_t count, size_t size);
void* __real_realloc(void* block, size_t size);
void* __real_memalign(size_t alignment, size_t size);
void __real_free(void* block);

static LightLock g_linear_lock;
static bool g_linear_lock_made = false;
static constexpr size_t kSoundReserve = 5 * 1024 * 1024;

static bool is_linear(const void* block) {
    const auto at = reinterpret_cast<std::uintptr_t>(block);
    return at >= __ctru_linear_heap && at < __ctru_linear_heap + __ctru_linear_heap_size;
}

static void lock() {
    if (!g_linear_lock_made) { LightLock_Init(&g_linear_lock); g_linear_lock_made = true; }   // first used before any second thread exists
    LightLock_Lock(&g_linear_lock);
}

// The linear allocator keeps its own small records with malloc: one that
// fails while we are in here must not come back in (the lock isn't recursive).
static __thread bool t_inside = false;

static void* from_linear(size_t size, size_t alignment) {
    if (t_inside) return nullptr;
    t_inside = true;
    lock();
    void* block = nullptr;
    if (linearSpaceFree() >= size + kSoundReserve) block = linearMemAlign(size, alignment < 16 ? 16 : alignment);
    LightLock_Unlock(&g_linear_lock);
    t_inside = false;
    return block;
}

void* __wrap_malloc(size_t size) {
    void* block = __real_malloc(size);
    return block || size == 0 ? block : from_linear(size, 16);
}

void* __wrap_calloc(size_t count, size_t size) {
    void* block = __real_calloc(count, size);
    if (block || count == 0 || size == 0) return block;
    if (size > SIZE_MAX / count) return nullptr;
    block = from_linear(count * size, 16);
    if (block) std::memset(block, 0, count * size);
    return block;
}

void* __wrap_memalign(size_t alignment, size_t size) {
    void* block = __real_memalign(alignment, size);
    return block || size == 0 ? block : from_linear(size, alignment);
}

void __wrap_free(void* block) {
    if (!is_linear(block)) { __real_free(block); return; }
    lock();
    linearFree(block);
    LightLock_Unlock(&g_linear_lock);
}

void* __wrap_realloc(void* block, size_t size) {
    if (!block) return __wrap_malloc(size);
    if (size == 0) { __wrap_free(block); return nullptr; }
    if (is_linear(block)) {
        lock();
        const size_t had = linearGetSize(block);
        LightLock_Unlock(&g_linear_lock);
        if (size <= had) return block;
        void* moved = __wrap_malloc(size);
        if (!moved) return nullptr;
        std::memcpy(moved, block, had);
        __wrap_free(block);
        return moved;
    }
    if (void* grown = __real_realloc(block, size)) return grown;
    void* moved = from_linear(size, 16);
    if (!moved) return nullptr;
    const size_t had = malloc_usable_size(block);
    std::memcpy(moved, block, had < size ? had : size);
    __real_free(block);
    return moved;
}

}
