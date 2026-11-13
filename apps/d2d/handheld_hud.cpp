// SPDX-License-Identifier: GPL-3.0-or-later
#include "handheld_hud.hpp"

#include "common.hpp"
#include "panels.hpp"
#include "scene.hpp"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <cmath>
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
    // Five times a second is as fast as anything here needs to change; the frame keeps the last one between.
    static std::uint64_t drawn_ms = 0;
    const std::uint64_t now_ms = SDL_GetTicks();
    if (drawn_ms != 0 && now_ms - drawn_ms < 200) return;
    drawn_ms = now_ms;
    const int width = kBottomWidth, height = kBottomHeight;
    // The stone the panel is cut from.
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int grain = ((x * 7 + y * 13) ^ (x * y >> 3)) & 7;
            canvas.put(x, y, std::uint8_t(26 + grain), std::uint8_t(23 + grain), std::uint8_t(20 + grain));
        }
    if (!state.stats) return;
    const auto& stats = *state.stats;

    const auto& small = scene.font_small;
    const auto& tiny = scene.font_tiny.line_height() > 0 ? scene.font_tiny : scene.font_small;
    // A cel at half size, each 2x2 of it averaged; only its last `rows` source rows (a globe's fill).
    auto half = [&](const d2d::dc6::Frame& cel, int x, int y, int rows) {
        const int source_w = int(cel.width), source_h = int(cel.height);
        for (int row = 0; row < (source_h + 1) / 2; ++row)
            for (int column = 0; column < (source_w + 1) / 2; ++column) {
                int red = 0, green = 0, blue = 0, count = 0;
                for (int sy = row * 2; sy < row * 2 + 2 && sy < source_h; ++sy)
                    for (int sx = column * 2; sx < column * 2 + 2 && sx < source_w; ++sx) {
                        if (sy < source_h - rows) continue;
                        const auto idx = cel.pixels[std::size_t(sy) * cel.width + std::size_t(sx)];
                        if (!idx) continue;
                        const auto colour = pal[idx];
                        red += colour.r; green += colour.g; blue += colour.b; ++count;
                    }
                if (count >= 2) canvas.put(x + column, y + row, std::uint8_t(red / count), std::uint8_t(green / count), std::uint8_t(blue / count));
            }
    };
    // The globes at half size in the bottom corners, the number on each.
    constexpr int kGlobe = 42;                                  // the socket's box
    const int globe_y = height - kGlobe - 2;
    auto globe = [&](int frame, int x, std::int64_t cur, std::int64_t max, const std::string& text) {
        for (int row = 0; row < kGlobe; ++row)                      // the socket: a dark disc with a lit rim
            for (int column = 0; column < kGlobe; ++column) {
                const int dx = column * 2 - kGlobe + 1, dy = row * 2 - kGlobe + 1, reach = dx * dx + dy * dy;
                if (reach <= kGlobe * kGlobe) { const bool rim = reach > (kGlobe - 3) * (kGlobe - 3); canvas.put(x + column, globe_y + row, rim ? 96 : 8, rim ? 82 : 6, rim ? 58 : 6); }
            }
        if (max > 0 && frame < int(scene.globes.frames_per_direction())) {
            const auto& cel = scene.globes.frame(0, std::uint32_t(frame));
            const int source = int(cel.height);
            const int rows = int(std::clamp<std::int64_t>(cur * source / max, cur > 0 ? 2 : 0, source));
            half(cel, x + (kGlobe - (int(cel.width) + 1) / 2) / 2, globe_y + (kGlobe - (source + 1) / 2) / 2, rows);
        }
        small.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x + (kGlobe - small.measure(text)) / 2, globe_y + (kGlobe - small.line_height()) / 2, text, 255, 255, 255);
    };
    const bool poisoned = state.hud && state.hud->poisoned;
    globe(poisoned ? 2 : 0, 3, stats.values[kLife], stats.values[kMaxLife], std::format("{}", stats.values[kLife] >> 8));
    globe(1, width - kGlobe - 3, stats.values[kMana], stats.values[kMaxMana], std::format("{}", stats.values[kMana] >> 8));

    // Between them, two rows: the skills on their buttons, then the belt.
    const int mid = width / 2, cell = 29;
    const int belt_left = mid - cell * 2, belt_top = height - 2 - cell;
    std::array<std::array<int, 2>, 4> spots{};
    for (int i = 0; i < 4; ++i) { spots[std::size_t(i)] = { belt_left + i * cell, belt_top }; canvas.slot(belt_left + i * cell, belt_top, belt_left + (i + 1) * cell, belt_top + cell); }
    if (state.belt)
        for (const auto& item : *state.belt) {
            if (item.location != d2d::d2s::item_location::kBelt || item.column < 0 || item.column > 3) continue;
            const auto& spot = spots[std::size_t(item.column)];
            if (const auto* spr = scene.item_sprite(item); spr && spr->frames_per_direction() > 0) {
                const auto& cel = spr->frame(0, 0);
                blit_sprite(framebuffer, cel, scene.item_pal(item, pal), spot[0] + (cell - int(cel.width)) / 2, spot[1] + (cell - int(cel.height)) / 2);
            }
        }
    constexpr int kIcon = 24, kPitch = 30;                       // a skill's icon at half size, its button's name beside it
    const int skill_top = belt_top - 3 - kIcon - 2;
    {
        const auto& skills = g_bottom_skills;
        const int left = mid - (7 * kPitch - (kPitch - kIcon - 2)) / 2;
        for (int i = 0; i < 7; ++i) {
            const int x = left + i * kPitch, id = i == 0 ? skills.main : skills.slots[std::size_t(i - 1)];
            const std::string& label = i == 0 ? skills.main_label : skills.labels[std::size_t(i - 1)];
            canvas.slot(x, skill_top, x + kIcon + 2, skill_top + kIcon + 2);
            if (const auto* skill = id >= 0 ? scene.skills.get(id) : nullptr) {
                int column = -1;
                for (int cls = 0; cls < 7; ++cls) if (skill->cls == d2d::rules::kClassCode[std::size_t(cls)]) column = cls;
                const auto& spr = column >= 0 ? scene.skill_icons[std::size_t(column)] : scene.generic_skill_icons;
                if (skill->icon >= 0 && spr.frames_per_direction() > std::uint32_t(skill->icon)) { const auto& cel = spr.frame(0, std::uint32_t(skill->icon)); half(cel, x + 1, skill_top + 1, int(cel.height)); }
            } else if (id <= -3) {
                static constexpr std::array<const char*, 7> kActions{ "Use", "Itm", "Inv", "Chr", "Skl", "Map", "Qst" };
                if (const int action = -3 - id; action < int(kActions.size())) tiny.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x + 3, skill_top + 9, kActions[std::size_t(action)], 199, 179, 119);
            }
            if (!label.empty()) {                                       // the button, on a dark tab in the slot's corner
                const int tab_w = tiny.measure(label) + 2, tab_h = tiny.line_height();
                // font6's cells carry their glyphs low: the text sits tab_h - 3 under the y it is given.
                canvas.rect(x + 1, skill_top + kIcon - 7, x + 1 + tab_w, skill_top + kIcon + 1, { 0, 0, 0 });
                tiny.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x + 2, skill_top + kIcon + 1 - tab_h - (tab_h > 9 ? tab_h - 9 : 0), label, skills.alternate ? 255 : 199, skills.alternate ? 210 : 179, skills.alternate ? 90 : 119);
            }
        }
    }
    // Experience and stamina: two thin lines over the skills.
    auto bar = [&](int top, std::int64_t cur, std::int64_t max, std::array<std::uint8_t, 3> c) {
        const int left = 3 + kGlobe + 4, right = width - 3 - kGlobe - 4;
        canvas.rect(left, top, right, top + 3, { 8, 7, 6 });
        if (max > 0 && cur > 0) canvas.rect(left, top + 1, left + int(std::clamp<std::int64_t>(cur * (right - left) / max, 1, right - left)), top + 2, c);
    };
    const auto lvl = stats.get(kLevel);
    std::int64_t exp_num = 0, exp_den = 0;
    if (lvl >= 1 && std::size_t(lvl) < scene.exp_next.size()) {
        const auto next = scene.exp_next[std::size_t(lvl)], prev = scene.exp_next[std::size_t(lvl - 1)];
        if (next > prev) { exp_num = std::int64_t(stats.values[kExp]) > std::int64_t(prev) ? std::int64_t(stats.values[kExp]) - std::int64_t(prev) : 0; exp_den = std::int64_t(next) - std::int64_t(prev); }
    }
    const int bars_top = skill_top - 8;
    bar(bars_top, exp_num, exp_den, { 214, 176, 78 });
    bar(bars_top + 4, stats.values[kStamina], stats.values[kMaxStamina], { 150, 190, 110 });

    // The automap, always open here: the mini map's cels round the player.
    {
        const int left = 3, top = 22, right = width - 3, bottom = bars_top - 3;
        canvas.slot(left, top, right, bottom);
        if (state.automap && scene.automap_cels_small.frames_per_direction() > 0) {
            const auto& map = *state.automap;
            const int x0 = left + 1, y0 = top + 1, x1 = right - 2, y1 = bottom - 2;
            const int centre_x = (x0 + x1) / 2, centre_y = (y0 + y1) / 2;
            constexpr int kDivisor = 20;
            const int scroll_x = int(std::lround((state.map_x - state.map_y) * 80 / float(kDivisor))) + 8 - centre_x;
            const int scroll_y = int(std::lround((state.map_x + state.map_y) * 40 / float(kDivisor))) - 8 - centre_y;
            const auto& miniature = map.cel_file > 0 && map.cel_file < 4 && scene.automap_miniatures_small[std::size_t(map.cel_file)].frames_per_direction() > 0
                                        ? scene.automap_miniatures_small[std::size_t(map.cel_file)] : scene.automap_miniatures[0];
            for (int list = 0; list < 4; ++list)
                for (const auto& cell : map.cells) {
                    const auto& cels = list == 3 ? miniature : scene.automap_cels_small;
                    if (cell.list != list || cell.cel < 0 || std::uint32_t(cell.cel) >= cels.frames_per_direction()) continue;
                    const auto& cel = cels.frame(0, std::uint32_t(cell.cel));
                    const int x = cell.x * 10 / kDivisor - scroll_x, cel_top = cell.y * 10 / kDivisor - scroll_y - int(cel.height) + 1;
                    if (x > x1 || x + int(cel.width) < x0 || cel_top > y1 || cel_top + int(cel.height) < y0) continue;
                    for (int row = 0; row < int(cel.height); ++row)
                        for (int column = 0; column < int(cel.width); ++column) {
                            const auto index = cel.pixels[std::size_t(row) * cel.width + std::size_t(column)];
                            const int px = x + column, py = cel_top + row;
                            if (index == 0 || px < x0 || px > x1 || py < y0 || py > y1) continue;
                            const auto colour = pal[index];
                            canvas.put(px, py, colour.r, colour.g, colour.b);
                        }
                }
            auto mark = [&](float unit_x, float unit_y, std::array<std::uint8_t, 3> c) {
                const int mx = int(std::lround((unit_x - unit_y) * 80 / float(kDivisor))) - scroll_x + 8;
                const int my = int(std::lround((unit_x + unit_y) * 40 / float(kDivisor))) - scroll_y - 8;
                for (int d = -3; d <= 3; ++d)
                    for (int e : { d, -d }) { const int px = mx + d, py = my + e / 2; if (px >= x0 && px <= x1 && py >= y0 && py <= y1) canvas.put(px, py, c[0], c[1], c[2]); }
            };
            for (const auto& other : map.marks) mark(other.x, other.y, { std::uint8_t(other.rgb[0]), std::uint8_t(other.rgb[1]), std::uint8_t(other.rgb[2]) });
            mark(state.map_x, state.map_y, { 80, 120, 255 });
        }
    }

    // The top line: where you are, and your level.
    canvas.slot(3, 3, width - 3, 20);
    scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, 9, 4, std::string(state.level_name), 199, 179, 119);
    const auto level_text = std::format("Lv {}", lvl);
    scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, width - 9 - scene.font.measure(level_text), 4, level_text, 255, 255, 255);
}

}  // namespace d2d::client
