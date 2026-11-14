// SPDX-License-Identifier: GPL-3.0-or-later
// A handheld's front menus on its second screen (320x240, touch): the 800x600 frame at two fifths
// could not be read. The top screen keeps the frame (the art); this page lists what the screen
// offers as large rows, a touch on one the click on its button in the frame.
// In a game the same page stands for the game menu and the death message (town/handheld_page.cpp):
// there a row carries what it is (`id`), a choice its value at the right, a slider its bar.
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
    // `value`: a choice's value, at the row's right. `steps` > 0: a slider of that many places, at `at`.
    // `id`: what the row is to the page's owner (the front menus have the click instead).
    struct Row {
        std::string text, detail; int x = 0, y = 0; bool small = false, on = false, dim = false; int left = 0, top = 0, right = 0, bottom = 0;
        std::string value; int steps = 0, at = 0, id = -1;
        bool picked = false;            // the screen's pick (the class chosen), where `on` is the cursor: gold words, a lighter ground
        // A slider's bar, and the place a touch at `touch_x` picks on it.
        [[nodiscard]] int bar_left() const { return left + (right - left) * 2 / 5; }
        [[nodiscard]] int bar_right() const { return right - 14; }
        [[nodiscard]] int place(int touch_x) const {
            if (steps < 2) return 0;
            const int span = std::max(1, bar_right() - bar_left());
            return std::clamp(((touch_x - bar_left()) * (steps - 1) + span / 2) / span, 0, steps - 1);
        }
    };
    bool active = false;
    std::string heading;
    std::array<std::uint8_t, 3> heading_ink{ 255, 232, 150 };
    std::vector<std::string> notes;     // lines of words under the heading (wrapped to the screen), above the rows
    bool notes_small = false;           // ... in font8, 13 rows a line (a class's description: three lines of it)
    std::array<std::uint8_t, 3> note_ink{ 214, 190, 130 };
    std::vector<Row> rows;
    std::vector<std::uint8_t> pixels;   // 320x240 RGBA, drawn by draw_menu_page
    void clear() { active = false; heading.clear(); notes.clear(); notes_small = false; rows.clear(); heading_ink = { 255, 232, 150 }; note_ink = { 214, 190, 130 }; }
    void add(std::string text, int x, int y, bool on = false, bool small = false, bool dim = false, std::string detail = {}) {
        Row row; row.text = std::move(text); row.detail = std::move(detail); row.x = x; row.y = y; row.on = on; row.small = small; row.dim = dim;
        rows.push_back(std::move(row));
    }
    // The row a touch at (x, y) of the screen is on; null for none (or a dim one).
    [[nodiscard]] const Row* hit(int x, int y) const {
        for (const auto& row : rows) if (!row.dim && x >= row.left && x < row.right && y >= row.top && y < row.bottom) return &row;
        return nullptr;
    }
    [[nodiscard]] const Row* by_id(int id) const {
        for (const auto& row : rows) if (row.id == id) return &row;
        return nullptr;
    }
};
inline MenuPage g_menu_page;

// The notes' lines as they fit the screen's width.
inline std::vector<std::string> menu_page_note_lines(const MenuPage& page, const Scene& scene) {
    std::vector<std::string> lines;
    for (const auto& note : page.notes) {
        std::string line;
        std::size_t at = 0;
        while (at < note.size()) {
            const std::size_t end = std::min(note.find(' ', at), note.size());
            const std::string word = note.substr(at, end - at);
            at = end + 1;
            if (word.empty()) continue;
            if (!line.empty() && (page.notes_small ? scene.font_small : scene.font).measure(line + " " + word) > 320 - 16) { lines.push_back(line); line.clear(); }
            line += (line.empty() ? "" : " ") + word;
        }
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

// Where each row goes on the screen (its left, top, right, bottom), without drawing it.
inline void layout_menu_page(MenuPage& page, const Scene& scene) {
    constexpr int kW = 320, kH = 240;
    int area_top = page.heading.empty() ? 4 : 30;
    area_top += int(menu_page_note_lines(page, scene).size()) * (page.notes_small ? 13 : 18) + (page.notes.empty() ? 0 : 6);
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
}

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
    // `align`: 0 the words centred on x, -1 starting at it, 1 ending at it.
    const auto put = [&](const d2d::font::Font& with, const std::string& words, int x, int centre_y, std::array<std::uint8_t, 3> c, int align = 0) {
        const int line = with.line_height(), width = with.measure(words);
        with.draw_tinted(page.pixels, kW, kH, pal, align == 0 ? x - width / 2 : align < 0 ? x : x - width, centre_y - line / 2 - (&with == &small ? line / 6 : line * 2 / 5), words, c[0], c[1], c[2]);
    };
    int notes_y = 4 + 9;
    if (!page.heading.empty()) {
        const auto& with = font.measure(page.heading) <= kW - 12 ? font : small;
        put(with, page.heading, kW / 2, 13, page.heading_ink);
        rect(8, 25, kW - 8, 26, { 78, 68, 52 });
        notes_y = 30 + 3 + 9;
    }
    if (page.notes_small) notes_y -= 2;
    for (const auto& line : menu_page_note_lines(page, scene)) { put(page.notes_small ? small : font, line, kW / 2, notes_y, page.note_ink); notes_y += page.notes_small ? 13 : 18; }
    layout_menu_page(page, scene);
    for (const auto& row : page.rows) {
        const std::array<std::uint8_t, 3> rim = row.on ? std::array<std::uint8_t, 3>{ 255, 220, 110 } : row.dim ? std::array<std::uint8_t, 3>{ 44, 40, 34 } : std::array<std::uint8_t, 3>{ 110, 94, 66 };
        const std::array<std::uint8_t, 3> fill = row.on ? std::array<std::uint8_t, 3>{ 66, 50, 24 } : row.picked ? std::array<std::uint8_t, 3>{ 50, 42, 26 } : std::array<std::uint8_t, 3>{ 30, 26, 21 };
        rect(row.left, row.top, row.right, row.bottom, rim);
        rect(row.left + (row.on ? 2 : 1), row.top + (row.on ? 2 : 1), row.right - (row.on ? 2 : 1), row.bottom - (row.on ? 2 : 1), fill);
        const std::array<std::uint8_t, 3> ink = row.dim ? std::array<std::uint8_t, 3>{ 90, 84, 74 } : row.on ? std::array<std::uint8_t, 3>{ 255, 255, 255 }
                                              : row.picked ? std::array<std::uint8_t, 3>{ 255, 232, 150 } : std::array<std::uint8_t, 3>{ 214, 190, 130 };
        const int centre_x = (row.left + row.right) / 2, centre_y = (row.top + row.bottom) / 2, room = row.right - row.left - 6;
        if (row.steps > 1) {                               // a slider: its name, then the bar with its knob
            const int bar_left = row.bar_left(), bar_right = row.bar_right();
            const auto& with = font.measure(row.text) <= bar_left - row.left - 14 ? font : small;
            put(with, row.text, row.left + 10, centre_y, ink, -1);
            const int knob = bar_left + (bar_right - bar_left) * std::clamp(row.at, 0, row.steps - 1) / (row.steps - 1);
            rect(bar_left - 1, centre_y - 4, bar_right + 1, centre_y + 4, { 110, 94, 66 });
            rect(bar_left, centre_y - 3, bar_right, centre_y + 3, { 16, 14, 12 });
            rect(bar_left, centre_y - 3, knob, centre_y + 3, { 190, 150, 60 });
            rect(knob - 5, centre_y - 10, knob + 5, centre_y + 10, row.on ? std::array<std::uint8_t, 3>{ 255, 255, 255 } : std::array<std::uint8_t, 3>{ 214, 190, 130 });
            rect(knob - 3, centre_y - 8, knob + 3, centre_y + 8, { 120, 92, 40 });
            continue;
        }
        if (!row.value.empty()) {                          // a choice: its name at the left, what it is set to at the right
            const bool fits = font.measure(row.text) + font.measure(row.value) <= room - 24;
            const auto& with = fits ? font : small;
            put(with, row.text, row.left + 10, centre_y, ink, -1);
            put(with, row.value, row.right - 10, centre_y, row.dim ? ink : std::array<std::uint8_t, 3>{ 255, 232, 150 }, 1);
            continue;
        }
        const auto& with = font.measure(row.text) <= room ? font : small;
        if (row.detail.empty() || row.bottom - row.top < 30) put(with, row.text, centre_x, centre_y, ink);
        else {
            put(with, row.text, centre_x, centre_y - 7, ink);
            put(small, row.detail, centre_x, centre_y + 9, { 170, 160, 140 });
        }
    }
}

}  // namespace d2d::client
