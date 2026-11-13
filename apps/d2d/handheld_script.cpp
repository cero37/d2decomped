// SPDX-License-Identifier: GPL-3.0-or-later
#include "handheld_script.hpp"

#include <devctl.hpp>
#include <log.hpp>

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace d2d::client {

void handheld_script_step(d2d::devctl::Channel& channel) {
    static bool loaded = false;
    static std::vector<std::string> lines;
    static std::size_t next = 0;
    static std::uint64_t wait_until = 0;
    if (!loaded) {
        loaded = true;
        std::string path;
        if (const char* named = std::getenv("D2D_SCRIPT")) path = named;
        else if (const char* user_dir = std::getenv("D2D_USER_DIR")) path = std::string(user_dir) + "/script.txt";
        if (path.empty()) return;
        std::ifstream file(path);
        if (!file) return;
        for (std::string line; std::getline(file, line);) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty() && line[0] != '#') lines.push_back(line);
        }
        d2d::log::info("script: {} commands from {}", lines.size(), path);
        g_script_running = !lines.empty();
    }
    const std::uint64_t now = SDL_GetTicks();
    while (next < lines.size() && now >= wait_until) {
        const std::string& line = lines[next++];
        if (line.rfind("wait ", 0) == 0) { wait_until = now + std::uint64_t(std::max(0, std::atoi(line.c_str() + 5))); continue; }
        std::string reply = channel.run(line);
        // The whole reply, a line a log line (a state dump is the point of asking).
        std::size_t from = 0;
        bool first = true;
        while (from < reply.size()) {
            const auto end = reply.find('\n', from);
            const std::string part = reply.substr(from, end == std::string::npos ? std::string::npos : end - from);
            if (!part.empty()) { if (first) d2d::log::info("script: {} -> {}", line, part); else d2d::log::info("script:   {}", part); }
            first = false;
            if (end == std::string::npos) break;
            from = end + 1;
        }
    }
}

}  // namespace d2d::client
