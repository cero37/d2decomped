// SPDX-License-Identifier: GPL-3.0-or-later
// A handheld's panels, made to fit its second screen (320x240, touch). The PC's panels are 320x432: half
// of one filled the screen and the other half took the top screen from the world. Here each panel is a
// page of its own, and the top screen stays the world's:
//   - the character sheet, in words, with a button beside each attribute while there are points to spend;
//   - the inventory: every body slot and the grid, pixel for pixel, moved together;
//   - the skill tree, drawn smaller beside its tabs, the points left and the skill the cursor is on;
//   - the waypoints, a row each;
//   - a store's stock at four fifths beside its tabs and buttons, the stash's cells, the cube over the
//     inventory's grid, the quest log's tabs, quests and words, the gold box with large buttons.
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
#include "store.hpp"

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

auto Town::stash_move_at(int mouse_x, int mouse_y) const -> StashMove {
    namespace d2s = d2d::d2s;
    StashMove move;
    if (!scene || !stash_open || cube_open || held) return move;
    const auto& bag = scene->inv_layout[std::size_t(std::max(character.character_class, 0))];
    const auto& stash = scene->stash_layout[character.expansion ? 1 : 0];
    for (const auto& carried : character.items) {
        if (carried.location != d2s::item_location::kStored) continue;
        const bool in_bag = carried.panel == d2s::item_panel::kInventory && inv_open, in_stash = carried.panel == d2s::item_panel::kStash;
        if (!in_bag && !in_stash) continue;
        const auto rect = grid_rect(*scene, in_bag ? bag : stash, carried);
        if (mouse_x < rect[0] || mouse_x >= rect[0] + rect[2] || mouse_y < rect[1] || mouse_y >= rect[1] + rect[3]) continue;
        move.item = &carried;
        move.to_stash = in_bag;
        move.from_x = rect[0] + rect[2] / 2; move.from_y = rect[1] + rect[3] / 2;
        break;
    }
    if (!move.item || (!move.to_stash && !inv_open)) { move.item = nullptr; return move; }
    const auto& to = move.to_stash ? stash : bag;
    const int panel = move.to_stash ? d2s::item_panel::kStash : d2s::item_panel::kInventory;
    std::vector<const d2s::Item*> placed;
    for (const auto& carried : character.items)
        if (carried.location == d2s::item_location::kStored && carried.panel == panel) placed.push_back(&carried);
    const auto [width, height] = d2d::rules::item_size(scene->rules, move.item->code);
    const auto [column, row] = d2d::rules::free_spot(scene->rules, placed, to.cols, to.rows, width, height, !move.to_stash);   // the stash from its top left
    if (column >= 0) {
        move.to_x = to.grid_x + column * to.box_w + width * to.box_w / 2;
        move.to_y = to.grid_y + row * to.box_h + height * to.box_h / 2;
    }
    return move;
}

auto Town::handheld_stash_move(int mouse_x, int mouse_y) -> bool {
    if (!g_auto_clicks.empty()) return false;
    const auto move = stash_move_at(mouse_x, mouse_y);
    if (!move.item) return false;
    if (move.to_x < 0) { d2d::log::info("no room in the {} for {}", move.to_stash ? "stash" : "bag", move.item->code); return false; }
    g_auto_clicks = { { move.from_x, move.from_y }, { move.to_x, move.to_y, true, true }, { mouse_x, mouse_y, false } };
    return true;
}

auto Town::handheld_sell(int mouse_x, int mouse_y) -> bool {
    if (store.npc < 0 || held || !scene) return false;
    const d2d::d2s::Item* under = bag_at(mouse_x, mouse_y);
    if (!under) return false;
    d2d::log::info("handheld: selling {} (id {}) to {} for {} gold", under->code, under->id, store.npc_id,
                   d2d::rules::item_price(scene->rules, *under, store.npc_id, true, store.header));
    send(cmd::Sell{ under->id });
    return true;
}

auto Town::handheld_return_held() -> void {
    namespace d2s = d2d::d2s;
    if (!scene || !held || !g_auto_clicks.empty()) return;
    const auto& bag = scene->inv_layout[std::size_t(std::max(character.character_class, 0))];
    std::vector<const d2s::Item*> placed;
    for (const auto& carried : character.items)
        if (carried.location == d2s::item_location::kStored && carried.panel == d2s::item_panel::kInventory) placed.push_back(&carried);
    const auto [width, height] = d2d::rules::item_size(scene->rules, held->code);
    const auto [column, row] = d2d::rules::free_spot(scene->rules, placed, bag.cols, bag.rows, width, height, true);
    inv_open = true;
    sync_panel_cam();
    if (column < 0) { d2d::log::info("the bag has no room for what's on the cursor: left open"); return; }
    g_auto_clicks = { { bag.grid_x + column * bag.box_w + width * bag.box_w / 2, bag.grid_y + row * bag.box_h + height * bag.box_h / 2 } };
    close_after_return = true;
    d2d::log::info("what was on the cursor goes back into the bag");
}

// The open panel as a page of the second screen, laid in the frame's top-left 320x240, with g_bottom_map
// for its touches and its cursor. False (and nothing drawn) when what is open has no page of its own:
// a trade with another player, the party, the hireling.
auto Town::handheld_fit_panel(const Mouse& mouse) -> bool {
        g_bottom_map.clear();
        handheld_line_y = 0;
        handheld_hover_w = 320;
        if (!scene) return false;
        enum Kind { kNoPage, kWaypoints, kTree, kInventory, kCharacter, kGoldBox, kStore, kStash, kCube, kQuests, kSpeech, kNpcMenu, kHireling };
        bool on_left = mouse.x < int(kScreenWidth) / 2;              // two panels up: the page of the side the cursor is on
        // A store, the stash or the hireling just opened beside the bag: its page first, wherever the cursor was.
        {
            const auto& stash = scene->stash_layout[character.expansion ? 1 : 0];
            const auto& torso = scene->merc_layout.slots[std::size_t(d2d::d2s::body_location::kTorso)];
            const int beside = store.npc >= 0 ? 1 : stash_open && !cube_open ? 2 : hireling.open && view.merc_sheet ? 3 : 0;
            if (beside != std::exchange(handheld_beside, beside) && beside != 0) {
                if (!on_left) cursor_move = beside == 1 ? std::pair{ 96 + 14, 123 + 14 } : beside == 2 ? std::pair{ stash.grid_x + stash.box_w / 2, stash.grid_y + stash.box_h / 2 }
                                                                                                    : std::pair{ torso[0] + torso[2] / 2, torso[1] + torso[3] / 2 };
                handheld_left = true;
            }
            // Beside the bag the page is the side the cursor was last on a cell or a slot of: a button of one side's page
            // may stand for a point on the other side of the frame (Sell, from the bag's page) and must not turn the page.
            if (beside != 0) {
                const auto& bag = scene->inv_layout[std::size_t(std::max(character.character_class, 0))];
                const auto in = [&](int left, int top, int width, int height) { return mouse.x >= left && mouse.x < left + width && mouse.y >= top && mouse.y < top + height; };
                bool in_bag = in(bag.grid_x, bag.grid_y, bag.cols * bag.box_w, bag.rows * bag.box_h);
                for (const auto& box : bag.slots) in_bag = in_bag || (box[2] > 0 && in(box[0], box[1], box[2], box[3]));
                bool in_other = beside == 1 ? in(96, 123, 290, 290) : beside == 2 ? in(stash.grid_x, stash.grid_y, stash.cols * stash.box_w, stash.rows * stash.box_h) : false;
                if (beside == 3) for (const auto& box : scene->merc_layout.slots) in_other = in_other || (box[2] > 0 && in(box[0], box[1], box[2], box[3]));
                // The game's own clicks (Put in stash, Take out) cross over and back: the page stays where it is.
                if (g_auto_clicks.empty()) { if (in_bag) handheld_left = false; else if (in_other) handheld_left = true; }
                on_left = handheld_left;
            }
        }
        const bool words = speech.npc >= 0, choices = npc_menu.npc >= 0 && !npc_menu.list && !npc_menu.lines.empty();
        if (!words && !choices && (party.open || (net_game && net_game->trade.state != 0))) return false;
        const bool merc = hireling.open && view.merc_sheet;
        const Kind kind = words ? kSpeech : choices ? kNpcMenu
                        : gold_box ? kGoldBox
                        : store.npc >= 0 ? (on_left || !inv_open ? kStore : kInventory)
                        : cube_open ? kCube
                        : stash_open ? (on_left || !inv_open ? kStash : kInventory)
                        : merc ? (on_left || !inv_open ? kHireling : kInventory)
                        : waypoint.open ? kWaypoints
                        : quest_log.open ? (on_left || !(inv_open || tree_open) ? kQuests : tree_open ? kTree : kInventory)
                        : inv_open && char_open ? (mouse.x < int(kScreenWidth) / 2 ? kCharacter : kInventory)
                        : tree_open && char_open ? (mouse.x < int(kScreenWidth) / 2 ? kCharacter : kTree)
                        : tree_open ? kTree : inv_open ? kInventory : char_open ? kCharacter : kNoPage;
        if (kind == kNoPage) return false;
        { static int last_kind = -1; if (int(kind) != last_kind) { d2d::log::info("handheld page: {} (cursor {}, {})", int(kind), mouse.x, mouse.y); last_kind = int(kind); } }
        if (kind == kSpeech || kind == kNpcMenu) handheld_hovers.clear();   // whatever lies under an NPC's page says nothing over it
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
        // A button that brings the other side's page: the cursor goes to (x, y) of the frame, nothing is clicked.
        const auto turn = [&](int left, int top, int right, int bottom, const std::string& words, int x, int y) {
            page.box(left, top, right, bottom, false);
            page.text(page.fit(words, right - left - 6), words, (left + right) / 2, (top + bottom) / 2, kGold, 0);
            map.spots.push_back({ left, top, right, bottom, x, y, true });
        };
        const auto& inventory = scene->inv_layout[std::size_t(cls)];
        const int to_inventory_x = inventory.grid_x + inventory.box_w / 2, to_inventory_y = inventory.grid_y + inventory.box_h / 2;

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
            handheld_line_y = line_y;                      // hover text keeps off this line's buttons where it can
            // Its words are the coin too: a touch there opens the box to drop gold (or to put it in the stash).
            const std::string gold_words = std::format("Gold {}", stats.get(d2d::d2s::kGold));
            page.text(small, gold_words, (kW - grid_w) / 2, line_y, kGold);
            map.spots.push_back({ (kW - grid_w) / 2 - 2, line_y - 7, (kW - grid_w) / 2 + small.measure(gold_words) + 6, line_y + 7, 493, 462 });
            if (store.npc >= 0 || stash_open || merc) {
                const auto& stash = scene->stash_layout[character.expansion ? 1 : 0];
                const auto& torso = scene->merc_layout.slots[std::size_t(body::kTorso)];
                const int right = (kW + grid_w) / 2;
                if (store.npc >= 0) {
                    turn(right - 84, line_y - 8, right, line_y + 8, "To the store", 96 + 14, 123 + 14);
                    // Sell the thing the cursor is on, for its price, in one touch (or the pad's Y): no mode to turn on first.
                    const d2d::d2s::Item* for_sale = held ? nullptr : bag_at(mouse.x, mouse.y);
                    const int sell_left = (kW - grid_w) / 2 + small.measure(gold_words) + 12, sell_right = right - 84 - 4;
                    if (for_sale && sell_right - sell_left > 40) {
                        const std::string words = std::format("Sell for {}", d2d::rules::item_price(scene->rules, *for_sale, store.npc_id, true, store.header));
                        page.box(sell_left, line_y - 8, sell_right, line_y + 8, true);
                        page.text(page.fit(words, sell_right - sell_left - 6), words, (sell_left + sell_right) / 2, line_y, kWhite, 0);
                        BottomMap::Spot spot{ sell_left, line_y - 8, sell_right, line_y + 8, mouse.x, mouse.y };
                        spot.sell = true;
                        map.spots.push_back(spot);
                    } else if (!held) page.text(small, "Pick a thing to sell", (sell_left + sell_right) / 2, line_y, kDimInk, 0);
                }
                else if (stash_open) turn(right - 96, line_y - 8, right, line_y + 8, "Stash page", stash.grid_x + stash.box_w / 2, stash.grid_y + stash.box_h / 2);
                else turn(right - 96, line_y - 8, right, line_y + 8, "To the hireling", torso[0] + torso[2] / 2, torso[1] + torso[3] / 2);
            } else page.text(small, "B: close", (kW + grid_w) / 2, line_y, kDimInk, 1);
            // The thing the cursor is on, if it can be used (a scroll, a tome, a potion): a button for it, which a finger has no
            // right click for. Identify then asks for the thing to identify: a touch on it (twice: once to read it).
            if (store.npc < 0) {
                const int left = (kW - grid_w) / 2 + small.measure(gold_words) + 12, right = (kW + grid_w) / 2 - (stash_open || merc ? 104 : 60);
                const d2d::d2s::Item* under = held ? nullptr : bag_at(mouse.x, mouse.y);
                // The stash open: the thing goes there with a touch (or the pad's Y), beside its Use when it has one.
                int use_right = right;
                if (under && identify_with < 0 && stash_open && !cube_open) {
                    const auto move = stash_move_at(mouse.x, mouse.y);
                    const bool room = move.item && move.to_x >= 0;
                    const int stash_left = item_use(*under).kind != ItemUseKind::None ? (left + right) / 2 + 2 : left;
                    page.box(stash_left, line_y - 8, right, line_y + 8, room, !room);
                    const std::string words = room ? "Put in stash" : "Stash full";
                    page.text(page.fit(words, right - stash_left - 6), words, (stash_left + right) / 2, line_y, room ? kWhite : kDimInk, 0);
                    if (room) { BottomMap::Spot spot{ stash_left, line_y - 8, right, line_y + 8, mouse.x, mouse.y }; spot.stash = true; map.spots.push_back(spot); }
                    use_right = stash_left == left ? left : stash_left - 4;
                }
                if (identify_with >= 0) page.text(small, "Touch what to identify", (left + right) / 2, line_y, kGold, 0);
                else if (under && use_right > left) {
                    const int right = use_right;
                    const auto use = item_use(*under);
                    if (use.kind != ItemUseKind::None && right - left >= 50) {
                        const std::string label = "Use: " + std::string(use.label);
                        page.box(left, line_y - 8, right, line_y + 8, true);
                        page.text(small, small.measure(label) <= right - left - 6 ? label : std::string("Use"), (left + right) / 2, line_y, kWhite, 0);
                        BottomMap::Spot spot{ left, line_y - 8, right, line_y + 8, mouse.x, mouse.y };
                        spot.use = true;
                        map.spots.push_back(spot);
                    } else if (!use.label.empty()) page.text(page.fit(std::string(use.label), right - left), std::string(use.label), (left + right) / 2, line_y, kDimInk, 0);
                }
            }
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
        } else if (kind == kSpeech) {
            // What an NPC says: the frame's box shows six lines of it; here the whole screen's worth rises past, in the
            // speech's own font and at its own pace (Speech::offset_px, which the voice keeps time with).
            const auto& told = scene->font_formal11.line_height() > 0 ? scene->font_formal11 : font;
            const auto& pal = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
            const int cell = told.sheet().frames_per_direction() > 0 ? int(told.sheet().frame(0, 0).height) : 16;
            constexpr int kTop = 4, kBottom = 208;
            page.rect(0, kTop - 2, kW, kBottom + 2, { 20, 17, 14 });
            int widest = 0;
            for (const auto& line : speech.lines) widest = std::max(widest, told.measure(line));
            const int left = std::max(4, (kW - widest) / 2), offset = speech.offset_px(now_ms);
            for (std::size_t index = 0; index < speech.lines.size(); ++index) {
                // The page starts full from its top (it began half way down, which read as missing text: the emulator
                // play test), and rises at the speech's own pace once the voice has caught up with it.
                const int rise = std::max(0, offset - (112 - cell));
                const int base = kTop + cell + int(index) * 18 - rise;
                if (base - cell + 1 >= kBottom || base < kTop) continue;
                told.draw_tinted(pixels, kW, kH, pal, left, base - cell + 1, speech.lines[index], 255, 255, 255, kTop, kBottom);
            }
            page.box(4, 212, kW - 4, 237, false);
            const std::string foot = "Up / Down: read on    A, B or touch: close";
            page.text(page.fit(foot, kW - 16), foot, kW / 2, 224, kInk, 0);
            map.spots.push_back({ 0, 0, kW, kH, mouse.x, mouse.y });       // any press shuts the speech: the cursor stays where it is
        } else if (kind == kNpcMenu) {
            // An NPC's menu (and its talk list, a confirmation, a refusal): its heading, then a large row for each choice.
            MenuPage list;
            list.active = true;
            int base = npc_menu.y;
            const int over_line = npc_menu.line_at(mouse.x, mouse.y);
            for (std::size_t index = 0; index < npc_menu.lines.size(); ++index) {
                const auto& line = npc_menu.lines[index];
                base += line.height;
                if (line.text.empty()) continue;
                if (line.header) { if (list.heading.empty()) list.heading = line.text; else list.notes.push_back(line.text); continue; }
                list.add(line.text, npc_menu.x + npc_menu.box_width / 2, base - line.height / 2, over_line == int(index));
            }
            draw_menu_page(list, *scene);
            pixels = list.pixels;
            for (const auto& row : list.rows) map.spots.push_back({ row.left, row.top, row.right, row.bottom, row.x, row.y });
        } else if (kind == kHireling) {
            // The hireling: what it wears, pixel for pixel (a weapon, the helm over the armour, the other hand), and its sheet in words.
            namespace body = d2d::d2s::body_location;
            const auto& sheet = *view.merc_sheet;
            const auto slot = [&](int which) { return scene->merc_layout.slots[std::size_t(which)]; };
            const bool left_first = slot(body::kRightArm)[0] <= slot(body::kLeftArm)[0];
            const std::array<std::vector<int>, 3> columns{ { { left_first ? body::kRightArm : body::kLeftArm }, { body::kHead, body::kTorso }, { left_first ? body::kLeftArm : body::kRightArm } } };
            int x = 2;
            for (const auto& column : columns) {
                int y = 24, wide = 0;
                for (const int which : column) {
                    const auto box = slot(which);
                    if (box[2] <= 0) continue;
                    map.pieces.push_back(part(box[0] - 1, box[1] - 1, box[2] + 2, box[3] + 2, x, y, 1.f, true));
                    y += box[3] + 4; wide = std::max(wide, box[2] + 2);
                }
                x += wide + 2;
            }
            for (const auto& piece : map.pieces) page.piece(ui_buffer, piece);
            page.text(page.fit(merc_label, x - 6), merc_label, 4, 11, kGold);
            int y = 12;
            const int side = x + 6;
            const auto line = [&](const std::string& name, const std::string& value, Rgb c = kWhite) {
                page.text(small, name, side, y, kInk);
                page.text(small, value, kW - 6, y, c, 1);
                y += 15;
            };
            line("Level", std::to_string(sheet.level));
            line("Life", std::format("{} / {}", sheet.life, sheet.max_life));
            line("Experience", std::to_string(sheet.exp));
            line("Next level", std::to_string(sheet.next));
            line("Strength", std::to_string(sheet.str));
            line("Dexterity", std::to_string(sheet.dex));
            line("Damage", std::format("{}-{}", sheet.min, sheet.max));
            line("Defense", std::to_string(sheet.defense));
            static constexpr std::array<const char*, 4> kMercResist = { "Fire resistance", "Cold resistance", "Lightning res.", "Poison resistance" };
            for (std::size_t index = 0; index < 4; ++index) line(kMercResist[index], std::to_string(sheet.res[index]), sheet.res[index] < 0 ? Rgb{ 255, 77, 77 } : kWhite);
            const int close_x = kCharPanelX + 0x110 + 16, close_y = int(kScreenHeight) - 0x3f - kCharPanelY - 16;
            if (inv_open) turn(side - 4, 196, kW - 4, 216, "To my items", to_inventory_x, to_inventory_y);
            button(side - 4, 218, kW - 4, 238, "Close", close_x, close_y, over(close_x - 16, close_y - 16, 33, 33));
        } else if (kind == kGoldBox) {
            // The gold box as it is, and its four buttons large beside the finger: less, more (held, they run), OK, Cancel.
            map.pieces.push_back(part(215, 140, 210, 158, (kW - 210) / 2, 2));
            page.piece(ui_buffer, map.pieces.back());
            const auto on = [&](GoldBoxPart which) { return gold_box_at(mouse.x, mouse.y) == which; };
            button(4, 170, 80, 236, "Less", 231, 226, on(GoldBoxPart::kDown));
            button(84, 170, 160, 236, "More", 231, 213, on(GoldBoxPart::kUp));
            button(164, 170, 238, 236, "OK", 266, 271, on(GoldBoxPart::kOk));
            button(242, 170, 316, 236, "Cancel", 371, 271, on(GoldBoxPart::kCancel));
        } else if (kind == kStore) {
            // The stock's grid (10x10 cells, 290 rows) at four fifths, which shows all of it; its tabs and buttons beside it.
            constexpr float kScale = .8f;
            map.pieces.push_back(part(96, 123, 290, 290, 2, 4, kScale, true));
            page.piece(ui_buffer, map.pieces.back());
            const int side = 2 + 232 + 4;
            for (int tab = 0; tab < 4; ++tab) {
                const bool empty = store.tabs[std::size_t(tab)].empty();
                button(side, 4 + tab * 25, kW - 2, 4 + tab * 25 + 23, string_id(*scene, kTabString[std::size_t(tab)]), kCharPanelX + 80 * tab + 40, 75, tab == store.tab, empty);
            }
            // Its four buttons in words (the frame's are pictures): Buy and Sell stay down while on, as there.
            static constexpr int kButtonX[4] = { 116, 169, 221, 273 };
            const bool repairs = store_button_frames(store)[2] == 6;
            const std::array<std::string, 4> names{ "Buy", "Sell", repairs ? "Repair" : "", repairs ? "Repair All" : "Leave" };
            for (int index = 0; index < 4; ++index) {
                if (names[std::size_t(index)].empty()) continue;
                const bool down = (index < 2 && store.mode == index + 1) || (index == 2 && store.mode == 3);
                button(side, 106 + index * 22, kW - 2, 106 + index * 22 + 20, names[std::size_t(index)], kCharPanelX - 1 + kButtonX[index] + 22, 453, down);
            }
            page.text(small, std::format("Gold {}", stats.get(d2d::d2s::kGold)), side + 2, 204, kGold);
            if (inv_open) turn(side, 216, kW - 2, 237, "To my items", to_inventory_x, to_inventory_y);
        } else if (kind == kStash) {
            const auto& stash = scene->stash_layout[character.expansion ? 1 : 0];
            const int grid_w = stash.cols * stash.box_w, grid_h = stash.rows * stash.box_h;
            const float scale = std::min(1.f, float(kH - 4) / float(grid_h));
            map.pieces.push_back(part(stash.grid_x, stash.grid_y, grid_w, grid_h, 2, 2, scale, true));
            page.piece(ui_buffer, map.pieces.back());
            const int side = 2 + int(float(grid_w) * scale) + 6, middle = (side + kW - 2) / 2;
            handheld_hover_w = side - 2;
            page.text(font, "Stash", middle, 14, kGold, 0);
            page.text(small, std::format("Gold in the stash: {}", stats.get(d2d::d2s::kGoldBank)), middle, 40, kInk, 0);
            page.text(small, std::format("Gold carried: {}", stats.get(d2d::d2s::kGold)), middle, 56, kInk, 0);
            button(side, 70, kW - 2, 100, "Take gold out", 163, 93, stash_coin_at(mouse.x, mouse.y));
            if (inv_open) button(side, 104, kW - 2, 134, "Put gold in", 493, 462, inventory_coin_at(mouse.x, mouse.y));
            if (inv_open) turn(side, 140, kW - 2, 170, "My items page", to_inventory_x, to_inventory_y);
            // The thing under the cursor back to the bag with a touch (or the pad's Y).
            if (const auto move = stash_move_at(mouse.x, mouse.y); move.item && !move.to_stash && identify_with < 0) {
                const bool room = move.to_x >= 0;
                page.box(side, 174, kW - 2, 204, room, !room);
                const std::string words = room ? "Take out" : "Bag is full";
                page.text(page.fit(words, kW - 2 - side - 6), words, (side + kW - 2) / 2, 189, room ? kWhite : kDimInk, 0);
                if (room) { BottomMap::Spot spot{ side, 174, kW - 2, 204, mouse.x, mouse.y }; spot.stash = true; map.spots.push_back(spot); }
            }
            page.text(small, "Touch twice: pick up", middle, 214, kDimInk, 0);
            page.text(small, "Y: move it across", middle, 228, kDimInk, 0);
        } else if (kind == kCube) {
            // The cube's cells and its button, over the inventory's grid: both on the one page.
            const auto& cube = scene->cube_layout;
            const int cube_w = cube.cols * cube.box_w, cube_h = cube.rows * cube.box_h;
            const int grid_w = inventory.cols * inventory.box_w, grid_h = inventory.rows * inventory.box_h;
            const int left = (kW - grid_w) / 2;
            map.pieces.push_back(part(cube.grid_x, cube.grid_y, cube_w, cube_h, left, 2, 1.f, true));
            page.piece(ui_buffer, map.pieces.back());
            if (inv_open) {
                map.pieces.push_back(part(inventory.grid_x, inventory.grid_y, grid_w, grid_h, left, kH - grid_h - 1, 1.f, true));
                page.piece(ui_buffer, map.pieces.back());
            }
            const int side = left + cube_w + 10;
            page.text(font, "Horadric Cube", (side + kW - 4) / 2, 14, kGold, 0);
            int press_x = kCharPanelX + 0x90 + 20, press_y = 0;
            for (int y = kCharPanelY; y < int(kScreenHeight); ++y) if (transmute_at(*scene, press_x, y)) { press_y = y + 19; break; }   // the frame's button, by its own test
            button(side, 30, kW - 4, 70, "Transmute", press_x, press_y, transmute_at(*scene, mouse.x, mouse.y));
            page.text(small, "Touch a thing to read it,", (side + kW - 4) / 2, 88, kDimInk, 0);
            page.text(small, "again to pick it up", (side + kW - 4) / 2, 102, kDimInk, 0);
        } else if (kind == kQuests) {
            // The acts' tabs; the six quests in one row; what the picked one says; its two buttons in words.
            map.pieces.push_back(part(kCharPanelX, kCharPanelY, 320, 29, 0, 0));
            constexpr float kIcon = .85f;
            for (int index = 0; index < 6; ++index) {
                const auto& box = kQuestSlot[std::size_t(index)];
                map.pieces.push_back(part(kCharPanelX + box.hit_x - 2, kCharPanelY + box.hit_y - 2, 60, 56, 2 + index * 53, 30, kIcon));
            }
            map.pieces.push_back(part(kCharPanelX, kCharPanelY + 226, 320, 138, 0, 79));
            for (const auto& piece : map.pieces) page.piece(ui_buffer, piece);
            if (quest_log.slot >= 0 && quest_log.slot < 6) {              // the picked quest: a rim round it
                const int left = 2 + quest_log.slot * 53, top = 30, right = left + 51, bottom = top + 47;
                page.rect(left, top, right, top + 1, kLit); page.rect(left, bottom - 1, right, bottom, kLit);
                page.rect(left, top, left + 1, bottom, kLit); page.rect(right - 1, top, right, bottom, kLit);
            }
            button(152, 218, 232, 238, "Speech", kCharPanelX + 0xe6 + 15, kCharPanelY + 0x187 + 16, quest_button_at(mouse.x, mouse.y) == 2);
            button(236, 218, 316, 238, "Close", kCharPanelX + 0x116 + 18, kCharPanelY + 0x188 + 17, quest_button_at(mouse.x, mouse.y) == 1);
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
        // The pad's cursor starts where the frame left it, which may be on nothing the page shows: then it goes to the
        // page's main piece (the grid's first cell; the tree has its own start, Town::pad_focus) or its first button.
        if (int at_x = 0, at_y = 0; pad_playing && kind != kTree && kind != kSpeech && kind != kNpcMenu && !map.to_page(mouse.x, mouse.y, at_x, at_y)) {
            const BottomMap::Piece* start = nullptr;
            for (const auto& piece : map.pieces) if (piece.look_first) start = &piece;
            if (start) cursor_move = std::pair{ start->src_x + std::min(14, start->src_w / 2), start->src_y + std::min(14, start->src_h / 2) };
            else if (!map.spots.empty()) cursor_move = std::pair{ map.spots.front().x, map.spots.front().y };
        }
        // What the pad's buttons would do with the thing under the cursor ("X to buy", "Hold B to sell"...), which the
        // frame writes at its foot: along the page's foot here, with the hold's progress under it.
        if (pad_playing && !pad_hold_text.empty() && (kind == kInventory || kind == kStore || kind == kStash || kind == kCube || kind == kHireling)) {
            Page foot{ pixels, *scene };
            const int width = std::min(kW - 4, small.measure(pad_hold_text) + 12), left = (kW - width) / 2;
            foot.rect(left, kH - 15, left + width, kH, { 110, 94, 66 });
            foot.rect(left + 1, kH - 14, left + width - 1, kH - 1, { 12, 10, 8 });
            foot.text(small, pad_hold_text, kW / 2, kH - 8, kGold, 0);
            if (pad_hold_fraction > 0.f) foot.rect(left + 1, kH - 3, left + 1 + int(float(width - 2) * std::clamp(pad_hold_fraction, 0.f, 1.f)), kH - 1, kLit);
        }
        // The pad's cursor on the page: a small ring where its point lands (the hand itself is drawn over it).
        if (int at_x = 0, at_y = 0; pad_playing && !held && map.on_piece(mouse.x, mouse.y) && map.to_page(mouse.x, mouse.y, at_x, at_y)) {
            Page ring{ pixels, *scene };
            for (const auto& [grow, c] : { std::pair{ 7, Rgb{ 20, 16, 10 } }, std::pair{ 6, kLit }, std::pair{ 5, kLit } }) {
                ring.rect(at_x - grow, at_y - grow, at_x + grow + 1, at_y - grow + 1, c); ring.rect(at_x - grow, at_y + grow, at_x + grow + 1, at_y + grow + 1, c);
                ring.rect(at_x - grow, at_y - grow, at_x - grow + 1, at_y + grow + 1, c); ring.rect(at_x + grow, at_y - grow, at_x + grow + 1, at_y + grow + 1, c);
            }
        }
        for (int y = 0; y < kH; ++y)
            std::copy_n(pixels.begin() + std::ptrdiff_t(std::size_t(y) * kW * 4), kW * 4, ui_buffer.begin() + std::ptrdiff_t(std::size_t(y) * kScreenWidth * 4));
        return true;
    }

}  // namespace d2d::client
