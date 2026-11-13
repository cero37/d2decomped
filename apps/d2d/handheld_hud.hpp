// SPDX-License-Identifier: GPL-3.0-or-later
// The handheld's second screen (the 3DS's 320x240 touch screen): the HUD
// the top screen doesn't carry. It draws into the top-left 320x240 of an
// 800x600 frame, so every blitter and font works on it as on the PC's.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
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
};
inline BottomSkills g_bottom_skills;

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
inline int g_bottom_origin_x = 0, g_bottom_origin_y = 0;

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
