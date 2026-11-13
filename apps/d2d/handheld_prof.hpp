// SPDX-License-Identifier: GPL-3.0-or-later
// Where a frame's time goes, for the handheld port: named spans on the main
// thread, summed and read out (and zeroed) by whoever reports them.
#pragma once

#include <array>
#include <chrono>
#include <cstdint>

namespace d2d::client::prof {

struct Span { const char* name = nullptr; std::uint64_t ns = 0; std::uint32_t calls = 0; };
inline std::array<Span, 24> g_spans;

inline Span& span(const char* name) {
    for (auto& entry : g_spans) {
        if (entry.name == name) return entry;
        if (!entry.name) { entry.name = name; return entry; }
    }
    return g_spans.back();
}

// Times from its making to stop() (or its end).
struct Mark {
    Span* into;
    std::chrono::steady_clock::time_point from = std::chrono::steady_clock::now();
    explicit Mark(const char* name) : into(&span(name)) {}
    void stop() {
        if (!into) return;
        into->ns += std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - from).count());
        ++into->calls;
        into = nullptr;
    }
    ~Mark() { stop(); }
    Mark(const Mark&) = delete;
    Mark& operator=(const Mark&) = delete;
};

}  // namespace d2d::client::prof
