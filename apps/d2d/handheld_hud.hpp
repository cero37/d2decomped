// SPDX-License-Identifier: GPL-3.0-or-later
// The handheld's second screen (the 3DS's 320x240 touch screen): the HUD
// the top screen doesn't carry. It draws into the top-left 320x240 of an
// 800x600 frame, so every blitter and font works on it as on the PC's.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <atomic>
#include <vector>

namespace d2d::d2s { struct Stats; struct Item; }

namespace d2d::client {

struct Scene;
struct Hud;
struct Automap;

inline constexpr int kBottomWidth = 320, kBottomHeight = 240;

struct BottomHud {
    const d2d::d2s::Stats* stats = nullptr;
    const std::vector<d2d::d2s::Item>* belt = nullptr;
    const Hud* hud = nullptr;
    std::string_view level_name;
    const Automap* automap = nullptr;
    float map_x = 0, map_y = 0;   // the player on the automap, in its cells
};

// The skills on the buttons, as the pad's skill buttons have them
// (Town::present_skill_pad): the trigger's, then the six slots', with the
// buttons' names. `alternate`: the second set is held.
struct BottomSkills {
    int main = -1;
    std::array<int, 6> slots{ -1, -1, -1, -1, -1, -1 };
    std::string main_label;
    std::array<std::string, 6> labels;
    bool alternate = false;
    // The Start list of panels and the skill-assignment ring, drawn on the second screen in the map's place.
    bool strip_on = false, wheel_on = false;
    int strip_at = 0, wheel_at = 0, wheel_page = 0;
    std::vector<int> wheel_ids;
    std::string wheel_prompt, feedback;
    // The row's seven slots (the trigger's, then the six buttons'), as Town tells them each frame:
    // `fired`: when the slot's button last worked it (pressed or held), `belt_fired` the same for the belt's
    // four columns: the slot shows lit for a moment after. `status`: Fight::skill_status of the slot's skill
    // (0 usable, 1 too little mana, 8 its delay still runs, else not usable now); `wait_ms`: what is left of the delay.
    std::array<std::uint32_t, 7> fired{};
    std::array<std::uint32_t, 4> belt_fired{};
    std::array<int, 7> status{}, wait_ms{};
    std::uint32_t now = 0;
};
inline BottomSkills g_bottom_skills;
// Which of the list's nine buttons, or of the ring's `shown` icons, a touch at (x, y) is on; -1 for none.
int bottom_strip_hit(int x, int y);
int bottom_wheel_hit(int x, int y, int shown);
// Which of the HUD's skill slots a touch is on: 0 the trigger's, 1..6 the buttons' in their order; -1 for none.
int bottom_slot_hit(int x, int y);

// An open panel is 432 rows and a screen 240: it goes across both, 216 rows each, its upper half
// at the foot of the top screen (this image, 320 x kPanelHalf RGBA; empty when no panel is up)
// and its lower half on the touch screen, over a bar of two buttons (flip the halves, close).
inline unsigned g_bottom_serial = 0;   // goes up each time the HUD is drawn again: the console copies it to its screen only then
inline constexpr int kPanelHalf = 216;
inline std::vector<std::uint8_t> g_top_panel;
inline bool g_panel_flip = false;      // the upper half on the touch screen instead
inline int g_bottom_bar_tap = -1;      // a touch on the bar: its x, for Town::draw to take

// The second screen's frame this frame (the UI frame, 800 wide; its top-left
// 320x240), or null outside the game. Town::draw sets it.
inline const std::vector<std::uint8_t>* g_bottom_frame = nullptr;

// A panel is open (inventory, character, stash, a store...): the second
// screen shows the PC's own panel, a 320x240 window of the UI frame from
// (g_bottom_origin_x, g_bottom_origin_y), and a touch is the mouse there.
// The panels are 320 wide, so only the height scrolls: the window follows
// the cursor.
inline bool g_bottom_panels = false;
// Something of the PC's that sits mid-screen is up (the game menu, an NPC's menu or speech,
// the death message): the second screen shows the whole UI frame at two fifths, as it does
// the menus, and a touch is the mouse on it.
inline bool g_bottom_whole = false;
// When the last input event came (SDL ticks, ms; main.cpp's event loop), and whether this frame keeps
// the open panel's page as it was (Town::draw: no input for a while, the cursor where it was, the page
// drawn under a quarter second ago). The panels at 800x600 and their page cost most of a 3DS frame.
inline std::uint64_t g_last_event_ms = 0;
inline bool g_keep_panels = false;
// Clicks the game makes for itself, one a frame (main.cpp): a thing moved between the bag and the stash
// (Town::handheld_stash_move) is picked up, put down where it fits, and the cursor goes back where it was.
// `wait_held`: not until the pick-up is back from the World (the thing on the cursor).
struct AutoClick { int x = 0, y = 0; bool click = true, wait_held = false; };
inline std::vector<AutoClick> g_auto_clicks;
// What the loader is doing (finish_scene), under the title's Loading bar; empty: nothing named.
inline std::atomic<const char*> g_loading_step{ "" };
inline int g_bottom_origin_x = 0, g_bottom_origin_y = 0;
// An NPC's menu or speech is up: the second screen shows just that box, scaled to fill it (up to
// twice its size) and laid in the UI frame's top-left 320x240; a touch at (x, y) is the mouse at
// (source_x + (x - left) / scale, source_y + (y - top) / scale).
struct BottomZoom { bool on = false; int source_x = 0, source_y = 0, left = 0, top = 0; float scale = 1.f; };
inline BottomZoom g_bottom_zoom;

// A panel made to fit the second screen (town/handheld_panels.cpp): the page in the UI frame's top-left
// 320x240 is put together from pieces of the PC's panel (a rect of the frame, drawn at `scale` at a place
// on the page) and from the page's own buttons (a `Spot`: a rect of the page that stands for a click at
// a point of the frame). A touch on the page is the mouse at the frame's point for it; the cursor, which
// lives in the frame, shows where its point lands on the page.
struct BottomMap {
    // look_first: a touch on something the cursor is not on yet only puts the cursor there (its hover text
    // shows); a touch on it again is the click. A finger has no other way to read before it acts.
    struct Piece { int src_x = 0, src_y = 0, src_w = 0, src_h = 0, dst_x = 0, dst_y = 0; float scale = 1.f; bool look_first = false; };
    // move: the touch only puts the cursor at the point (to bring the other side's page), no click.
    // use: the touch is a right click where the cursor is (the thing under it is used), not a click at the point.
    struct Spot { int left = 0, top = 0, right = 0, bottom = 0, x = 0, y = 0; bool move = false, use = false, stash = false, sell = false; };
    enum Touch { kClick, kLookFirst, kMove, kUse, kStash, kSell };
    bool on = false;
    std::vector<Piece> pieces;
    std::vector<Spot> spots;
    void clear() { on = false; pieces.clear(); spots.clear(); }
    [[nodiscard]] bool to_frame(int page_x, int page_y, int& frame_x, int& frame_y, Touch* how = nullptr) const {
        if (how) *how = kClick;
        for (const auto& spot : spots)
            if (page_x >= spot.left && page_x < spot.right && page_y >= spot.top && page_y < spot.bottom) {
                frame_x = spot.x; frame_y = spot.y;
                if (how && spot.move) *how = kMove;
                if (how && spot.use) *how = kUse;
                if (how && spot.stash) *how = kStash;
                if (how && spot.sell) *how = kSell;
                return true;
            }
        for (const auto& piece : pieces) {
            const int width = int(float(piece.src_w) * piece.scale), height = int(float(piece.src_h) * piece.scale);
            if (page_x < piece.dst_x || page_y < piece.dst_y || page_x >= piece.dst_x + width || page_y >= piece.dst_y + height) continue;
            frame_x = piece.src_x + std::min(piece.src_w - 1, int((float(page_x - piece.dst_x) + .5f) / piece.scale));
            frame_y = piece.src_y + std::min(piece.src_h - 1, int((float(page_y - piece.dst_y) + .5f) / piece.scale));
            if (how && piece.look_first) *how = kLookFirst;
            return true;
        }
        return false;
    }
    [[nodiscard]] bool on_piece(int frame_x, int frame_y) const {
        for (const auto& piece : pieces)
            if (frame_x >= piece.src_x && frame_y >= piece.src_y && frame_x < piece.src_x + piece.src_w && frame_y < piece.src_y + piece.src_h) return true;
        return false;
    }
    [[nodiscard]] bool to_page(int frame_x, int frame_y, int& page_x, int& page_y) const {
        for (const auto& piece : pieces) {
            if (frame_x < piece.src_x || frame_y < piece.src_y || frame_x >= piece.src_x + piece.src_w || frame_y >= piece.src_y + piece.src_h) continue;
            page_x = piece.dst_x + int(float(frame_x - piece.src_x) * piece.scale);
            page_y = piece.dst_y + int(float(frame_y - piece.src_y) * piece.scale);
            return true;
        }
        for (const auto& spot : spots)
            if (spot.x == frame_x && spot.y == frame_y) { page_x = (spot.left + spot.right) / 2; page_y = (spot.top + spot.bottom) / 2; return true; }
        return false;
    }
};
inline BottomMap g_bottom_map;

// A touch on the second screen with no panel open (the HUD's own buttons): the last one, for the HUD to take.
// How much of the world the top screen shows: 0 = 400x240 pixel for pixel, 1 = 600x360, 2 = 800x480
// (the PC's scale), scaled down to the screen. A tap on the second screen's map steps it.
inline int g_handheld_zoom = 1;
struct BottomTouch { int x = 0, y = 0; bool down = false, pressed = false, released = false; };
inline BottomTouch g_bottom_touch;

// The console's own present (platform/3ds): both screens from the frame.
void handheld_present(const std::vector<std::uint8_t>& frame, int frame_w, int frame_h, int clip_top, bool world_only, const std::vector<std::uint8_t>* bottom, int bottom_stride, int bottom_x, int bottom_y);

void draw_bottom_hud(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const BottomHud& state);

// Over the title while the game loads: the frame dimmed below the logo, "Loading" and a moving bar.
void draw_loading_note(std::vector<std::uint8_t>& framebuffer, const Scene& scene, std::uint32_t now_ms);

}  // namespace d2d::client
