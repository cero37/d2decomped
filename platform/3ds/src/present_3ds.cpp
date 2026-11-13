// SPDX-License-Identifier: GPL-3.0-or-later
// The frame onto the 3DS's two screens, straight into their framebuffers.
//   top (400x240):    in game, the world's 240 rows of the 400-wide frame, pixel for pixel;
//                     elsewhere (the menus' 800x600) the frame averaged down to fit.
//   bottom (320x240): the second screen's HUD (the top-left of the UI frame), or black.
// A 3DS framebuffer is the screen on its side: 240 wide, a column a row,
// bottom first; RGBA8 words are R in the top byte.
#include <3ds.h>

#include "handheld_hud.hpp"
#include "handheld_prof.hpp"

#include <log.hpp>

#include <cstdint>
#include <cstdio>
#include <malloc.h>
#include <cstring>
#include <string>
#include <vector>

const char* logs_3ds_notes();

namespace d2d::client {

namespace {
inline std::uint32_t word(const std::uint8_t* rgba) { return std::uint32_t(rgba[0]) << 24 | std::uint32_t(rgba[1]) << 16 | std::uint32_t(rgba[2]) << 8 | 0xFFu; }
}

void handheld_present(const std::vector<std::uint8_t>& frame, int frame_w, int frame_h, int clip_top, bool world_only, const std::vector<std::uint8_t>* bottom, int bottom_stride, int bottom_x, int bottom_y) {
    u16 fb_w = 0, fb_h = 0;
    auto* top = reinterpret_cast<std::uint32_t*>(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_w, &fb_h));   // 240 x 400
    if (top && world_only && g_top_panel.size() >= std::size_t(320) * kPanelHalf * 4) {
        // A panel is open: its other half at the foot of the top screen, black round it.
        std::memset(top, 0, 240 * 400 * 4);
        for (int x = 0; x < 320; ++x) {
            std::uint32_t* column = top + (x + 40) * 240;
            const std::uint8_t* source = g_top_panel.data() + (std::size_t(kPanelHalf - 1) * 320 + std::size_t(x)) * 4;
            for (int y = 0; y < kPanelHalf; ++y, source -= 320 * 4) column[y] = word(source);
        }
    } else if (top && world_only && frame_w == 400) {
        for (int x = 0; x < 400; ++x) {
            std::uint32_t* column = top + x * 240;
            const std::uint8_t* source = frame.data() + (std::size_t(clip_top + 239) * 400 + std::size_t(x)) * 4;
            for (int y = 0; y < 240; ++y, source -= 400 * 4) column[y] = word(source);   // column[0] is the bottom row
        }
    } else if (top && world_only && frame_w == 600) {
        // Zoomed out by a half: 600x360 of the world, three pixels into two each way (weights 2:1, 1:2).
        const std::size_t pitch = 600 * 4;
        for (int y = 0; y < 240; ++y) {
            const std::uint8_t* upper = frame.data() + std::size_t(clip_top + (y / 2) * 3 + (y & 1)) * pitch;
            const std::uint8_t* lower = upper + pitch;
            const unsigned wu = (y & 1) ? 1 : 2, wl = 3 - wu;
            std::uint32_t* out = top + (239 - y);
            for (int x = 0; x < 200; ++x, upper += 12, lower += 12, out += 480) {
                unsigned red = wu * (2 * upper[0] + upper[4]) + wl * (2 * lower[0] + lower[4]), green = wu * (2 * upper[1] + upper[5]) + wl * (2 * lower[1] + lower[5]), blue = wu * (2 * upper[2] + upper[6]) + wl * (2 * lower[2] + lower[6]);
                out[0] = (red * 7282u >> 16) << 24 | (green * 7282u >> 16) << 16 | (blue * 7282u >> 16) << 8 | 0xFFu;
                red = wu * (upper[4] + 2 * upper[8]) + wl * (lower[4] + 2 * lower[8]); green = wu * (upper[5] + 2 * upper[9]) + wl * (lower[5] + 2 * lower[9]); blue = wu * (upper[6] + 2 * upper[10]) + wl * (lower[6] + 2 * lower[10]);
                out[240] = (red * 7282u >> 16) << 24 | (green * 7282u >> 16) << 16 | (blue * 7282u >> 16) << 8 | 0xFFu;
            }
        }
    } else if (top && world_only && frame_w == 800) {
        // Zoomed out to the PC's scale: 800x480 of the world, four pixels into one.
        const std::size_t pitch = 800 * 4;
        for (int y = 0; y < 240; ++y) {
            const std::uint8_t* upper = frame.data() + std::size_t(clip_top + y * 2) * pitch;
            const std::uint8_t* lower = upper + pitch;
            std::uint32_t* out = top + (239 - y);
            for (int x = 0; x < 400; ++x, upper += 8, lower += 8, out += 240)
                *out = ((upper[0] + upper[4] + lower[0] + lower[4]) >> 2) << 24 | ((upper[1] + upper[5] + lower[1] + lower[5]) >> 2) << 16 | ((upper[2] + upper[6] + lower[2] + lower[6]) >> 2) << 8 | 0xFFu;
        }
    } else if (top && frame_w >= 800 && frame_h >= 600) {
        // 800x600 -> 400x240: two columns and three rows a pixel (rows step 2.5, so they
        // overlap by one now and then, which keeps thin lines). Six samples: no division by a variable.
        const int off = (frame_w - 800) / 2;
        const std::size_t pitch = std::size_t(frame_w) * 4;
        for (int y = 0; y < 240; ++y) {
            const int first = y * 5 / 2 > 597 ? 597 : y * 5 / 2;
            const std::uint8_t* pixel = frame.data() + std::size_t(first) * pitch + std::size_t(off) * 4;
            std::uint32_t* out = top + (239 - y);
            for (int x = 0; x < 400; ++x, pixel += 8, out += 240) {
                const std::uint8_t *a = pixel, *b = pixel + pitch, *c = pixel + 2 * pitch;
                const unsigned red = a[0] + a[4] + b[0] + b[4] + c[0] + c[4], green = a[1] + a[5] + b[1] + b[5] + c[1] + c[5], blue = a[2] + a[6] + b[2] + b[6] + c[2] + c[6];
                *out = (red * 10923u >> 16) << 24 | (green * 10923u >> 16) << 16 | (blue * 10923u >> 16) << 8 | 0xFFu;
            }
        }
    }
    auto* low = reinterpret_cast<std::uint32_t*>(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &fb_w, &fb_h));   // 240 x 320
    if (low) {
        // bottom_x < 0: the whole 800x600 UI frame at two fifths (a mid-screen menu is up).
        const std::vector<std::uint8_t>* whole = bottom && bottom_x < 0 && bottom_stride >= 800 && bottom->size() >= std::size_t(bottom_stride) * 600 * 4 ? bottom
                                                : !bottom && !world_only && frame_w >= 800 && frame_h >= 600 ? &frame : nullptr;
        const int whole_stride = whole == &frame ? frame_w : bottom_stride;
        if (bottom && bottom_x >= 0 && bottom_y >= 0 && bottom_x + 320 <= bottom_stride && bottom->size() >= std::size_t(bottom_stride) * std::size_t(bottom_y + 240) * 4) {
            for (int x = 0; x < 320; ++x) {
                std::uint32_t* column = low + x * 240;
                const std::uint8_t* source = bottom->data() + (std::size_t(bottom_y + 239) * std::size_t(bottom_stride) + std::size_t(bottom_x + x)) * 4;
                for (int y = 0; y < 240; ++y, source -= std::size_t(bottom_stride) * 4) column[y] = word(source);
            }
        } else if (whole) {
            // The menus: the whole 800x600 at two fifths, where a touch can reach all of it.
            // Each pixel covers 2.5 x 2.5 of the frame: the first, the second and the last of its
            // columns and rows (the last is the second again half the time). Nine samples, fixed.
            const int off = (whole_stride - 800) / 2;
            const std::size_t pitch = std::size_t(whole_stride) * 4;
            for (int y = 0; y < 240; ++y) {
                const int sy = y * 5 / 2, sy_last = (y + 1) * 5 / 2 - 1;
                const std::uint8_t* rows[3] = { whole->data() + std::size_t(sy) * pitch, whole->data() + std::size_t(sy + 1) * pitch, whole->data() + std::size_t(sy_last) * pitch };
                std::uint32_t* out = low + (239 - y);
                for (int x = 0; x < 320; ++x, out += 240) {
                    const int sx = off + x * 5 / 2, last = off + (x + 1) * 5 / 2 - 1;
                    unsigned red = 0, green = 0, blue = 0;
                    for (const std::uint8_t* row : rows) {
                        const std::uint8_t *a = row + sx * 4, *c = row + last * 4;
                        red += a[0] + a[4] + c[0]; green += a[1] + a[5] + c[1]; blue += a[2] + a[6] + c[2];
                    }
                    *out = (red * 7282u >> 16) << 24 | (green * 7282u >> 16) << 16 | (blue * 7282u >> 16) << 8 | 0xFFu;
                }
            }
        } else {
            std::memset(low, 0, 240 * 320 * 4);
        }
    }
    // At the first frame: what this console gave us. Then every ten seconds: the frame rate, the
    // memory in use and where the frames' time went, in the game's log.
    static unsigned frames = 0;
    static u64 last = 0;
    if (last == 0) {
        bool new_model = false;
        APT_CheckNew3DS(&new_model);
        d2d::log::info("3ds: {} | application memory {} MiB | heap {} MiB | linear heap {} MiB | system {}.{}.{}", new_model ? "New 3DS" : "old 3DS (not supported)",
                       osGetMemRegionSize(MEMREGION_APPLICATION) >> 20, envGetHeapSize() >> 20, envGetLinearHeapSize() >> 20,
                       unsigned(GET_VERSION_MAJOR(osGetFirmVersion())), unsigned(GET_VERSION_MINOR(osGetFirmVersion())), unsigned(GET_VERSION_REVISION(osGetFirmVersion())));
        for (const char* notes = logs_3ds_notes(); *notes;) {
            const char* end = std::strchr(notes, '\n');
            d2d::log::info("{}", std::string(notes, end ? std::size_t(end - notes) : std::strlen(notes)));
            if (!end) break;
            notes = end + 1;
        }
        last = osGetTime();
    }
    ++frames;
    if (const u64 now = osGetTime(); now - last >= 10000) {
        const struct mallinfo heap = mallinfo();
        d2d::log::info("perf: {} frames in {} ms ({:.1f} fps) | heap used {} of {} KiB | linear free {} KiB", frames, now - last, double(frames) * 1000.0 / double(now - last),
                       heap.uordblks / 1024, envGetHeapSize() / 1024, linearSpaceFree() / 1024);
        for (auto& entry : prof::g_spans) {
            if (!entry.name) break;
            d2d::log::info("perf:   {:<26} {:6.1f} ms a frame ({} calls)", entry.name, double(entry.ns) / 1e6 / double(frames), entry.calls);
            entry.ns = 0; entry.calls = 0;
        }
        frames = 0; last = now;
    }
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

}  // namespace d2d::client
