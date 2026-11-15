// SPDX-License-Identifier: GPL-3.0-or-later
#include "handheld_hud.hpp"

#include "common.hpp"
#include "panels.hpp"
#include "scene.hpp"
#include "skillpad.hpp"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
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
    // (At once when the list, the ring or the zoom changes: those answer a button.)
    static int drawn_state = -1;
    const auto& lists = g_bottom_skills;
    const int state_now = (lists.strip_on ? 1 : 0) | (lists.wheel_on ? 2 : 0) | lists.strip_at << 2 | lists.wheel_at << 8 | lists.wheel_page << 16 | g_handheld_zoom << 24 | int(lists.feedback.size()) << 26;
    if (drawn_ms != 0 && now_ms - drawn_ms < 200 && state_now == drawn_state) return;
    drawn_ms = now_ms; drawn_state = state_now;
    ++g_bottom_serial;
    const int width = kBottomWidth, height = kBottomHeight;
    // The stone the panel is cut from.
    static const std::vector<std::uint8_t> stone = [&] {   // worked out once, copied a row at a time after
        std::vector<std::uint8_t> made(std::size_t(width) * std::size_t(height) * 4);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const int grain = ((x * 7 + y * 13) ^ (x * y >> 3)) & 7;
                auto* pixel = made.data() + (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4;
                pixel[0] = std::uint8_t(26 + grain); pixel[1] = std::uint8_t(23 + grain); pixel[2] = std::uint8_t(20 + grain); pixel[3] = 0xFF;
            }
        return made;
    }();
    for (int y = 0; y < height; ++y)
        std::memcpy(framebuffer.data() + std::size_t(y) * kScreenWidth * 4, stone.data() + std::size_t(y) * std::size_t(width) * 4, std::size_t(width) * 4);
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
    // Each belt column's d-pad direction (up, down, left, right, as the pad's Belt 1..4 are bound): an arrow on a dark tab in its corner.
    for (int column = 0; column < 4; ++column) {
        const int x = spots[std::size_t(column)][0] + 1, y = spots[std::size_t(column)][1] + 1;
        canvas.rect(x, y, x + 9, y + 9, { 0, 0, 0 });
        for (int step = 0; step < 4; ++step)                         // a triangle, its point the way the arrow goes
            for (int across = -step; across <= step; ++across) {
                const int px = column == 0 ? x + 4 + across : column == 1 ? x + 4 + across : column == 2 ? x + 2 + step : x + 6 - step;
                const int py = column == 0 ? y + 2 + step : column == 1 ? y + 6 - step : column == 2 ? y + 4 + across : y + 4 + across;
                canvas.put(px, py, 255, 220, 110);
            }
    }
    for (int column = 0; column < 4; ++column)                   // a belt column its d-pad direction just drank from: a bright rim for a moment
        if (const auto fired = g_bottom_skills.belt_fired[std::size_t(column)]; fired != 0 && g_bottom_skills.now - fired < 220) {
            const int x = spots[std::size_t(column)][0], y = spots[std::size_t(column)][1];
            canvas.rect(x, y, x + cell, y + 2, { 255, 240, 160 }); canvas.rect(x, y + cell - 2, x + cell, y + cell, { 255, 240, 160 });
            canvas.rect(x, y, x + 2, y + cell, { 255, 240, 160 }); canvas.rect(x + cell - 2, y, x + cell, y + cell, { 255, 240, 160 });
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
            // Not to be had now: the icon dimmed (blue-grey for want of mana); a delay still running counts down over it.
            if (const int status = skills.status[std::size_t(i)]; status != 0 && id > 0) {
                for (int py = skill_top + 1; py < skill_top + kIcon + 1; ++py)
                    for (int px = x + 1; px < x + kIcon + 1; ++px) {
                        auto* pixel = &framebuffer[(std::size_t(py) * kScreenWidth + std::size_t(px)) * 4];
                        const int grey = (pixel[0] * 3 + pixel[1] * 6 + pixel[2]) / 10 * 2 / 5;
                        pixel[0] = std::uint8_t(grey); pixel[1] = std::uint8_t(grey); pixel[2] = std::uint8_t(status == 1 ? std::min(255, grey + 50) : grey);
                    }
                if (status == 8 && skills.wait_ms[std::size_t(i)] > 0) {
                    const int wait = skills.wait_ms[std::size_t(i)];
                    const std::string left_text = wait >= 10000 ? std::to_string((wait + 999) / 1000) : std::format("{}.{}", wait / 1000, wait % 1000 / 100);
                    small.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x + 1 + (kIcon - small.measure(left_text)) / 2, skill_top + 1 + (kIcon - small.line_height()) / 2 - 2, left_text, 255, 255, 255);
                }
            }
            // Its button just worked it: a bright rim for a moment.
            if (skills.fired[std::size_t(i)] != 0 && skills.now - skills.fired[std::size_t(i)] < 220) {
                canvas.rect(x, skill_top, x + kIcon + 2, skill_top + 2, { 255, 240, 160 }); canvas.rect(x, skill_top + kIcon, x + kIcon + 2, skill_top + kIcon + 2, { 255, 240, 160 });
                canvas.rect(x, skill_top, x + 2, skill_top + kIcon + 2, { 255, 240, 160 }); canvas.rect(x + kIcon, skill_top, x + kIcon + 2, skill_top + kIcon + 2, { 255, 240, 160 });
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

    // The automap, always open here: the mini map's cels round the player. The Start list or the
    // skill ring takes its place while it is up.
    {
        const int left = 3, top = 22, right = width - 3, bottom = bars_top - 3;
        canvas.slot(left, top, right, bottom);
        if (lists.strip_on) {
            for (int index = 0; index < int(skillpad::kPanels.size()); ++index) {
                const int x = 6 + index % 3 * 103, y = 25 + index / 3 * 45;
                const bool at = index == lists.strip_at;
                canvas.rect(x, y, x + 100, y + 42, at ? std::array<std::uint8_t, 3>{ 214, 176, 78 } : std::array<std::uint8_t, 3>{ 78, 68, 52 });
                canvas.rect(x + 1, y + 1, x + 99, y + 41, at ? std::array<std::uint8_t, 3>{ 52, 42, 26 } : std::array<std::uint8_t, 3>{ 26, 23, 20 });
                const auto name = skillpad::kPanels[std::size_t(skillpad::handheld_panel(index))];
                scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x + 50 - scene.font.measure(name) / 2, y + 21 - scene.font.line_height() / 2, name, at ? 255 : 199, at ? 255 : 179, at ? 255 : 119);
            }
        } else if (lists.wheel_on) {
            const int count = int(lists.wheel_ids.size()), start = std::clamp(lists.wheel_page, 0, std::max(count - 1, 0));
            const int shown = std::min(skillpad::kWheelShown, count - start);
            const int centre_x = width / 2, centre_y = (top + bottom) / 2 + 4;
            for (int index = 0; index < shown; ++index) {
                const float angle = (float(index) + .5f) * skillpad::kTau / float(shown);
                const int x = centre_x + int(std::lround(std::sin(angle) * 100.f)) - 13, y = centre_y - int(std::lround(std::cos(angle) * 46.f)) - 13;
                const int id = lists.wheel_ids[std::size_t(start + index)];
                const bool at = start + index == lists.wheel_at;
                canvas.rect(x - 1, y - 1, x + 27, y + 27, at ? std::array<std::uint8_t, 3>{ 255, 220, 110 } : std::array<std::uint8_t, 3>{ 60, 52, 40 });
                canvas.rect(x, y, x + 26, y + 26, { 10, 9, 8 });
                if (const auto* skill = id >= 0 ? scene.skills.get(id) : nullptr) {
                    int column = -1;
                    for (int cls = 0; cls < 7; ++cls) if (skill->cls == d2d::rules::kClassCode[std::size_t(cls)]) column = cls;
                    const auto& spr = column >= 0 ? scene.skill_icons[std::size_t(column)] : scene.generic_skill_icons;
                    if (skill->icon >= 0 && spr.frames_per_direction() > std::uint32_t(skill->icon)) { const auto& cel = spr.frame(0, std::uint32_t(skill->icon)); half(cel, x + 1, y + 1, int(cel.height)); }
                } else {
                    const auto label = skillpad::action_label(id, false);
                    tiny.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x + 13 - tiny.measure(label) / 2, y + 9, label, 199, 179, 119);
                }
            }
            if (count > 0) {
                const int id = lists.wheel_ids[std::size_t(std::clamp(lists.wheel_at, 0, count - 1))];
                const auto* skill = id >= 0 ? scene.skills.get(id) : nullptr;
                const std::string name = skill ? skill->name : std::string(skillpad::action_label(id, true));
                scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, centre_x - scene.font.measure(name) / 2, centre_y - 16, name, 255, 255, 255);
                // Two short lines: one ran edge to edge over the slots' icons (the emulator play test).
                const std::string how = lists.feedback.empty() ? std::string("Press a button (B too)") : lists.feedback;
                small.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, centre_x - small.measure(how) / 2, centre_y + 2, how, 199, 179, 119);
                if (lists.feedback.empty()) {
                    const std::string more = "or touch a slot . Select: done";
                    small.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, centre_x - small.measure(more) / 2, centre_y + 14, more, 199, 179, 119);
                }
            }
        } else if (state.automap && scene.automap_cels_small.frames_per_direction() > 0) {
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
            // The ways out: a gold diamond and the place's name; one off the map sits on its edge, toward it.
            const auto& small_font = scene.font_small;
            // Names already placed, so a second one near the first moves to a free line instead of printing over it
            // ("to Rogue Encampmenten of Evil": the emulator play test).
            std::vector<std::array<int, 4>> placed;
            const auto clashes = [&](int l, int t, int r, int b) {
                for (const auto& p : placed) if (l < p[2] && r > p[0] && t < p[3] && b > p[1]) return true;
                return false;
            };
            for (const auto& label : g_map_labels) {
                int mx = int(std::lround((label.x - label.y) * 80 / float(kDivisor))) - scroll_x + 8;
                int my = int(std::lround((label.x + label.y) * 40 / float(kDivisor))) - scroll_y - 8;
                const bool off = mx < x0 + 4 || mx > x1 - 4 || my < y0 + 4 || my > y1 - 4;
                mx = std::clamp(mx, x0 + 4, x1 - 4);
                my = std::clamp(my, y0 + 4, y1 - 4);
                for (int d = -3; d <= 3; ++d)
                    for (int e = -(3 - std::abs(d)); e <= 3 - std::abs(d); ++e) canvas.put(mx + d, my + e, 230, 190, 80);
                const std::string words = off ? "to " + label.text : label.text;
                const int width = small_font.measure(words);
                const int tx = std::clamp(mx - width / 2, x0 + 1, x1 - width - 1);
                const int below = my + 6 + 10 <= y1 ? my + 5 : my - 15;
                int ty = below;
                for (int step = 0; step < 12; ++step) {
                    const int offset = (step + 1) / 2 * 11 * (step % 2 ? 1 : -1);
                    const int candidate = std::clamp(below + offset, y0 + 1, y1 - 11);
                    if (!clashes(tx - 2, candidate, tx + width + 2, candidate + 11)) { ty = candidate; break; }
                }
                placed.push_back({ tx - 2, ty, tx + width + 2, ty + 11 });
                small_font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, tx, ty, words, 240, 210, 120);
            }
            // The camp's people: a pale dot and the name, on the map only (one off it isn't shown), clear of the other names.
            for (const auto& person : g_map_people) {
                const int mx = int(std::lround((person.x - person.y) * 80 / float(kDivisor))) - scroll_x + 8;
                const int my = int(std::lround((person.x + person.y) * 40 / float(kDivisor))) - scroll_y - 8;
                if (mx < x0 + 2 || mx > x1 - 2 || my < y0 + 2 || my > y1 - 2) continue;
                if (person.waypoint)                                   // a waypoint: a bright square with a dark rim
                    for (int d = -3; d <= 3; ++d) for (int e = -3; e <= 3; ++e) {
                        const bool rim = std::abs(d) == 3 || std::abs(e) == 3;
                        canvas.put(mx + d, my + e, rim ? 20 : 120, rim ? 40 : 230, rim ? 60 : 255);
                    }
                else for (int d = -1; d <= 1; ++d) for (int e = -1; e <= 1; ++e) canvas.put(mx + d, my + e, 170, 220, 255);
                const int width = small_font.measure(person.text);
                const int tx = std::clamp(mx - width / 2, x0 + 1, x1 - width - 1);
                int ty = -1;
                for (int step = 0; step < 6; ++step) {
                    const int candidate = std::clamp(my - 13 + (step + 1) / 2 * 11 * (step % 2 ? 1 : -1), y0 + 1, y1 - 11);
                    if (!clashes(tx - 2, candidate, tx + width + 2, candidate + 11)) { ty = candidate; break; }
                }
                if (ty < 0) continue;                                // no free line near: the dot alone
                placed.push_back({ tx - 2, ty, tx + width + 2, ty + 11 });
                small_font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, tx, ty, person.text, 170, 220, 255);
            }
            mark(state.map_x, state.map_y, { 80, 120, 255 });
        }
    }

    // The top line: where you are, and your level.
    canvas.slot(3, 3, width - 3, 20);
    scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, 9, 4, std::string(state.level_name), 199, 179, 119);
    const auto level_text = std::format("Lv {}", lvl);
    scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, width - 9 - scene.font.measure(level_text), 4, level_text, 255, 255, 255);
}

int bottom_strip_hit(int x, int y) {
    if (x < 6 || y < 25 || x >= 6 + 3 * 103 || y >= 25 + 3 * 45) return -1;
    return (y - 25) / 45 * 3 + (x - 6) / 103;
}

int bottom_wheel_hit(int x, int y, int shown) {
    constexpr int kBarsTop = kBottomHeight - 2 - 29 - 3 - 24 - 2 - 8;
    const int centre_x = kBottomWidth / 2, centre_y = (22 + kBarsTop - 3) / 2 + 4;
    for (int index = 0; index < shown; ++index) {
        const float angle = (float(index) + .5f) * skillpad::kTau / float(shown);
        const int left = centre_x + int(std::lround(std::sin(angle) * 100.f)) - 16, top = centre_y - int(std::lround(std::cos(angle) * 46.f)) - 16;
        if (x >= left && x < left + 32 && y >= top && y < top + 32) return index;
    }
    return -1;
}

int bottom_belt_hit(int x, int y) {
    constexpr int cell = 29, left = 320 / 2 - cell * 2, top = 240 - 2 - cell;
    if (y < top || y >= top + cell || x < left || x >= left + 4 * cell) return -1;
    return (x - left) / cell;
}

int bottom_slot_hit(int x, int y) {
    constexpr int kPitch = 30, kIcon = 24;                       // as draw_bottom_hud lays the row
    const int left = kBottomWidth / 2 - (7 * kPitch - (kPitch - kIcon - 2)) / 2, top = kBottomHeight - 2 - 29 - 3 - kIcon - 2;
    if (y < top - 4 || y >= top + kIcon + 4 || x < left - 2 || x >= left + 7 * kPitch) return -1;
    return std::clamp((x - left + 2) / kPitch, 0, 6);
}

void draw_loading_note(std::vector<std::uint8_t>& framebuffer, const Scene& scene, std::uint32_t now_ms) {
    const int width = int(kScreenWidth), height = int(kScreenHeight);
    if (framebuffer.size() < std::size_t(width) * std::size_t(height) * 4) return;
    for (int y = 230; y < height; ++y) {
        std::uint8_t* pixel = framebuffer.data() + std::size_t(y) * std::size_t(width) * 4;
        for (int x = 0; x < width; ++x, pixel += 4) { pixel[0] >>= 2; pixel[1] >>= 2; pixel[2] >>= 2; }
    }
    const std::string_view words = "Loading";
    scene.font42.draw_tinted_baseline(framebuffer, kScreenWidth, kScreenHeight, scene.pal, (width - scene.font42.measure(words)) / 2, 380, words, 255, 255, 255);
    if (const std::string_view step = g_loading_step.load(std::memory_order_relaxed); !step.empty())
        scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, scene.pal, (width - scene.font.measure(step)) / 2, 452, step, 199, 179, 119);
    // The bar: a 400x20 trough and a bright piece that crosses it every two seconds.
    const int left = 200, top = 420, wide = 400, tall = 20, piece = 100;
    const int at = int(now_ms % 2000) * (wide + piece) / 2000 - piece;
    for (int y = top; y < top + tall; ++y) {
        std::uint8_t* pixel = framebuffer.data() + (std::size_t(y) * std::size_t(width) + left) * 4;
        const bool edge = y < top + 2 || y >= top + tall - 2;
        for (int x = 0; x < wide; ++x, pixel += 4) {
            const bool lit = !edge && x >= 2 && x < wide - 2 && x >= at && x < at + piece;
            const bool rim = edge || x < 2 || x >= wide - 2;
            pixel[0] = lit ? 220 : rim ? 120 : 20; pixel[1] = lit ? 170 : rim ? 95 : 14; pixel[2] = lit ? 80 : rim ? 60 : 10;
        }
    }
}

}  // namespace d2d::client
