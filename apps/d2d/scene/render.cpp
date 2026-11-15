// SPDX-License-Identifier: GPL-3.0-or-later
// The Scene's rendering support: overlay sprites and drawn-sprite tracking.
#include "scene.hpp"

#include <thread>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <dc6.hpp>
#include <dcc.hpp>
#include <log.hpp>

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <format>
#include <fstream>
#include <ios>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace d2d::client {

// Present a DC6's frames through the DCC interface used by the renderer.
d2d::dcc::Sprite dc6_as_dcc(std::span<const std::byte> bytes) {
    const d2d::dc6::Sprite cels(bytes);
    std::vector<d2d::dcc::Frame> frames;
    for (const auto& cel : cels.frames()) {
        const int width = int(cel.width), height = int(cel.height), top = cel.offset_y - height + 1;
        frames.push_back({ width, height, cel.offset_x, cel.offset_y, cel.offset_x, top, cel.offset_x + width, cel.offset_y + 1, cel.pixels });
    }
    return d2d::dcc::Sprite::from_frames(std::uint8_t(cels.directions()), std::int32_t(cels.frames_per_direction()), std::move(frames));
}

// Load each overlay once, using the real asset loader for either sprite format.
const d2d::dcc::Sprite* Scene::overlay_sprite(const OverlayInfo& overlay) const {
    auto [found, fresh] = overlay_sprites.try_emplace(&overlay);
    if (fresh) {
        // Overlay row 0x7a (mephisto) is a DC6 (FUN_005fe610); the rest DCCs.
        const std::string path = R"(data\global\overlays\)" + overlay.file;
        try {
            if (auto bytes = mpqs.try_read(path + ".dcc")) found->second = d2d::dcc::Sprite(*bytes);
            else if (auto cels = mpqs.try_read(path + ".dc6")) found->second = dc6_as_dcc(*cels);
        } catch (const std::exception& error) { d2d::log::warn("overlay {}: {}", overlay.file, error.what()); }
    }
    const bool usable = found->second.directions() && found->second.frames_per_direction();
    return usable ? &found->second : nullptr;
}

// A missile's cel, decoded at its first draw.
const d2d::dcc::Sprite* Scene::missile_cel(const std::string& name) const {
    auto [found, fresh] = missile_cels.try_emplace(name);
    if (fresh) {
        const auto missile = missiles.find(name);
        if (missile != missiles.end())
            try {
                if (auto bytes = mpqs.try_read(R"(data\global\missiles\)" + missile->second.cel_file + ".dcc")) found->second = d2d::dcc::Sprite(*bytes);
            } catch (const std::exception& error) { d2d::log::warn("missile cel {}: {}", name, error.what()); }
    }
    const bool usable = found->second.directions() && found->second.frames_per_direction();
    return usable ? &found->second : nullptr;
}

// Record each sprite once when the optional animation audit is enabled.
// The animation worker: one thread, the layers in the order they were asked for.
void Scene::anim_load(std::shared_ptr<AnimLoad> load) {
    struct Worker {
        std::mutex mutex;
        std::condition_variable wake;
        std::deque<std::shared_ptr<AnimLoad>> queue, ahead;   // ahead: a unit on screen waits on it (not read ahead)
        std::thread thread{ [this] {
            d2d::mpq::t_urgent_reads = true;              // the area builder's reads step aside for these
            for (;;) {
                std::shared_ptr<AnimLoad> job;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [this] { return !queue.empty() || !ahead.empty(); });
                    if (!ahead.empty()) { job = std::move(ahead.front()); ahead.pop_front(); }
                    else {   // a read-ahead now wanted on screen (a death happening) before the others
                        auto pick = std::ranges::find_if(queue, [](const auto& queued) { return queued->urgent.load(std::memory_order_relaxed); });
                        if (pick == queue.end()) pick = queue.begin();
                        job = std::move(*pick);
                        queue.erase(pick);
                    }
                }
                // A fast machine standing in for a handheld's card (testing): each layer this much slower.
                static const int delay_ms = [] { const char* text = std::getenv("D2D_ANIM_DELAY_MS"); return text ? std::atoi(text) : 0; }();
                if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                try {
                    if (!job->path.empty() && g_anim_loader)
                        if (auto bytes = g_anim_loader(job->path)) job->bytes = std::move(*bytes);
                    if (!job->bytes.empty()) {
                        job->result = d2d::dcc::Sprite(job->bytes);
                        if (job->tint) job->result.remap(job->tint);
                    }
                } catch (const std::exception& error) { d2d::log::warn("{} ({}): {}", job->name, job->path, error.what()); }
                std::vector<std::byte>().swap(job->bytes);
                job->done.store(true, std::memory_order_release);
            }
        } };
    };
    static Worker* worker = new Worker;      // never joined: it runs to the end of the program
    { const std::scoped_lock lock(worker->mutex); (g_anim_read_ahead ? worker->queue : worker->ahead).push_back(std::move(load)); }
    worker->wake.notify_one();
}

void Scene::trim_anims(std::uint32_t now_ms, std::uint32_t idle_ms) const {
    g_anim_now_ms = now_ms;
    static std::uint32_t checked_ms = 0;
    if (idle_ms >= 15000 && now_ms - checked_ms < 1000) return;   // a look a second is plenty (a shorter idle: asked for now)
    checked_ms = now_ms;
    const auto trim = [&](const PlayerAnim& anim) {
        if (now_ms - anim.used_ms < idle_ms) return;
        for (std::size_t layer = 0; layer < anim.decoded.size(); ++layer)
            if (!anim.path[layer].empty() && !anim.dropped[layer] && anim.dcc[layer].empty() && anim.decoded[layer].frames_per_direction() > 0) {
                anim.decoded[layer] = {};
                anim.dropped[layer] = true;
            }
    };
    for (const auto& [key, anim] : composites) trim(anim);
    for (const auto& [key, anim] : npc_anims) trim(anim);
}

void Scene::cap_anims(std::uint32_t now_ms, std::size_t budget) const {
    static std::uint32_t checked_ms = 0;
    if (now_ms - checked_ms < 1000) return;
    checked_ms = now_ms;
    const auto bytes_of = [](const PlayerAnim& anim) {
        std::size_t bytes = 0;
        for (const auto& layer : anim.decoded) for (const auto& frame : layer.frames()) bytes += frame.pixels.size();
        return bytes;
    };
    std::vector<std::pair<std::uint32_t, const PlayerAnim*>> held;
    std::size_t total = 0;
    for (const auto& [key, anim] : npc_anims)
        if (const auto bytes = bytes_of(anim); bytes > 0) { total += bytes; held.emplace_back(anim.used_ms, &anim); }
    if (total <= budget) return;
    std::ranges::sort(held, [&](const auto& a, const auto& b) { return std::int32_t(a.first - b.first) < 0; });   // longest undrawn first
    std::size_t freed = 0;
    for (const auto& [used, anim] : held) {
        if (total - freed <= budget) break;
        if (now_ms - used < 500) break;                       // drawn just now: kept
        for (std::size_t layer = 0; layer < anim->decoded.size(); ++layer)
            if (!anim->path[layer].empty() && !anim->dropped[layer] && anim->dcc[layer].empty() && anim->decoded[layer].frames_per_direction() > 0) {
                for (const auto& frame : anim->decoded[layer].frames()) freed += frame.pixels.size();
                anim->decoded[layer] = {};
                anim->dropped[layer] = true;
            }
    }
    if (freed) d2d::log::info("memory: monster animations {} KiB over their {} KiB, {} KiB let go", total / 1024, budget / 1024, freed / 1024);
}

std::string Scene::cache_report() const {
    const auto dcc_bytes = [](const d2d::dcc::Sprite& sprite) { std::size_t bytes = 0; for (const auto& frame : sprite.frames()) bytes += frame.pixels.size(); return bytes; };
    const auto anim_bytes = [&](const PlayerAnim& anim, std::size_t& decoded, std::size_t& raw) {
        for (std::size_t layer = 0; layer < anim.decoded.size(); ++layer) { decoded += dcc_bytes(anim.decoded[layer]); raw += anim.dcc[layer].size(); }
    };
    std::size_t hero = 0, hero_raw = 0, npc = 0, npc_raw = 0, missiles = 0, overlays = 0;
    for (const auto& [key, anim] : composites) anim_bytes(anim, hero, hero_raw);
    for (const auto& [key, anim] : npc_anims) anim_bytes(anim, npc, npc_raw);
    for (const auto& [key, sprite] : missile_cels) missiles += dcc_bytes(sprite);
    for (const auto& [key, sprite] : overlay_sprites) overlays += dcc_bytes(sprite);
    return std::format("hero anims {} KiB (+{} raw, {} kept), monsters/NPCs {} KiB (+{} raw, {} kept), missiles {} KiB ({}), overlays {} KiB ({}), "
                       "item sprites {}, palshifts {}, decoded tiles {} KiB",
                       hero / 1024, hero_raw / 1024, composites.size(), npc / 1024, npc_raw / 1024, npc_anims.size(), missiles / 1024, missile_cels.size(),
                       overlays / 1024, overlay_sprites.size(), item_sprites.size() + flippy_sprites.size(), palshifts.size(), d2d::dt1::File::decoded_bytes() / 1024);
}

void Scene::note_drawn(std::string_view kind, std::string_view name) {
    static std::unordered_set<std::string> drawn;
    static std::ofstream out = [] {
        const char* path = std::getenv("D2D_ANIM_LOG");
        return path && *path ? std::ofstream(path, std::ios::app) : std::ofstream{};
    }();
    if (!out.is_open() || name.empty()) return;
    if (auto line = std::format("{} {}", kind, name); drawn.insert(line).second) out << line << '\n' << std::flush;
}

}  // namespace d2d::client
