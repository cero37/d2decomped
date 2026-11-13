// SPDX-License-Identifier: GPL-3.0-or-later
#include "handheld_hud.hpp"

#include "common.hpp"
#include "panels.hpp"
#include "scene.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string>

namespace d2d::client {

namespace {

struct Canvas {
    std::vector<std::uint8_t>& fb;
    void put(int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) const {
        if (x < 0 || y < 0 || x >= kBottomWidth || y >= kBottomHeight) return;
        auto* pixel = fb.data() + (std::size_t(y) * kScreenWidth + std::size_t(x)) * 4;
        pixel[0] = r; pixel[1] = g; pixel[2] = b; pixel[3] = 0xFF;
    }
    void rect(int left, int top, int right, int bottom, std::array<std::uint8_t, 3> c) const {
        for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) put(x, y, c[0], c[1], c[2]);
    }
    // A sunken slot: dark fill, a lit edge below and right.
    void slot(int left, int top, int right, int bottom) const {
        rect(left, top, right, bottom, { 10, 9, 8 });
        rect(left, top, right, top + 1, { 0, 0, 0 });
        rect(left, top, left + 1, bottom, { 0, 0, 0 });
        rect(left, bottom - 1, right, bottom, { 78, 68, 52 });
        rect(right - 1, top, right, bottom, { 78, 68, 52 });
    }
};

}  // namespace

void draw_bottom_hud(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const BottomHud& state) {
    using namespace d2d::d2s;
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    const Canvas canvas{ framebuffer };
    const int width = kBottomWidth, height = kBottomHeight;
    // The stone the panel is cut from.
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int grain = ((x * 7 + y * 13) ^ (x * y >> 3)) & 7;
            canvas.put(x, y, std::uint8_t(26 + grain), std::uint8_t(23 + grain), std::uint8_t(20 + grain));
        }
    if (!state.stats) return;
    const auto& stats = *state.stats;

    // The globes, in the PC panel's own end pieces (the statues), at the corners.
    auto at_bottom = [&](const d2d::dc6::Sprite& spr, int frame, int x, int bottom) {
        if (frame >= int(spr.frames_per_direction())) return;
        const auto& cel = spr.frame(0, std::uint32_t(frame));
        blit_sprite(framebuffer, cel, pal, x, bottom - int(cel.height));
    };
    const int panel_w = scene.ctrl_panel.frames_per_direction() >= 6 ? int(scene.ctrl_panel.frame(0, 5).width) : 117;
    at_bottom(scene.ctrl_panel, 0, 0, height);
    at_bottom(scene.ctrl_panel, 5, width - panel_w, height);
    auto fill = [&](int frame, int x, std::int64_t cur, std::int64_t max) {
        if (max <= 0 || frame >= int(scene.globes.frames_per_direction())) return;
        const auto& cel = scene.globes.frame(0, std::uint32_t(frame));
        const int rows = int(std::clamp<std::int64_t>(cur * 80 / max, cur > 0 ? 2 : 0, 80));
        const int cel_height = int(cel.height), top = height - 13 - cel_height;
        for (int y = std::max(cel_height - rows, 0); y < cel_height; ++y)
            for (int left = 0; left < int(cel.width); ++left)
                if (const auto idx = cel.pixels[std::size_t(y) * cel.width + std::size_t(left)]) {
                    const auto colour = pal[idx];
                    canvas.put(x + left, top + y, colour.r, colour.g, colour.b);
                }
    };
    const bool poisoned = state.hud && state.hud->poisoned;
    fill(poisoned ? 2 : 0, 29, stats.values[kLife], stats.values[kMaxLife]);
    fill(1, width - 0x6f, stats.values[kMana], stats.values[kMaxMana]);
    at_bottom(scene.globe_glass, 0, 28, height - 5);
    at_bottom(scene.globe_glass, 1, width - 0x6e, height - 9);

    const auto& small = scene.font_small;
    auto centred = [&](const d2d::font::Font& font, int centre, int top, const std::string& text, std::array<std::uint8_t, 3> c) {
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, centre - font.measure(text) / 2, top, text, c[0], c[1], c[2]);
    };
    centred(small, 29 + 40, height - 13 - 80 - small.line_height() - 2, std::format("{}/{}", stats.values[kLife] >> 8, stats.values[kMaxLife] >> 8), { 255, 255, 255 });
    centred(small, width - 0x6f + 40, height - 13 - 80 - small.line_height() - 2, std::format("{}/{}", stats.values[kMana] >> 8, stats.values[kMaxMana] >> 8), { 255, 255, 255 });

    // Between the globes: the belt as the D-pad that drinks from it, the
    // experience and stamina bars under it.
    const int mid = width / 2, cell = 29;
    const int pad_top = height - 30 - cell * 3;
    const std::array<std::array<int, 2>, 4> spots{ { { mid - cell / 2 - cell, pad_top + cell },      // column 0: left
                                                     { mid - cell / 2, pad_top },                   // 1: up
                                                     { mid - cell / 2 + cell, pad_top + cell },     // 2: right
                                                     { mid - cell / 2, pad_top + cell * 2 } } };    // 3: down
    for (const auto& spot : spots) canvas.slot(spot[0], spot[1], spot[0] + cell, spot[1] + cell);
    if (state.belt)
        for (const auto& item : *state.belt) {
            if (item.location != d2d::d2s::item_location::kBelt || item.column < 0 || item.column > 3) continue;
            const auto& spot = spots[std::size_t(item.column)];
            if (const auto* spr = scene.item_sprite(item); spr && spr->frames_per_direction() > 0) {
                const auto& cel = spr->frame(0, 0);
                blit_sprite(framebuffer, cel, scene.item_pal(item, pal), spot[0] + (cell - int(cel.width)) / 2, spot[1] + (cell - int(cel.height)) / 2);
            }
        }
    auto bar = [&](int top, std::int64_t cur, std::int64_t max, std::array<std::uint8_t, 3> c) {
        const int left = mid - 43, right = mid + 43;
        canvas.slot(left, top, right, top + 7);
        if (max > 0 && cur > 0) canvas.rect(left + 1, top + 1, left + 1 + int(std::clamp<std::int64_t>(cur * (right - left - 2) / max, 1, right - left - 2)), top + 6, c);
    };
    const auto lvl = stats.get(kLevel);
    std::int64_t exp_num = 0, exp_den = 0;
    if (lvl >= 1 && std::size_t(lvl) < scene.exp_next.size()) {
        const auto next = scene.exp_next[std::size_t(lvl)], prev = scene.exp_next[std::size_t(lvl - 1)];
        if (next > prev) { exp_num = std::int64_t(stats.values[kExp]) > std::int64_t(prev) ? std::int64_t(stats.values[kExp]) - std::int64_t(prev) : 0; exp_den = std::int64_t(next) - std::int64_t(prev); }
    }
    bar(height - 26, exp_num, exp_den, { 214, 176, 78 });
    bar(height - 16, stats.values[kStamina], stats.values[kMaxStamina], { 196, 190, 96 });

    // The top line: where you are, and your level.
    canvas.slot(4, 4, width - 4, 22);
    scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, 10, 6, std::string(state.level_name), 199, 179, 119);
    const auto level_text = std::format("Lv {}", lvl);
    scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, width - 10 - scene.font.measure(level_text), 6, level_text, 255, 255, 255);
}

}  // namespace d2d::client
