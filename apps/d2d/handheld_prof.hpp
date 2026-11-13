// SPDX-License-Identifier: GPL-3.0-or-later
// Where a frame's time goes, for the handheld port: named spans on the main
// thread, summed and read out (and zeroed) by whoever reports them.
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace d2d::client::prof {

struct Span { const char* name = nullptr; std::uint64_t ns = 0; std::uint32_t calls = 0; std::uint64_t frame_ns = 0; };   // frame_ns: since frame_end()
inline std::array<Span, 40> g_spans;

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
        const auto took = std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - from).count());
        into->ns += took; into->frame_ns += took;
        ++into->calls;
        into = nullptr;
    }
    ~Mark() { stop(); }
    Mark(const Mark&) = delete;
    Mark& operator=(const Mark&) = delete;
};

// This frame's spans of `least_ms` or more ("town.draw 212, light 180"), for a slow frame's report.
inline std::string frame_spans(double least_ms = 2) {
    std::string out;
    for (const auto& entry : g_spans) {
        if (!entry.name) break;
        if (double(entry.frame_ns) / 1e6 < least_ms) continue;
        out += (out.empty() ? "" : ", ") + std::string(entry.name) + " " + std::to_string(entry.frame_ns / 1000000);
    }
    return out;
}
inline void frame_end() { for (auto& entry : g_spans) entry.frame_ns = 0; }

}  // namespace d2d::client::prof
