// SPDX-License-Identifier: GPL-3.0-or-later
// D2Decomp DT1 (Diablo Tile) parser.
//
// DT1 = an atlas of tiles that compose a Diablo II level. Each tile is a
// collection of 32×32 pixel blocks positioned within a tile-local bounding
// box. Blocks are stored in one of two encodings (isometric diamond or
// RLE) and decoded into a single palette-indexed pixel buffer per tile.
//
// File layout (little-endian throughout):
//
//   +0x000  i32  majorVersion  (= 7)
//   +0x004  i32  minorVersion  (= 6)
//   +0x008  260 bytes  padding
//   +0x10C  i32  numberOfTiles
//   +0x110  i32  tileHeadersOffset  (usually 0x114 — right after)
//
// Per tile header (96 bytes):
//   i32  direction
//   i16  roofHeight
//   u16  materialFlags
//   i32  height          (can be NEGATIVE — floor tiles extend up from y=0)
//   i32  width
//   4    padding
//   i32  type             (orientation: floor / left-wall / right-wall / …)
//   i32  style
//   i32  sequence
//   i32  rarityFrameIndex
//   4    padding
//   25   subTileFlags     (5×5 grid, walkability + LoS per subtile)
//   7    padding
//   i32  blockHeaderPointer  (file offset)
//   i32  blockHeaderSize
//   i32  numBlocks
//   12   padding
//
// Per block header (20 bytes, at blockHeaderPointer):
//   i16  x, y             (position within tile, in pixels; can be negative)
//   2    padding
//   u8   gridX, gridY
//   i16  format           (0 = RLE, 1 = 3D isometric)
//   i32  length           (encoded pixel data length in bytes)
//   2    padding
//   i32  fileOffset       (RELATIVE to blockHeaderPointer)
//
// Block encoded data:
//   * Isometric (256 bytes exactly):
//     Diamond fill with row widths {4,8,12,16,20,24,28,32,28,24,20,16,12,8,4}
//     and x-offsets  {14,12,10,8,6,4,2,0,2,4,6,8,10,12,14}.
//   * RLE:
//     Pairs (skipX, runLen); skipX advances x by that many transparent
//     pixels, then runLen palette-index bytes follow. When both bytes of the
//     pair are 0, advance to next scanline (x=0, y++).
//
// Block x counts from the cell's left corner in a 160-wide frame, whatever
// the tile's width: draw a tile at the cell's left corner, not centred
// (1677 of the tiles are narrower; every block of every DT1 lies in
// [0, width)).
//
// Output: each tile gets a width × abs(height) row-major byte buffer with
// (none with Pixels::skip: headers and subtile flags only)
// palette indices; index 0 is transparent. game.exe draws the signed block
// coordinates directly (FUN_005130a0); d2d instead shifts blocks by
// max(0, -min(block.y)) to pack this nonnegative buffer (docs/research/re/dt1.md).
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::dt1 {

struct Tile;

// A DT1 file's bytes and the tiles decoded from it so far. Decoded tiles
// are about nine times the file (Act 1's outdoors: 9 MB of DT1s, 75 MB of
// pixels), so a tile decodes when it is first drawn and the least recently
// drawn ones are dropped when the cache is over its budget (trim()).
// Archives of one file (two levels of a level type) share one of these.
class File {
public:
    explicit File(std::span<const std::byte> bytes) : bytes_(bytes.begin(), bytes.end()) {}
    ~File() { for (auto& slot : slots_) delete slot.pixels.load(std::memory_order_relaxed); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }
    void set_tile_count(std::size_t count) { if (slots_.size() != count) slots_ = std::vector<Slot>(count); }

    // The tile's pixels, decoded now if they aren't. Safe from the draw
    // threads; trim() must not run while any of them draws.
    [[nodiscard]] const std::uint8_t* pixels(const Tile& tile, std::size_t index);

    // One frame has passed: tiles drawn since the last call count as fresh.
    // Over `budget` bytes of decoded pixels, the stalest go.
    static void trim(std::size_t budget = default_budget()) {
        auto& all = registry();
        const std::lock_guard lock(all.mutex);
        const auto now = all.clock.fetch_add(1, std::memory_order_relaxed);
        if (all.decoded_bytes <= budget) return;
        struct Old { std::uint32_t age; Slot* slot; };
        std::vector<Old> old;
        for (auto it = all.files.begin(); it != all.files.end();) {
            const auto file = it->lock();
            if (!file) { it = all.files.erase(it); continue; }
            for (auto& slot : file->slots_)
                if (slot.pixels.load(std::memory_order_relaxed) && slot.used.load(std::memory_order_relaxed) != now)
                    old.push_back({ now - slot.used.load(std::memory_order_relaxed), &slot });
            ++it;
        }
        std::sort(old.begin(), old.end(), [](const Old& left, const Old& right) { return left.age > right.age; });
        for (const auto& entry : old) {
            if (all.decoded_bytes <= budget) break;
            const auto* pixels = entry.slot->pixels.exchange(nullptr, std::memory_order_relaxed);
            all.decoded_bytes -= pixels->size();
            delete pixels;
        }
    }
    [[nodiscard]] static std::size_t decoded_bytes() { auto& all = registry(); const std::lock_guard lock(all.mutex); return all.decoded_bytes; }
    // D2D_TILE_CACHE_MB, else 12 MB on the 3DS and 96 MB elsewhere.
    [[nodiscard]] static std::size_t default_budget() {
        static const std::size_t budget = [] {
            if (const char* text = std::getenv("D2D_TILE_CACHE_MB"); text && *text) return std::size_t(std::atoi(text)) << 20;
#ifdef __3DS__
            return std::size_t(12) << 20;
#else
            return std::size_t(96) << 20;
#endif
        }();
        return budget;
    }

    // The file of `key` (its path) if an archive still holds it, else a new one of `bytes`.
    [[nodiscard]] static std::shared_ptr<File> shared(std::string_view key, std::span<const std::byte> bytes) {
        auto& all = registry();
        const std::lock_guard lock(all.mutex);
        if (!key.empty())
            if (const auto found = all.by_key.find(std::string(key)); found != all.by_key.end())
                if (auto file = found->second.lock(); file && file->bytes_.size() == bytes.size()) return file;
        auto file = std::make_shared<File>(bytes);
        all.files.push_back(file);
        if (!key.empty()) all.by_key[std::string(key)] = file;
        return file;
    }

private:
    struct Slot {
        std::atomic<const std::vector<std::uint8_t>*> pixels{ nullptr };
        std::atomic<std::uint32_t> used{ 0 };
    };
    struct Registry {
        std::mutex mutex;
        std::vector<std::weak_ptr<File>> files;
        std::unordered_map<std::string, std::weak_ptr<File>> by_key;
        std::size_t decoded_bytes = 0;
        std::atomic<std::uint32_t> clock{ 1 };
    };
    static Registry& registry() { static Registry all; return all; }

    std::vector<std::byte> bytes_;
    std::vector<Slot> slots_;
};

struct Tile {
    std::int32_t  direction{};
    std::int16_t  roof_height{};
    std::uint16_t material_flags{};
    std::int32_t  height{};     // may be negative (floor tiles)
    std::int32_t  width{};
    std::int32_t  type{};       // orientation code
    std::int32_t  style{};
    std::int32_t  sequence{};
    std::int32_t  rarity_frame_index{};
    std::array<std::uint8_t, 25> subtile_flags{};
    std::int32_t y_shift{};             // added to block y to land in pixels()
    // width * abs(height) palette indices, 0=transparent; null for a tile
    // with no area or of an archive read with Pixels::skip. Valid until the
    // next File::trim().
    [[nodiscard]] const std::uint8_t* pixels() const { return file ? file->pixels(*this, index) : nullptr; }
    File* file{};                       // the archive's; decodes this tile on demand
    std::uint32_t index{};              // this tile's number in its DT1
    std::int32_t block_headers{}, block_count{};   // where its blocks are in the file
    std::vector<std::pair<std::int16_t, std::int16_t>> blocks;   // each block's top left in pixels[] (a wall's draw clips by them)
};

// Whether an archive decodes its tiles' pixels. A headless game server
// needs only the headers and subtile flags (walkability).
enum class Pixels { decode, skip };

class Archive {
public:
    Archive() = default;
    // `key` (the file's path) lets archives of the same file share its bytes and decoded tiles.
    explicit Archive(std::span<const std::byte> bytes, Pixels pixels = Pixels::decode, std::string_view key = {}) {
        if (pixels == Pixels::decode) {
            file_ = File::shared(key, bytes);
            parse(file_->bytes(), pixels);
            file_->set_tile_count(tiles_.size());
            for (std::size_t i = 0; i < tiles_.size(); ++i)
                if (buf_height(tiles_[i]) != 0 && tiles_[i].width > 0) { tiles_[i].file = file_.get(); tiles_[i].index = std::uint32_t(i); }
        } else {
            parse(bytes, pixels);
        }
    }

    [[nodiscard]] std::span<const Tile> tiles() const noexcept { return tiles_; }
    [[nodiscard]] std::size_t size() const noexcept { return tiles_.size(); }

    // A tile's pixels from its blocks (File::pixels calls this once per decode).
    static void decode(const Tile& tile, std::span<const std::byte> bytes, std::vector<std::uint8_t>& out) {
        constexpr std::size_t kBlockHdr = 20;
        out.assign(std::size_t(tile.width) * buf_height(tile), 0);
        for (std::int32_t j = 0; j < tile.block_count; ++j) {
            const std::byte* block_header = bytes.data() + tile.block_headers + j * kBlockHdr;
            const auto x = rd_i16(block_header + 0);
            const auto y = rd_i16(block_header + 2);
            const auto format = rd_i16(block_header + 8);
            const auto length = rd_i32(block_header + 10);
            const auto data = std::span(bytes.data() + std::size_t(tile.block_headers) + std::size_t(rd_i32(block_header + 16)), std::size_t(length));
            if (format == 1) decode_isometric(tile, out, x, y, data);
            else decode_rle(tile, out, x, y, data);
        }
    }

private:
    static std::int16_t rd_i16(const std::byte* source) {
        std::int16_t value; std::memcpy(&value, source, 2); return value;
    }
    static std::uint16_t rd_u16(const std::byte* source) {
        std::uint16_t value; std::memcpy(&value, source, 2); return value;
    }
    static std::int32_t rd_i32(const std::byte* source) {
        std::int32_t value; std::memcpy(&value, source, 4); return value;
    }

    void parse(std::span<const std::byte> bytes, Pixels pixels) {
        if (bytes.size() < 0x114) throw std::runtime_error("DT1: truncated header");

        const auto major = rd_i32(bytes.data());
        const auto minor = rd_i32(bytes.data() + 4);
        if (major != 7 || minor != 6)
            throw std::runtime_error("DT1: unsupported version");

        const auto numTiles = rd_i32(bytes.data() + 0x10c);
        const auto tileHdrOff = rd_i32(bytes.data() + 0x110);
        if (numTiles < 0 || tileHdrOff < 0)
            throw std::runtime_error("DT1: negative counts");

        constexpr std::size_t kTileHdr = 96;
        if (bytes.size() < std::size_t(tileHdrOff)
                     + std::size_t(numTiles) * kTileHdr)
            throw std::runtime_error("DT1: tile header table OOB");

        tiles_.resize(numTiles);
        // Pass 1: tile headers (block metadata lives elsewhere in the file).
        std::vector<std::pair<std::int32_t, std::int32_t>> block_ptrs(numTiles);
        for (std::int32_t i = 0; i < numTiles; ++i) {
            const std::byte* header = bytes.data() + tileHdrOff + i * kTileHdr;
            auto& tile = tiles_[i];
            tile.direction          = rd_i32(header + 0);
            tile.roof_height        = rd_i16(header + 4);
            tile.material_flags     = rd_u16(header + 6);
            tile.height             = rd_i32(header + 8);
            tile.width              = rd_i32(header + 12);
            // h + 16: 4 bytes padding
            tile.type               = rd_i32(header + 20);
            tile.style              = rd_i32(header + 24);
            tile.sequence           = rd_i32(header + 28);
            tile.rarity_frame_index = rd_i32(header + 32);
            // h + 36: 4 bytes padding
            for (int k = 0; k < 25; ++k)
                tile.subtile_flags[k] = std::uint8_t(header[40 + k]);
            // h + 65: 7 bytes padding
            const auto blkHdrPtr   = rd_i32(header + 72);
            // h + 76: blockHeaderSize (unused)
            const auto numBlocks   = rd_i32(header + 80);
            // h + 84: 12 bytes padding
            block_ptrs[i]          = {blkHdrPtr, numBlocks};
        }
        if (pixels == Pixels::skip) return;

        // Pass 2: block headers, checked here so that decode() needn't; the
        // pixels come later, when the tile is drawn (File::pixels).
        // d2d packs the decoded blocks into a nonnegative pixel buffer using
        // y_shift = max(0, -min(block.y)). game.exe draws signed block Y
        // directly (docs/research/re/dt1.md). put_pixel clips decoded pixels
        // that fall outside d2d's own buffer.
        constexpr std::size_t kBlockHdr = 20;
        struct BlockDecode {
            std::int16_t x, y;
        };
        std::vector<BlockDecode> decoded;
        for (std::int32_t i = 0; i < numTiles; ++i) {
            const auto [blkHdrPtr, numBlocks] = block_ptrs[i];
            if (numBlocks == 0) continue;
            if (blkHdrPtr < 0
                || std::size_t(blkHdrPtr) + std::size_t(numBlocks) * kBlockHdr
                   > bytes.size())
                throw std::runtime_error("DT1: block header table OOB");

            decoded.clear();
            decoded.reserve(numBlocks);
            std::int32_t min_y = 0;
            for (std::int32_t j = 0; j < numBlocks; ++j) {
                const std::byte* block_header = bytes.data() + blkHdrPtr + j * kBlockHdr;
                BlockDecode block;
                block.x      = rd_i16(block_header + 0);
                block.y      = rd_i16(block_header + 2);
                // bh + 4: padding, bh + 6/7: gridX/Y (unused), bh + 8: format
                const auto length = rd_i32(block_header + 10);
                // bh + 14: padding
                const auto blkOff = rd_i32(block_header + 16);   // relative to blkHdrPtr

                const std::size_t dataOff =
                    std::size_t(blkHdrPtr) + std::size_t(blkOff);
                if (length < 0 || dataOff + std::size_t(length) > bytes.size())
                    throw std::runtime_error("DT1: block data OOB");

                if (block.y < min_y) min_y = block.y;
                decoded.push_back(block);
            }

            tiles_[i].y_shift = -min_y;   // >= 0
            tiles_[i].block_headers = blkHdrPtr;
            tiles_[i].block_count = numBlocks;
            for (const auto& block : decoded)
                tiles_[i].blocks.emplace_back(block.x, std::int16_t(block.y - min_y));
        }
    }

    static std::size_t buf_height(const Tile& tile) {
        return std::size_t(tile.height < 0 ? -tile.height : tile.height);
    }

    static void put_pixel(const Tile& tile, std::vector<std::uint8_t>& out, std::int32_t pixel_x, std::int32_t pixel_y,
                          std::uint8_t value) {
        const auto y = pixel_y + tile.y_shift;
        if (pixel_x < 0 || pixel_x >= tile.width) return;
        if (y < 0 || std::size_t(y) >= buf_height(tile)) return;
        out[std::size_t(y) * tile.width + pixel_x] = value;
    }

    static void decode_isometric(const Tile& tile, std::vector<std::uint8_t>& out, std::int16_t block_x, std::int16_t block_y,
                                 std::span<const std::byte> enc) {
        // Fixed 256-byte diamond fill. If the block is short, silently clip.
        static constexpr int xjump[15] = {14,12,10,8,6,4,2,0,2,4,6,8,10,12,14};
        static constexpr int nbpix[15] = { 4, 8,12,16,20,24,28,32,28,24,20,16,12, 8, 4};
        std::size_t idx = 0;
        for (int row = 0; row < 15 && idx < enc.size(); ++row) {
            const int count = nbpix[row];
            const int left = xjump[row];
            for (int k = 0; k < count; ++k, ++idx) {
                if (idx >= enc.size()) return;
                put_pixel(tile, out, block_x + left + k, block_y + row, std::uint8_t(enc[idx]));
            }
        }
    }

    static void decode_rle(const Tile& tile, std::vector<std::uint8_t>& out, std::int16_t block_x, std::int16_t block_y,
                           std::span<const std::byte> enc) {
        std::int32_t x = 0, y = 0;
        std::size_t idx = 0;
        std::int32_t remain = static_cast<std::int32_t>(enc.size());
        while (remain > 0) {
            const auto first_byte = std::uint8_t(enc[idx]);
            const auto second_byte = std::uint8_t(enc[idx + 1]);
            idx    += 2;
            remain -= 2;
            if ((first_byte | second_byte) == 0) {
                x = 0;
                ++y;
                continue;
            }
            x += first_byte;
            if (idx + second_byte > enc.size()) return;   // a short block: what there is of it
            for (int k = 0; k < second_byte; ++k) {
                put_pixel(tile, out, block_x + x, block_y + y, std::uint8_t(enc[idx + k]));
                ++x;
            }
            idx    += second_byte;
            remain -= second_byte;
        }
    }

    std::shared_ptr<File> file_;   // null with Pixels::skip
    std::vector<Tile> tiles_;
};

inline const std::uint8_t* File::pixels(const Tile& tile, std::size_t index) {
    auto& slot = slots_[index];
    auto& all = registry();
    slot.used.store(all.clock.load(std::memory_order_relaxed), std::memory_order_relaxed);
    if (const auto* ready = slot.pixels.load(std::memory_order_acquire)) return ready->data();
    const std::lock_guard lock(all.mutex);
    if (const auto* ready = slot.pixels.load(std::memory_order_acquire)) return ready->data();
    auto* decoded = new std::vector<std::uint8_t>;
    Archive::decode(tile, bytes_, *decoded);
    all.decoded_bytes += decoded->size();
    slot.pixels.store(decoded, std::memory_order_release);
    return decoded->data();
}

}  // namespace d2d::dt1
