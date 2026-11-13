// SPDX-License-Identifier: GPL-3.0-or-later
// The handheld's second screen (the 3DS's 320x240 touch screen): the HUD
// the top screen doesn't carry. It draws into the top-left 320x240 of an
// 800x600 frame, so every blitter and font works on it as on the PC's.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace d2d::d2s { struct Stats; struct Item; }

namespace d2d::client {

struct Scene;
struct Hud;

inline constexpr int kBottomWidth = 320, kBottomHeight = 240;

struct BottomHud {
    const d2d::d2s::Stats* stats = nullptr;
    const std::vector<d2d::d2s::Item>* belt = nullptr;
    const Hud* hud = nullptr;
    std::string_view level_name;
};

void draw_bottom_hud(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const BottomHud& state);

}  // namespace d2d::client
