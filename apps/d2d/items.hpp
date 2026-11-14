// SPDX-License-Identifier: GPL-3.0-or-later
// Item hover text drawn (the lines: item_text.hpp).
#pragma once

#include "common.hpp"
#include "scene.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace d2d::client {

// A handheld's panels are a page of its second screen (town/handheld_panels.cpp): hover text is not drawn
// where the frame would have it, which is off that page as often as on it. While this points at a list
// the two functions below add their lines to it and draw nothing; the page shows them itself, fitted.
inline std::vector<std::vector<TextLine>>* g_hover_capture = nullptr;

// Hover text box: lines centred over [x0, x1], bottom on `bottom` (below
// `top` instead when it would leave the screen), on a darkened backdrop.
void draw_hover_text(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<TextLine>& lines,
                     int left, int right, int top, int bottom);
// The same lines in a box placed already (rules::hover_box: {left, top,
// right, bottom}), darkened, each line centred in it.
// The lines in a box inside (left, top, width, height) of the frame, clear of the cursor at (avoid_x, avoid_y):
// a handheld's, where the text must fit a 320x240 screen (town/handheld_panels.cpp).
void draw_handheld_hover(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<TextLine>& lines,
                         int left, int top, int width, int height, int avoid_x, int avoid_y);
void draw_hover_lines(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<TextLine>& lines, const std::array<int, 4>& box);

}  // namespace d2d::client
