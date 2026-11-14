// SPDX-License-Identifier: GPL-3.0-or-later
// A handheld's panels, made to fit its second screen (320x240, touch). The PC's panels are 320x432: half
// of one filled the screen and the other half took the top screen from the world. Here each panel is a
// page of its own, and the top screen stays the world's:
//   - the character sheet, in words, with a button beside each attribute while there are points to spend;
//   - the inventory: every body slot and the grid, pixel for pixel, moved together;
//   - the skill tree, drawn smaller beside its tabs, the points left and the skill the cursor is on;
//   - the waypoints, a row each.
// The frame still draws the PC's panel (off any screen) and takes the input: a touch on the page is the
// mouse at the frame's point for it, and the pad's cursor, which hops between the frame's buttons, shows
// where it is on the page (handheld_hud.hpp, BottomMap). Hover text is kept out of the frame and laid
// on the page, where it is made to fit (items.hpp, g_hover_capture).
#include "town.hpp"

#include "common.hpp"
#include "handheld_hud.hpp"
#include "handheld_menu.hpp"
#include "items.hpp"
#include "panels.hpp"
#include "scene.hpp"
#include "skill_tree_close.hpp"
#include "skilltree.hpp"

#include <d2s_items.hpp>
#include <font.hpp>
#include <rules.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace d2d::client {

namespace {

constexpr int kW = 320, kH = 240;
using Rgb = std::array<std::uint8_t, 3>;
constexpr Rgb kInk{ 214, 190, 130 }, kWhite{ 255, 255, 255 }, kGold{ 255, 232, 150 }, kDimInk{ 120, 110, 92 }, kRim{ 110, 94, 66 }, kLit{ 255, 220, 110 };

// The page being put together: 320x240 RGBA, copied to the frame's top-left when done.
struct Page {
    std::vector<std::uint8_t>& pixels;
    const Scene& scene;
    void rect(int left, int top, int right, int bottom, Rgb c) {
        for (int y = std::max(top, 0); y < std::min(bottom, kH); ++y)
            for (int x = std::max(left, 0); x < std::min(right, kW); ++x) {
                auto* pixel = &pixels[(std::size_t(y) * kW + std::size_t(x)) * 4];
                pixel[0] = c[0]; pixel[1] = c[1]; pixel[2] = c[2]; pixel[3] = 0xff;
            }
    }
    void box(int left, int top, int right, int bottom, bool lit, bool dim = false) {
        rect(left, top, right, bottom, lit ? kLit : dim ? Rgb{ 44, 40, 34 } : kRim);
        const int rim = lit ? 2 : 1;
        rect(left + rim, top + rim, right - rim, bottom - rim, lit ? Rgb{ 66, 50, 24 } : Rgb{ 30, 26, 21 });
    }
    // Words with their ink's middle on centre_y (the fonts' cells carry their glyphs low: draw_menu_page).
    // align: -1 starting at x, 0 centred on it, 1 ending at it.
    void text(const d2d::font::Font& font, const std::string& words, int x, int centre_y, Rgb c, int align = -1) {
        const int line = font.line_height(), width = font.measure(words);
        const bool small = &font == &scene.font_small;
        font.draw_tinted(pixels, kW, kH, scene.pal, align == 0 ? x - width / 2 : align < 0 ? x : x - width, centre_y - line / 2 - (small ? line / 6 : line * 2 / 5), words, c[0], c[1], c[2]);
    }
    // The font that fits `room`: font16, else font8.
    const d2d::font::Font& fit(const std::string& words, int room) const { return scene.font.measure(words) <= room ? scene.font : scene.font_small; }
    // A rect of the frame onto the page, at `scale` (1: pixel for pixel).
    void piece(const std::vector<std::uint8_t>& frame, const BottomMap::Piece& part) {
        const int out_w = int(float(part.src_w) * part.scale), out_h = int(float(part.src_h) * part.scale);
        for (int y = 0; y < out_h; ++y) {
            const int to_y = part.dst_y + y;
            if (to_y < 0 || to_y >= kH) continue;
            const float from_y = std::clamp((float(y) + .5f) / part.scale - .5f, 0.f, float(part.src_h - 1));
            const int y0 = int(from_y), y1 = std::min(part.src_h - 1, y0 + 1), weight_y = int((from_y - float(y0)) * 256.f);
            for (int x = 0; x < out_w; ++x) {
                const int to_x = part.dst_x + x;
                if (to_x < 0 || to_x >= kW) continue;
                const float from_x = std::clamp((float(x) + .5f) / part.scale - .5f, 0.f, float(part.src_w - 1));
                const int x0 = int(from_x), x1 = std::min(part.src_w - 1, x0 + 1), weight_x = int((from_x - float(x0)) * 256.f);
                const auto at = [&](int source_x, int source_y) { return frame.data() + (std::size_t(part.src_y + source_y) * kScreenWidth + std::size_t(part.src_x + source_x)) * 4; };
                auto* out = &pixels[(std::size_t(to_y) * kW + std::size_t(to_x)) * 4];
                if (part.scale == 1.f) { std::copy_n(at(x0, y0), 3, out); out[3] = 0xff; continue; }
                const auto *a = at(x0, y0), *b = at(x1, y0), *c = at(x0, y1), *d = at(x1, y1);
                for (int channel = 0; channel < 3; ++channel) {
                    const int upper = a[channel] * (256 - weight_x) + b[channel] * weight_x, lower = c[channel] * (256 - weight_x) + d[channel] * weight_x;
                    out[channel] = std::uint8_t((upper * (256 - weight_y) + lower * weight_y) >> 16);
                }
                out[3] = 0xff;
            }
        }
    }
};

BottomMap::Piece part(int src_x, int src_y, int src_w, int src_h, int dst_x, int dst_y, float scale = 1.f, bool look_first = false) {
    BottomMap::Piece piece;
    piece.src_x = src_x; piece.src_y = src_y; piece.src_w = src_w; piece.src_h = src_h; piece.dst_x = dst_x; piece.dst_y = dst_y; piece.scale = scale; piece.look_first = look_first;
    return piece;
}

}  // namespace

// Hover text on a handheld's panel: the lines in a box inside (left, top, width, height) of the frame, clear
// of the cursor at (avoid_x, avoid_y) where it can be. Font16 while that fits, font8 past it; lines too many
// even for that are left off the end.
void draw_handheld_hover(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<TextLine>& lines,
                         int left, int top, int width, int height, int avoid_x, int avoid_y) {
    if (lines.empty() || width < 16 || height < 16) return;
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    const auto widest = [&](const d2d::font::Font& font) { int most = 0; for (const auto& line : lines) most = std::max(most, font.measure(line.text)); return most; };
    const bool big = widest(scene.font) <= width - 8 && int(lines.size()) * 16 <= height - 6;
    const auto& font = big ? scene.font : scene.font_small;
    const int pitch = big ? 16 : 12, lift = big ? 0 : 2;
    const int shown = std::min(int(lines.size()), (height - 6) / pitch);
    const int box_w = std::min(width, widest(font) + 8), box_h = shown * pitch + 6;
    // Above the cursor if there is room, else below it, else wherever it is least in the way.
    int box_x = std::clamp(avoid_x - box_w / 2, left, left + width - box_w);
    int box_y = top + (height - box_h) / 2;
    if (avoid_y >= top && avoid_y < top + height) {
        if (avoid_y - 18 - box_h >= top) box_y = avoid_y - 18 - box_h;
        else if (avoid_y + 20 + box_h <= top + height) box_y = avoid_y + 20;
        else {
            box_y = avoid_y - top > height / 2 ? top : top + height - box_h;
            if (avoid_x - 16 - box_w >= left) box_x = avoid_x - 16 - box_w;                 // beside it instead
            else if (avoid_x + 24 + box_w <= left + width) box_x = avoid_x + 24;
        }
    }
    for (int y = box_y; y < box_y + box_h; ++y)
        for (int x = box_x; x < box_x + box_w; ++x) {
            if (x < 0 || y < 0 || x >= int(kScreenWidth) || y >= int(kScreenHeight)) continue;
            auto* pixel = &framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4];
            const bool edge = x == box_x || y == box_y || x == box_x + box_w - 1 || y == box_y + box_h - 1;
            pixel[0] = edge ? 110 : std::uint8_t(pixel[0] / 8); pixel[1] = edge ? 94 : std::uint8_t(pixel[1] / 8); pixel[2] = edge ? 66 : std::uint8_t(pixel[2] / 8);
        }
    int y = box_y + 3 - lift;
    for (int index = 0; index < shown; ++index) {
        const auto& line = lines[std::size_t(index)];
        const int x = box_x + (box_w - font.measure(line.text)) / 2;
        const std::string head = line.text.substr(0, line.split);
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x, y, head, line.rgb[0], line.rgb[1], line.rgb[2]);
        if (line.split < line.text.size())
            font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x + font.measure(head), y, line.text.substr(line.split), line.tail[0], line.tail[1], line.tail[2]);
        y += pitch;
    }
}

// The open panel as a page of the second screen, laid in the frame's top-left 320x240, with g_bottom_map
// for its touches and its cursor. False (and nothing drawn) when what is open has no page of its own:
// a store, the stash, the cube, a trade, the quest log, the party, the hireling.
auto Town::handheld_fit_panel(const Mouse& mouse) -> bool {
        g_bottom_map.clear();
        if (!scene || stash_open || cube_open || store.npc >= 0 || gold_box || quest_log.open || party.open || hireling.open
            || (net_game && net_game->trade.state != 0) || identify_with >= 0 || use_item.open()) return false;
        enum Kind { kNoPage, kWaypoints, kTree, kInventory, kCharacter };
        const Kind kind = waypoint.open ? kWaypoints
                        : inv_open && char_open ? (mouse.x < int(kScreenWidth) / 2 ? kCharacter : kInventory)
                        : tree_open && char_open ? (mouse.x < int(kScreenWidth) / 2 ? kCharacter : kTree)
                        : tree_open ? kTree : inv_open ? kInventory : char_open ? kCharacter : kNoPage;
        if (kind == kNoPage) return false;
        static std::vector<std::uint8_t> pixels;
        pixels.assign(std::size_t(kW) * kH * 4, 0);
        Page page{ pixels, *scene };
        page.rect(0, 0, kW, kH, { 12, 10, 8 });
        auto& map = g_bottom_map;
        map.on = true;
        const auto& font = scene->font;
        const auto& small = scene->font_small;
        const int cls = std::max(character.character_class, 0);
        const auto& stats = character.stats;
        // A button of the page's own: a touch on it is a click at (x, y) of the frame. Lit while the cursor is there.
        const auto button = [&](int left, int top, int right, int bottom, const std::string& words, int x, int y, bool lit, bool dim = false) {
            page.box(left, top, right, bottom, lit, dim);
            page.text(page.fit(words, right - left - 6), words, (left + right) / 2, (top + bottom) / 2, dim ? kDimInk : lit ? kWhite : kInk, 0);
            if (!dim) map.spots.push_back({ left, top, right, bottom, x, y });
        };
        const auto over = [&](int left, int top, int width, int height) { return mouse.x >= left && mouse.x < left + width && mouse.y >= top && mouse.y < top + height; };

        if (kind == kCharacter) {
            namespace d2s = d2d::d2s;
            const auto& panel = character.panel;
            const auto pts = stats.get(d2s::kStatPts);
            // The head: the name, then level and class; experience under them.
            page.text(page.fit(character.name, 150), character.name, 6, 11, kGold);
            const std::string rank = std::format("Level {} {}", stats.get(d2s::kLevel), kClassKey[kSaveClassToUi[cls]]);
            page.text(page.fit(rank, 160), rank, kW - 6, 11, kWhite, 1);
            page.rect(4, 22, kW - 4, 23, { 78, 68, 52 });
            const std::string exp = panel.next >= 0 ? std::format("Experience {}   Next level {}", std::int64_t(stats.values[d2s::kExp]), panel.next)
                                                    : std::format("Experience {}", std::int64_t(stats.values[d2s::kExp]));
            page.text(small, exp, kW / 2, 31, kInk, 0);
            // The attributes, each with its button while there are points to spend.
            static constexpr std::array<const char*, 4> kNames = { "Strength", "Dexterity", "Vitality", "Energy" };
            for (int index = 0; index < 4; ++index) {
                const auto& stat_button = kStatButtons[std::size_t(index)];
                const int top = 40 + index * 34, stat = stat_button.stat;
                const auto bonus = panel.bonus[std::size_t(stat)];
                page.box(4, top, 156, top + 31, false);
                page.text(font, kNames[std::size_t(index)], 10, top + 15, kInk);
                page.text(font, std::to_string(stats.get(stat) + bonus), pts > 0 ? 118 : 148, top + 15, bonus > 0 ? Rgb{ 105, 105, 255 } : bonus < 0 ? Rgb{ 255, 77, 77 } : kWhite, 1);
                if (pts > 0) {
                    const int x = kCharPanelX + stat_button.x + 20, y = kCharPanelY + stat_button.y - 11;
                    const bool lit = stat_button_at(mouse.x, mouse.y) == index;
                    page.rect(122, top + 2, 154, top + 29, stat_pressed == index ? kWhite : lit ? kLit : Rgb{ 150, 40, 30 });
                    page.rect(124, top + 4, 152, top + 27, stat_pressed == index ? Rgb{ 120, 30, 20 } : Rgb{ 190, 50, 36 });
                    page.rect(131, top + 14, 145, top + 17, kWhite);          // the plus
                    page.rect(136, top + 9, 139, top + 22, kWhite);
                    map.spots.push_back({ 108, top, 160, top + 31, x, y });
                }
            }
            page.box(4, 178, 156, 204, pts > 0);
            page.text(page.fit("Points to spend", 108), "Points to spend", 10, 191, pts > 0 ? kWhite : kDimInk);
            page.text(font, std::to_string(pts), 148, 191, pts > 0 ? kGold : kDimInk, 1);
            // What they come to: the two hands' attacks, defense, the three bars, the resistances.
            int y = 45;
            const auto line = [&](const std::string& name, const std::string& value, Rgb c = kWhite) {
                page.text(small, name, 166, y, kInk);
                page.text(small, value, kW - 6, y, c, 1);
                y += 14;
            };
            static constexpr std::array<Rgb, 5> kState{ { { 255, 255, 255 }, { 255, 77, 77 }, { 0, 255, 0 }, { 105, 105, 255 }, { 199, 179, 119 } } };
            for (std::size_t block = 0; block < 2; ++block) {
                const auto& attack = panel.attack[block];
                if (!scene->skills.get(attack.skill)) continue;
                const char* hand = block == 0 ? "Left" : "Right";
                if (attack.damage) line(std::format("{} damage", hand), std::format("{}-{}", attack.min, std::max(attack.max, attack.min + 1)), kState[std::size_t(std::clamp(attack.damage_colour, 0, 4))]);
                if (attack.attack_rating != 0) line(std::format("{} attack rating", hand), std::to_string(attack.attack_rating), kState[std::size_t(std::clamp(attack.ar_colour, 0, 4))]);
            }
            const auto state = [&](std::uint8_t bit) { return (panel.red & bit) ? kState[1] : (panel.blue & bit) ? kState[3] : kWhite; };
            line("Defense", std::to_string(panel.defense), state(d2d::rules::kPanelDefense));
            line("Life", std::format("{} / {}", stats.fixed(d2s::kLife), stats.fixed(d2s::kMaxLife)));
            line("Mana", std::format("{} / {}", stats.fixed(d2s::kMana), stats.fixed(d2s::kMaxMana)));
            line("Stamina", std::format("{} / {}", stats.fixed(d2s::kStamina), stats.fixed(d2s::kMaxStamina)));
            static constexpr std::array<const char*, 4> kResist = { "Fire resistance", "Cold resistance", "Lightning resistance", "Poison resistance" };
            static constexpr std::array<std::uint8_t, 4> kResistBit = { d2d::rules::kPanelFire, d2d::rules::kPanelCold, d2d::rules::kPanelLightning, d2d::rules::kPanelPoison };
            for (std::size_t index = 0; index < 4; ++index) {
                const auto value = panel.res[index];
                Rgb c = state(kResistBit[index]);
                if (value >= panel.res_cap[index] && c != kState[3]) c = kState[4];
                else if (value < 0) c = kState[1];
                line(kResist[index], std::to_string(value), c);
            }
            const int close_x = kCharPanelX + 0x80 + 17, close_y = kCharPanelY + 420 - 17;
            page.text(small, pts > 0 ? "Touch +, or D-pad and A" : "", 6, 222, kDimInk);
            button(236, 214, 316, 237, "Close", close_x, close_y, over(close_x - 17, close_y - 17, 34, 34));
        } else if (kind == kInventory) {
            namespace body = d2d::d2s::body_location;
            const auto& lay = scene->inv_layout[std::size_t(cls)];
            const auto slot = [&](int which) { return lay.slots[std::size_t(which)]; };
            // The body's slots in five columns, each as wide as its widest and stacked from the top; the grid under them.
            const bool left_first = slot(body::kRightArm)[0] <= slot(body::kLeftArm)[0];
            const std::array<std::vector<std::vector<int>>, 5> columns{ {
                { { left_first ? body::kRightArm : body::kLeftArm } },
                { { body::kTorso }, { body::kBelt } },
                { { body::kHead }, { body::kGloves } },
                { { left_first ? body::kLeftArm : body::kRightArm } },
                { { body::kFeet }, { body::kRightRing, body::kLeftRing }, { body::kNeck } },
            } };
            constexpr int kEdge = 1;                       // the slot's rim in the panel's art
            std::array<int, 5> widths{};
            int total = 0;
            for (std::size_t column = 0; column < columns.size(); ++column) {
                for (const auto& row : columns[column]) {
                    int row_w = 0;
                    for (const int which : row) row_w += slot(which)[2] + 2 * kEdge;
                    widths[column] = std::max(widths[column], row_w);
                }
                total += widths[column];
            }
            const int gap = std::max(0, (kW - total) / 6);
            int x = (kW - total - gap * 4) / 2;
            for (std::size_t column = 0; column < columns.size(); ++column) {
                int y = 1;
                for (const auto& row : columns[column]) {
                    int row_w = 0, row_h = 0;
                    for (const int which : row) { row_w += slot(which)[2] + 2 * kEdge; row_h = std::max(row_h, slot(which)[3] + 2 * kEdge); }
                    int at = x + (widths[column] - row_w) / 2;
                    for (const int which : row) {
                        const auto box = slot(which);
                        map.pieces.push_back(part(box[0] - kEdge, box[1] - kEdge, box[2] + 2 * kEdge, box[3] + 2 * kEdge, at, y, 1.f, true));
                        at += box[2] + 2 * kEdge;
                    }
                    y += row_h;
                }
                x += widths[column] + gap;
            }
            const int grid_w = lay.cols * lay.box_w, grid_h = lay.rows * lay.box_h;
            map.pieces.push_back(part(lay.grid_x, lay.grid_y, grid_w, grid_h, (kW - grid_w) / 2, kH - grid_h - 1, 1.f, true));
            for (const auto& piece : map.pieces) page.piece(ui_buffer, piece);
            // Between them: the gold carried, and how to leave.
            const int line_y = kH - grid_h - 1 - 6;
            page.text(small, std::format("Gold {}", stats.get(d2d::d2s::kGold)), (kW - grid_w) / 2, line_y, kGold);
            page.text(small, "B: close", (kW + grid_w) / 2, line_y, kDimInk, 1);
        } else if (kind == kTree) {
            // The tree's own art (icons, the lines between them) at a size that shows all six rows; its tabs in words beside it.
            constexpr int kSrcX = kTreeR - 0x131 - 8, kSrcY = kTreeB - 0x1a2 - 0x30 - 6, kSrcW = 0x131 - 0xa7 + 0x30 + 22, kSrcH = 0x1a2 - 0x4d + 0x30 + 18;
            const float scale = std::min(float(kH - 4) / float(kSrcH), 1.f);
            map.pieces.push_back(part(kSrcX, kSrcY, kSrcW, kSrcH, 2, 2, scale, true));
            page.piece(ui_buffer, map.pieces.back());
            const int tree_w = int(float(kSrcW) * scale);
            const auto& list = scene->rules.class_skills[std::size_t(cls)];
            const auto& ids = scene->skills.class_ids[std::size_t(cls)];
            const auto env = tip_env();
            const auto pts = stats.get(d2d::d2s::kSkillPts);
            const int clvl = int(stats.get(d2d::d2s::kLevel));
            int hover = -1;
            for (std::size_t index = 0; index < list.size(); ++index) {           // each skill's level, in a size that reads
                if (list[index].page != tree_tab) continue;
                const auto [icon_x, icon_y] = skill_icon_at(list[index]);
                if (icon_x <= -1000) continue;
                if (over(icon_x + 1, icon_y - 0x30 + 1, 0x2f, 0x2f)) hover = int(index);
                int at_x = 0, at_y = 0;
                if (!map.to_page(icon_x + 0x2f, icon_y - 1, at_x, at_y)) continue;
                const int base = int(stats.skills[index]), level = env.level && index < ids.size() ? env.level(ids[index]) : base;
                if (level != 0) page.rect(at_x - 1, at_y - 6, at_x + (level > 9 ? 15 : 9), at_y + 7, { 12, 10, 8 });
                if (level != 0) page.text(small, std::to_string(level), at_x + 1, at_y + 1, level > base ? Rgb{ 105, 105, 255 } : level < base ? Rgb{ 255, 77, 77 } : kWhite);
                if (hover == int(index)) {                                        // the skill the cursor is on: a rim round its icon
                    int left = 0, top = 0;
                    if (map.to_page(icon_x, icon_y - 0x30, left, top)) {
                        const int size = int(48.f * scale);
                        page.rect(left - 1, top - 1, left + size + 1, top, kLit); page.rect(left - 1, top + size, left + size + 1, top + size + 1, kLit);
                        page.rect(left - 1, top, left, top + size, kLit); page.rect(left + size, top, left + size + 1, top + size, kLit);
                    }
                }
            }
            const int side = tree_w + 8, side_w = kW - side - 4;
            const std::string left_words = std::format("Skill points: {}", pts);
            page.text(page.fit(left_words, side_w), left_words, side + side_w / 2, 12, pts > 0 ? kGold : kInk, 0);
            // The three tabs, top to bottom as the panel has them (tab 3 the top one).
            for (int band = 0; band < 3; ++band) {
                const int tab = 3 - band, frame_top = kTreeB - (band == 0 ? 0x174 : band == 1 ? 0x108 : 0x9c);
                std::string words;
                for (const auto& label : kTreeTabs[std::size_t(cls)])
                    if (label.y >= (band == 0 ? 0 : band == 1 ? 270 : 380) && label.y < (band == 0 ? 270 : band == 1 ? 380 : 9999))
                        if (const auto found = lookup_string(*scene, label.id)) words += (words.empty() ? "" : " ") + u16_to_latin1(*found);
                const int top = 26 + band * 34;
                button(side, top, kW - 4, top + 31, words, kTreeR - 0x2c, frame_top + 0x35, tab == tree_tab);
            }
            // The skill under the cursor: its name and level here, the rest in its hover text.
            page.box(side, 130, kW - 4, 208, false);
            if (hover >= 0 && std::size_t(hover) < ids.size()) {
                const auto* skill = scene->skills.get(ids[std::size_t(hover)]);
                const std::string name = skill ? skill->name : std::string();
                const int base = int(stats.skills[std::size_t(hover)]);
                const bool can = pts > 0 && d2d::rules::can_learn(scene->rules, cls, hover, stats.skills, clvl);
                page.text(page.fit(name, side_w - 8), name, side + side_w / 2, 144, kWhite, 0);
                page.text(small, std::format("Level {}", base), side + side_w / 2, 162, kInk, 0);
                page.text(small, can ? "Touch it again, or press A," : pts > 0 ? "Cannot be learned yet" : "No skill points to spend", side + side_w / 2, 180, can ? kGold : kDimInk, 0);
                if (can) page.text(small, "to put a point in it", side + side_w / 2, 194, kGold, 0);
            } else {
                page.text(small, "Touch a skill to read it,", side + side_w / 2, 160, kDimInk, 0);
                page.text(small, "again to put a point in it", side + side_w / 2, 176, kDimInk, 0);
            }
            const auto close = skill_tree_close(character.character_class, tree_tab);
            button(kW - 84, 212, kW - 4, 237, "Close", close.left + 17, close.bottom - 17, close.contains(mouse.x, mouse.y));
        } else {                                               // the waypoints: a row each, the acts along the foot
            MenuPage list;
            list.active = true;
            const int acts = character.expansion ? 5 : 4, act_w = character.expansion ? 64 : 80;
            list.heading = std::format("Waypoints - Act {}", waypoint.tab + 1);
            const auto& rows = scene->waypoint_levels[std::size_t(waypoint.tab)];
            const int difficulty = character.header.active_difficulty();
            for (std::size_t index = 0; index < rows.size() && index < 9; ++index) {
                const bool active = character.header.waypoint(difficulty, rows[index].waypoint), here = level && rows[index].level == level->id;
                const auto name = lookup_string(*scene, std::string_view(rows[index].name));
                const int x = kCharPanelX + 17 + 140, y = 60 + kWpHitTop[index] + 15;
                list.add(name ? u16_to_latin1(*name) : rows[index].name, x, y, over(kCharPanelX + 17, 60 + kWpHitTop[index], 280, 30), false, !active, here ? "You are here" : "");
            }
            static constexpr std::array<const char*, 5> kActs = { "I", "II", "III", "IV", "V" };
            for (int act = 0; act < acts; ++act)
                list.add(std::string("Act ") + kActs[std::size_t(act)], kCharPanelX + act * act_w + act_w / 2, 75, act == waypoint.tab, true, !waypoint_act_open(character.header, act));
            list.add("Close", kCharPanelX + 0x111 + 0x12, 60 + 0x183 + 0x11, over(kCharPanelX + 0x111, 60 + 0x183, 0x24, 0x22), true);
            draw_menu_page(list, *scene);
            pixels = list.pixels;
            for (const auto& row : list.rows)
                if (!row.dim) map.spots.push_back({ row.left, row.top, row.right, row.bottom, row.x, row.y });
        }
        for (int y = 0; y < kH; ++y)
            std::copy_n(pixels.begin() + std::ptrdiff_t(std::size_t(y) * kW * 4), kW * 4, ui_buffer.begin() + std::ptrdiff_t(std::size_t(y) * kScreenWidth * 4));
        return true;
    }

}  // namespace d2d::client
