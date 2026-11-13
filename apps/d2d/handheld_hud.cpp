// SPDX-License-Identifier: GPL-3.0-or-later
#include "handheld_hud.hpp"

#include "common.hpp"
#include "panels.hpp"
#include "scene.hpp"

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
    const int width = kBottomWidth, height = kBottomHeight;
    // The stone the panel is cut from.
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int grain = ((x * 7 + y * 13) ^ (x * y >> 3)) & 7;
            canvas.put(x, y, std::uint8_t(26 + grain), std::uint8_t(23 + grain), std::uint8_t(20 + grain));
        }
    if (!state.stats) return;
    const auto& stats = *state.stats;

    // The globes at half size in the bottom corners (each source 2x2 averaged),
    // their numbers beside them.
    const auto& small = scene.font_small;
    auto centred = [&](const d2d::font::Font& font, int centre, int top, const std::string& text, std::array<std::uint8_t, 3> c) {
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, centre - font.measure(text) / 2, top, text, c[0], c[1], c[2]);
    };
    auto globe = [&](int frame, int x, int y, std::int64_t cur, std::int64_t max) {
        constexpr int kSize = 40;
        for (int row = -2; row < kSize + 2; ++row)                      // the socket: a dark disc with a lit rim
            for (int column = -2; column < kSize + 2; ++column) {
                const int dx = column * 2 - kSize + 1, dy = row * 2 - kSize + 1, reach = dx * dx + dy * dy;
                if (reach <= (kSize + 3) * (kSize + 3)) { const bool rim = reach > (kSize - 1) * (kSize - 1); canvas.put(x + column, y + row, rim ? 96 : 8, rim ? 82 : 6, rim ? 58 : 6); }
            }
        if (max <= 0 || frame >= int(scene.globes.frames_per_direction())) return;
        const auto& cel = scene.globes.frame(0, std::uint32_t(frame));
        const int source = int(cel.height);
        const int rows = int(std::clamp<std::int64_t>(cur * source / max, cur > 0 ? 2 : 0, source));
        for (int row = 0; row < kSize; ++row)
            for (int column = 0; column < kSize; ++column) {
                int red = 0, green = 0, blue = 0, count = 0;
                for (int sy = row * 2; sy < row * 2 + 2; ++sy)
                    for (int sx = column * 2; sx < column * 2 + 2; ++sx) {
                        if (sy < source - rows || sy >= source || sx >= int(cel.width)) continue;
                        const auto idx = cel.pixels[std::size_t(sy) * cel.width + std::size_t(sx)];
                        if (!idx) continue;
                        const auto colour = pal[idx];
                        red += colour.r; green += colour.g; blue += colour.b; ++count;
                    }
                if (count >= 2) canvas.put(x + column, y + row, std::uint8_t(red / count), std::uint8_t(green / count), std::uint8_t(blue / count));
            }
    };
    const bool poisoned = state.hud && state.hud->poisoned;
    const int globe_y = height - 46;
    globe(poisoned ? 2 : 0, 6, globe_y, stats.values[kLife], stats.values[kMaxLife]);
    globe(1, width - 46, globe_y, stats.values[kMana], stats.values[kMaxMana]);
    const auto life_text = std::format("{}", stats.values[kLife] >> 8), mana_text = std::format("{}", stats.values[kMana] >> 8);
    small.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, 50, height - 20, life_text, 255, 255, 255);
    small.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, width - 50 - small.measure(mana_text), height - 20, mana_text, 255, 255, 255);

    // Between them: the belt, a slot a D-pad direction.
    const int mid = width / 2, cell = 29;
    const int belt_left = mid - cell * 2, belt_top = height - 4 - cell;
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
    // Experience and stamina: two thin lines over the belt.
    auto bar = [&](int top, std::int64_t cur, std::int64_t max, std::array<std::uint8_t, 3> c) {
        const int left = belt_left, right = belt_left + cell * 4;
        canvas.rect(left, top, right, top + 3, { 8, 7, 6 });
        if (max > 0 && cur > 0) canvas.rect(left, top + 1, left + int(std::clamp<std::int64_t>(cur * (right - left) / max, 1, right - left)), top + 2, c);
    };
    const auto lvl = stats.get(kLevel);
    std::int64_t exp_num = 0, exp_den = 0;
    if (lvl >= 1 && std::size_t(lvl) < scene.exp_next.size()) {
        const auto next = scene.exp_next[std::size_t(lvl)], prev = scene.exp_next[std::size_t(lvl - 1)];
        if (next > prev) { exp_num = std::int64_t(stats.values[kExp]) > std::int64_t(prev) ? std::int64_t(stats.values[kExp]) - std::int64_t(prev) : 0; exp_den = std::int64_t(next) - std::int64_t(prev); }
    }
    bar(belt_top - 9, exp_num, exp_den, { 214, 176, 78 });
    bar(belt_top - 5, stats.values[kStamina], stats.values[kMaxStamina], { 150, 190, 110 });

    // The skill buttons: the trigger's, then the six slots' (the second set while its button is held).
    const int skill_top = belt_top - 12 - 48;
    {
        constexpr int kPitch = 45, kWide = 44;
        const auto& skills = g_bottom_skills;
        const int left = (width - (7 * kPitch - 1)) / 2;
        for (int i = 0; i < 7; ++i) {
            const int x = left + i * kPitch, id = i == 0 ? skills.main : skills.slots[std::size_t(i - 1)];
            const std::string& label = i == 0 ? skills.main_label : skills.labels[std::size_t(i - 1)];
            canvas.slot(x, skill_top, x + kWide, skill_top + 48);
            if (const auto* skill = id >= 0 ? scene.skills.get(id) : nullptr) {
                int column = -1;
                for (int cls = 0; cls < 7; ++cls) if (skill->cls == d2d::rules::kClassCode[std::size_t(cls)]) column = cls;
                const auto& spr = column >= 0 ? scene.skill_icons[std::size_t(column)] : scene.generic_skill_icons;
                if (skill->icon >= 0 && spr.frames_per_direction() > std::uint32_t(skill->icon)) {
                    const auto& cel = spr.frame(0, std::uint32_t(skill->icon));
                    const int crop = (int(cel.width) - kWide) / 2;          // the icon's 48 in the slot's 44
                    for (int row = 0; row < int(cel.height) && row < 48; ++row)
                        for (int column_x = 1; column_x < kWide - 1; ++column_x) {
                            const int source = column_x + crop;
                            if (source < 0 || source >= int(cel.width)) continue;
                            if (const auto idx = cel.pixels[std::size_t(row) * cel.width + std::size_t(source)]) {
                                const auto colour = pal[idx];
                                canvas.put(x + column_x, skill_top + row, colour.r, colour.g, colour.b);
                            }
                        }
                }
            } else if (id <= -3) {
                static constexpr std::array<const char*, 7> kActions{ "Use", "Items", "Inv", "Char", "Tree", "Map", "Quest" };
                const int action = -3 - id;
                if (action < int(kActions.size())) centred(small, x + kWide / 2, skill_top + 20, kActions[std::size_t(action)], { 199, 179, 119 });
            }
            if (!label.empty()) {                                         // the button, on a dark tab in the corner
                const int tab = small.measure(label) + 4;
                canvas.rect(x + 1, skill_top + 1, x + 1 + tab, skill_top + 1 + small.line_height(), { 0, 0, 0 });
                small.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x + 3, skill_top + 1, label, skills.alternate ? 255 : 199, skills.alternate ? 210 : 179, skills.alternate ? 90 : 119);
            }
        }
    }

    // The automap, always open here: the mini map's cels round the player.
    {
        const int left = 4, top = 24, right = width - 4, bottom = skill_top - 4;
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
    canvas.slot(4, 4, width - 4, 22);
    scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, 10, 6, std::string(state.level_name), 199, 179, 119);
    const auto level_text = std::format("Lv {}", lvl);
    scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, width - 10 - scene.font.measure(level_text), 6, level_text, 255, 255, 255);
}

}  // namespace d2d::client
