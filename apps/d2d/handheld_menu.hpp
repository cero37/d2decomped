// SPDX-License-Identifier: GPL-3.0-or-later
// A handheld's front menus on its second screen (320x240, touch): the 800x600 frame at two fifths
// could not be read. The top screen keeps the frame (the art); this page lists what the screen
// offers as large rows, a touch on one the click on its button in the frame.
#pragma once

#include "scene.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace d2d::client {

struct MenuPage {
    // A row: its words, and the click it stands for in the frame (x, y). `small`: one of the foot's
    // buttons. `on`: lit (the cursor is on its button, or it is the pick). `dim`: not to be had.
    struct Row { std::string text, detail; int x = 0, y = 0; bool small = false, on = false, dim = false; int left = 0, top = 0, right = 0, bottom = 0; };
    bool active = false;
    std::string heading;
    std::vector<Row> rows;
    std::vector<std::uint8_t> pixels;   // 320x240 RGBA, drawn by draw_menu_page
    void clear() { active = false; heading.clear(); rows.clear(); }
    void add(std::string text, int x, int y, bool on = false, bool small = false, bool dim = false, std::string detail = {}) {
        Row row; row.text = std::move(text); row.detail = std::move(detail); row.x = x; row.y = y; row.on = on; row.small = small; row.dim = dim;
        rows.push_back(std::move(row));
    }
    // The row a touch at (x, y) of the screen is on; null for none (or a dim one).
    [[nodiscard]] const Row* hit(int x, int y) const {
        for (const auto& row : rows) if (!row.dim && x >= row.left && x < row.right && y >= row.top && y < row.bottom) return &row;
        return nullptr;
    }
};
inline MenuPage g_menu_page;

inline void draw_menu_page(MenuPage& page, const Scene& scene) {
    constexpr int kW = 320, kH = 240;
    page.pixels.assign(std::size_t(kW) * kH * 4, 0);
    const auto rect = [&](int left, int top, int right, int bottom, std::array<std::uint8_t, 3> c) {
        for (int y = std::max(top, 0); y < std::min(bottom, kH); ++y)
            for (int x = std::max(left, 0); x < std::min(right, kW); ++x) {
                auto* pixel = &page.pixels[(std::size_t(y) * kW + std::size_t(x)) * 4];
                pixel[0] = c[0]; pixel[1] = c[1]; pixel[2] = c[2]; pixel[3] = 0xff;
            }
    };
    rect(0, 0, kW, kH, { 12, 10, 8 });
    const auto& pal = scene.pal;
    const auto& font = scene.font;
    const auto& small = scene.font_small;
    // The fonts' cells carry their glyphs low: font16's ink sits about two fifths of its height under the y given, font8's a sixth.
    const auto put = [&](const d2d::font::Font& with, const std::string& words, int centre_x, int centre_y, std::array<std::uint8_t, 3> c) {
        const int line = with.line_height();
        with.draw_tinted(page.pixels, kW, kH, pal, centre_x - with.measure(words) / 2, centre_y - line / 2 - (&with == &small ? line / 6 : line * 2 / 5), words, c[0], c[1], c[2]);
    };
    int area_top = 4;
    if (!page.heading.empty()) {
        const auto& with = font.measure(page.heading) <= kW - 12 ? font : small;
        put(with, page.heading, kW / 2, 13, { 255, 232, 150 });
        rect(8, 25, kW - 8, 26, { 78, 68, 52 });
        area_top = 30;
    }
    std::vector<MenuPage::Row*> big, foot;
    for (auto& row : page.rows) (row.small ? foot : big).push_back(&row);
    int area_bottom = kH - 4;
    if (!foot.empty()) {                                   // the foot: one line of buttons, side by side
        constexpr int kFootH = 34;
        const int count = int(foot.size()), gap = 4, width = (kW - 8 - gap * (count - 1)) / count;
        for (int index = 0; index < count; ++index) {
            auto& row = *foot[std::size_t(index)];
            row.left = 4 + index * (width + gap); row.right = row.left + width; row.top = kH - 4 - kFootH; row.bottom = kH - 4;
        }
        area_bottom = kH - 4 - kFootH - 4;
    }
    if (!big.empty()) {                                    // the rows: one column to five, two past that
        const int count = int(big.size()), columns = count > 5 ? 2 : 1, lines = (count + columns - 1) / columns;
        const int gap = 4, height = std::min(44, (area_bottom - area_top - gap * (lines - 1)) / lines);
        const int width = (kW - 8 - gap * (columns - 1)) / columns;
        const int top = area_top + std::max(0, (area_bottom - area_top - (height * lines + gap * (lines - 1))) / 2);
        for (int index = 0; index < count; ++index) {
            auto& row = *big[std::size_t(index)];
            const int column = index % columns, line = index / columns;
            row.left = 4 + column * (width + gap); row.right = row.left + width;
            row.top = top + line * (height + gap); row.bottom = row.top + height;
        }
    }
    for (const auto& row : page.rows) {
        const std::array<std::uint8_t, 3> rim = row.on ? std::array<std::uint8_t, 3>{ 255, 220, 110 } : row.dim ? std::array<std::uint8_t, 3>{ 44, 40, 34 } : std::array<std::uint8_t, 3>{ 110, 94, 66 };
        const std::array<std::uint8_t, 3> fill = row.on ? std::array<std::uint8_t, 3>{ 66, 50, 24 } : std::array<std::uint8_t, 3>{ 30, 26, 21 };
        rect(row.left, row.top, row.right, row.bottom, rim);
        rect(row.left + (row.on ? 2 : 1), row.top + (row.on ? 2 : 1), row.right - (row.on ? 2 : 1), row.bottom - (row.on ? 2 : 1), fill);
        const std::array<std::uint8_t, 3> ink = row.dim ? std::array<std::uint8_t, 3>{ 90, 84, 74 } : row.on ? std::array<std::uint8_t, 3>{ 255, 255, 255 } : std::array<std::uint8_t, 3>{ 214, 190, 130 };
        const int centre_x = (row.left + row.right) / 2, centre_y = (row.top + row.bottom) / 2, room = row.right - row.left - 6;
        const auto& with = font.measure(row.text) <= room ? font : small;
        if (row.detail.empty() || row.bottom - row.top < 30) put(with, row.text, centre_x, centre_y, ink);
        else {
            put(with, row.text, centre_x, centre_y - 7, ink);
            put(small, row.detail, centre_x, centre_y + 9, { 170, 160, 140 });
        }
    }
}

}  // namespace d2d::client
