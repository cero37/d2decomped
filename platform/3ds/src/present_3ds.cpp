// SPDX-License-Identifier: GPL-3.0-or-later
// The frame onto the 3DS's two screens, straight into their framebuffers.
//   top (400x240):    in game, the world's 240 rows of the 400-wide frame, pixel for pixel;
//                     elsewhere (the menus' 800x600) the frame averaged down to fit.
//   bottom (320x240): the second screen's HUD (the top-left of the UI frame), or black.
// A 3DS framebuffer is the screen on its side: 240 wide, a column a row,
// bottom first; RGBA8 words are R in the top byte.
#include <3ds.h>

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
    if (top && world_only && frame_w == 400) {
        for (int x = 0; x < 400; ++x) {
            std::uint32_t* column = top + x * 240;
            const std::uint8_t* source = frame.data() + (std::size_t(clip_top + 239) * 400 + std::size_t(x)) * 4;
            for (int y = 0; y < 240; ++y, source -= 400 * 4) column[y] = word(source);   // column[0] is the bottom row
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
