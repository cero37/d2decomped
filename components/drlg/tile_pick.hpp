// SPDX-License-Identifier: GPL-3.0-or-later
// Picking a room's DT1 tile (RoomTile.cpp, FUN_0066d820): the DT1s a
// room lists and the rarity pick on the room seed. docs/research/re/drlg.md
// "Room tiles".
#pragma once

#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::drlg {

// A DT1's tile headers, what the pick reads (0x60-byte records from +0x110).
struct Dt1Head { int orient = 0, style = 0, seq = 0, rarity = 0, material = 0; };   // material: +6
struct Dt1File {
    std::string name;                                   // lower-case file name
    std::vector<Dt1Head> tiles;                         // file order
    // The tiles by (orient, style, seq), each key's in reverse file order (index_tiles): a pick
    // looks its key up, where scanning every header of every DT1 per pick was most of a lay.
    struct Keyed { int orient, style, seq, index; };
    std::vector<Keyed> by_key;
};
// Dt1File::by_key, built once the headers are read (never lazily: builds on workers share files).
inline void index_tiles(Dt1File& file) {
    file.by_key.clear();
    file.by_key.reserve(file.tiles.size());
    for (int i = 0; i < int(file.tiles.size()); ++i)
        file.by_key.push_back({ file.tiles[std::size_t(i)].orient, file.tiles[std::size_t(i)].style, file.tiles[std::size_t(i)].seq, i });
    std::ranges::sort(file.by_key, [](const Dt1File::Keyed& a, const Dt1File::Keyed& b) {
        return std::tie(a.orient, a.style, a.seq, b.index) < std::tie(b.orient, b.style, b.seq, a.index);
    });
}
template <class Bytes> Dt1File dt1_heads(std::string name, const Bytes& bytes) {
    Dt1File file{ std::move(name), {} };
    auto read_i32 = [&](std::size_t offset) { std::int32_t value = 0; if (offset + 4 <= bytes.size()) std::memcpy(&value, bytes.data() + offset, 4); return value; };
    const int count = read_i32(0x10c), off = read_i32(0x110);
    for (int i = 0; i < count; ++i) {
        const auto header = std::size_t(off) + std::size_t(i) * 0x60;
        file.tiles.push_back({ read_i32(header + 0x14), read_i32(header + 0x18), read_i32(header + 0x1c), read_i32(header + 0x20), (read_i32(header + 4) >> 16) & 0xffff });
    }
    index_tiles(file);
    return file;
}

// The DT1s a level's rooms can list: LvlTypes File1..32 of its type (index
// = mask bit), then Blank, InvisWal, Warp (always appended).
struct RoomDt1s {
    std::array<const Dt1File*, 32> by_bit{};
    std::array<const Dt1File*, 3> always{};
};

// One placed tile: layer 0 wall, 1 floor, 2 shadow; room-relative x, y; the
// orientation it was picked with; which DT1 of the room's list and which
// tile of that file (-1 = nothing matched).
struct RoomTile { int layer, x, y, orient; const Dt1File* file; int index; std::uint32_t word = 0; };   // word: what flags it (FUN_0066db20)

// A room's DT1 list (FUN_0066f240): its mask's files in bit order, then the three.
inline std::vector<const Dt1File*> room_dt1_list(std::uint32_t mask, const RoomDt1s& dt1s) {
    std::vector<const Dt1File*> list;
    for (int bit = 0; bit < 32; ++bit)
        if ((mask >> bit & 1) && dt1s.by_bit[std::size_t(bit)]) list.push_back(dt1s.by_bit[std::size_t(bit)]);
    for (const auto* file : dt1s.always) if (file) list.push_back(file);
    return list;
}


// FUN_0066d820: up to 40 candidates with the orientation / style /
// sequence, DT1 by DT1 in list order, each DT1's in reverse file order
// (its hash bucket list is built by inserting at the front); the first
// whose running rarity reaches rand(total) + 1.
// The candidates for a key (FUN_00604ae0): up to 40, the room's DT1s in
// list order, each one's tiles in reverse file order.
inline std::vector<std::pair<const Dt1File*, int>> tile_candidates(const std::vector<const Dt1File*>& list, int orient, int style, int seq) {
    std::vector<std::pair<const Dt1File*, int>> candidates;
    for (const auto* file : list) {
        if (file->by_key.size() == file->tiles.size()) {
            const auto key = [](const Dt1File::Keyed& keyed) { return std::tuple{ keyed.orient, keyed.style, keyed.seq }; };
            const auto [first, last] = std::ranges::equal_range(file->by_key, std::tuple{ orient, style, seq }, {}, key);
            for (auto it = first; it != last && candidates.size() < 40; ++it) candidates.emplace_back(file, it->index);
        } else {
            for (int i = int(file->tiles.size()) - 1; i >= 0 && candidates.size() < 40; --i) {
                const auto& tile = file->tiles[std::size_t(i)];
                if (tile.orient == orient && tile.style == style && tile.seq == seq) candidates.emplace_back(file, i);
            }
        }
        if (candidates.size() >= 40) break;
    }
    return candidates;
}

inline std::pair<const Dt1File*, int> pick_tile(const std::vector<const Dt1File*>& list, d2d::rules::Rng& rng, int orient,
                                           std::uint32_t word) {
    const int style = word ? int((word >> 20) & 0x3f) : 0, seq = word ? int((word >> 8) & 0xff) : 0;
    auto candidates = tile_candidates(list, orient, style, seq);
    if (candidates.empty()) {
        candidates = tile_candidates(list, 10, 0, 0);
        return candidates.empty() ? std::pair<const Dt1File*, int>{ nullptr, -1 } : candidates.front();
    }
    int total = 0;
    for (auto [file, tile_index] : candidates) total += file->tiles[std::size_t(tile_index)].rarity;
    if (total < 1) return candidates.front();
    int roll = int(rng(total)) + 1;
    std::size_t index = 0;
    while (candidates.size() > 1 && roll > 0) { roll -= candidates[index].first->tiles[std::size_t(candidates[index].second)].rarity; ++index; }
    return candidates[index ? index - 1 : 0];
}


}  // namespace d2d::drlg
