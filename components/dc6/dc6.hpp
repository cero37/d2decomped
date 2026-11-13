// SPDX-License-Identifier: GPL-3.0-or-later
// D2Decomp DC6 sprite parser.
//
// DC6 = Diablo Cel format 6. Palette-indexed sprite frames, RLE-encoded.
// Structure:
//   header (24 bytes: version, subver, encoding, terminator, dirs, frames)
//   frame pointer table (dirs * frames_per_dir * u32)
//   per frame: 32-byte header (flip, w, h, ox, oy, unk, next, len)
//              `len` bytes of RLE-encoded pixel data
//              3-byte terminator (0xEE 0xEE 0xEE)
//
// RLE tokens (one byte each):
//   0x80             : end of scanline (advance y towards next row)
//   0x81..0xFF       : (t & 0x7F) transparent pixels
//   0x00..0x7F       : t raw palette-index bytes follow
//
// Scanlines are bottom-up by default (dwFlip == 0). dwFlip != 0 means
// top-down (rare — a few UI elements only).
//
// Output pixels[]: width*height palette indices, row-major from top row.
// Index 0 is D2's transparent colour.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace d2d::dc6 {

struct Frame {
    std::uint32_t width{}, height{};
    std::int32_t  offset_x{}, offset_y{};
    bool          flipped{};
    std::vector<std::uint8_t> pixels;   // width * height
};

class Sprite {
public:
    Sprite() = default;

    explicit Sprite(std::span<const std::byte> bytes) {
        parse(bytes);
    }

    // A sprite read when it is first looked at (through set_loader's reader),
    // for art most sessions never show: a panel nobody opens costs nothing.
    // A file that isn't there, or won't parse, is a sprite with no frames.
    using Loader = std::function<std::optional<std::vector<std::byte>>(const std::string& path)>;
    static void set_loader(Loader loader) { const std::scoped_lock lock(lazy_mutex()); lazy_loader() = std::move(loader); }
    [[nodiscard]] static Sprite lazy(std::string path) { Sprite sprite; sprite.lazy_path_ = std::move(path); return sprite; }

    [[nodiscard]] std::uint32_t directions() const noexcept { ensure(); return directions_; }
    [[nodiscard]] std::uint32_t frames_per_direction() const noexcept { ensure(); return framesPerDir_; }
    [[nodiscard]] std::size_t total_frames() const noexcept { ensure(); return frames_.size(); }

    [[nodiscard]] const Frame& frame(std::uint32_t dir, std::uint32_t idx) const {
        ensure();
        if (dir >= directions_ || idx >= framesPerDir_) {
            throw std::out_of_range("DC6: frame out of range");
        }
        return frames_[dir * framesPerDir_ + idx];
    }

    [[nodiscard]] std::span<const Frame> frames() const noexcept { ensure(); return frames_; }

private:
    static std::mutex& lazy_mutex() { static std::mutex mutex; return mutex; }
    static Loader& lazy_loader() { static Loader loader; return loader; }
    void ensure() const noexcept {
        if (lazy_path_.empty()) return;
        const std::scoped_lock lock(lazy_mutex());
        if (lazy_path_.empty() || !lazy_loader()) return;      // read meanwhile, or no reader yet (asked again later)
        auto* self = const_cast<Sprite*>(this);                 // the sprite's own cache: it is the same sprite either way
        try {
            if (const auto bytes = lazy_loader()(lazy_path_)) self->parse(*bytes);
        } catch (...) {
            self->frames_.clear(); self->directions_ = 0; self->framesPerDir_ = 0;
        }
        self->lazy_path_.clear();
    }

    static std::uint32_t rd32(const std::byte* source) {
        std::uint32_t value; std::memcpy(&value, source, 4); return value;
    }

    void parse(std::span<const std::byte> bytes) {
        constexpr std::size_t kFileHdr = 24;
        constexpr std::size_t kFrameHdr = 32;
        if (bytes.size() < kFileHdr) throw std::runtime_error("DC6: truncated header");

        const auto version   = rd32(bytes.data());
        if (version != 6) throw std::runtime_error("DC6: bad version");

        directions_   = rd32(bytes.data() + 16);
        framesPerDir_ = rd32(bytes.data() + 20);
        const auto total = std::size_t(directions_) * framesPerDir_;
        if (total == 0) return;

        const std::size_t ptrTblOff = kFileHdr;
        if (bytes.size() < ptrTblOff + total * 4)
            throw std::runtime_error("DC6: truncated pointer table");

        frames_.reserve(total);
        for (std::size_t i = 0; i < total; ++i) {
            const auto frameOff = rd32(bytes.data() + ptrTblOff + i * 4);
            if (frameOff + kFrameHdr > bytes.size())
                throw std::runtime_error("DC6: bad frame offset");

            const std::byte* frame_header = bytes.data() + frameOff;
            Frame frame;
            frame.flipped  = rd32(frame_header + 0)  != 0;
            frame.width    = rd32(frame_header + 4);
            frame.height   = rd32(frame_header + 8);
            frame.offset_x = static_cast<std::int32_t>(rd32(frame_header + 12));
            frame.offset_y = static_cast<std::int32_t>(rd32(frame_header + 16));
            // fh + 20: unknown/reserved
            // fh + 24: next-block offset (unused here — the pointer table
            //          already tells us where each frame starts)
            const auto rleLen = rd32(frame_header + 28);

            const std::size_t dataOff = frameOff + kFrameHdr;
            if (dataOff + rleLen > bytes.size())
                throw std::runtime_error("DC6: RLE data OOB");

            decode_rle(frame, std::span(bytes.data() + dataOff, rleLen));
            frames_.push_back(std::move(frame));
        }
    }

    static void decode_rle(Frame& frame, std::span<const std::byte> rle) {
        frame.pixels.assign(std::size_t(frame.width) * frame.height, 0);
        if (frame.width == 0 || frame.height == 0) return;

        // Scanline direction: bottom-up when flip=0 (D2 convention),
        // top-down when flip!=0.
        std::uint32_t x = 0;
        std::int64_t  y = frame.flipped ? 0 : std::int64_t(frame.height) - 1;
        const auto width = frame.width;

        for (std::size_t i = 0; i < rle.size(); ++i) {
            const auto token = std::uint8_t(rle[i]);
            if (token == 0x80) {                        // end of scanline
                x = 0;
                y += frame.flipped ? 1 : -1;
                continue;
            }
            if (token & 0x80) {                          // transparent run
                x += std::uint32_t(token & 0x7F);
                continue;
            }
            // Raw pixel run of length t. Bounds-check both source and dest.
            if (i + token >= rle.size()) throw std::runtime_error("DC6: raw run OOB");
            if (y < 0 || y >= std::int64_t(frame.height))
                throw std::runtime_error("DC6: scanline OOB");
            for (std::uint8_t k = 0; k < token; ++k) {
                if (x >= width) throw std::runtime_error("DC6: pixel x OOB");
                frame.pixels[std::size_t(y) * width + x++] =
                    std::uint8_t(rle[++i]);
            }
        }
    }

    std::uint32_t directions_{};
    std::uint32_t framesPerDir_{};
    std::vector<Frame> frames_;
    std::string lazy_path_;                    // set: not read yet (Sprite::lazy)
};

}  // namespace d2d::dc6
