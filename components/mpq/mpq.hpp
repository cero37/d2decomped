// SPDX-License-Identifier: GPL-3.0-or-later
// D2Decomp MPQ reader — thin RAII wrapper over StormLib.
//
// Two types: Archive (one .mpq) and Stack (search across many in push order,
// first-match wins). Priority ordering matches the D2 model documented in
// research: patch archives pushed first, base archives after.
//
// Header-only. No exceptions on missing files — use try_read; read() throws
// only when the file exists but the read itself fails (I/O error, decompress
// failure). Constructor throws when the archive won't open at all.
#pragma once

#include "bnpatch.hpp"

#include <log.hpp>

#include <StormLib.h>
#include <StormPort.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace d2d::mpq {

// What the archives were asked for since the last take(), the main thread's apart from the
// workers': how many files, how long the reads took, how much of that the main thread spent
// waiting for a worker's read to let go of the archives, and the first few names. For a slow
// frame's report.
struct ReadStats {
    std::mutex mutex;
    struct Side { std::uint32_t files = 0; std::uint64_t ns = 0, wait_ns = 0; std::string names; };
    Side main, other;
    struct Taken { Side main, other; std::uint32_t files; std::uint64_t ns; };
    Taken take() {
        const std::scoped_lock lock(mutex);
        Taken taken{ std::move(main), std::move(other), main.files + other.files, main.ns + other.ns };
        main = {}; other = {};
        return taken;
    }
    void add(bool on_main, std::string_view name, std::uint64_t ns, std::uint64_t wait_ns) {
        const std::scoped_lock lock(mutex);
        auto& side = on_main ? main : other;
        ++side.files; side.ns += ns; side.wait_ns += wait_ns;
        if (side.names.size() < 160) { side.names += side.names.empty() ? "" : ", "; side.names += name.substr(name.find_last_of("\\/") + 1); }
    }
};
inline ReadStats g_read_stats;

// The thread that draws: its reads go first. A worker's read (a level being built, an animation)
// goes in pieces and steps aside between them while the main thread waits for the archives:
// the main thread once waited 300 ms for a level's tile file to finish reading.
inline std::thread::id g_main_thread = std::this_thread::get_id();
inline void mark_main_thread() { g_main_thread = std::this_thread::get_id(); }
inline std::atomic<int> g_main_waiting{ 0 };
// A worker whose reads the player waits on (the animation worker): other workers let it in before
// their next file (g_urgent_waiting), but only the main thread makes them stop in the middle of one.
inline thread_local bool t_urgent_reads = false;
inline std::atomic<int> g_urgent_waiting{ 0 };
// Set by Stack::try_read on a worker for the read under way: called between pieces of a file.
inline thread_local const std::function<void()>* t_between_pieces = nullptr;

// A file opened for streaming reads (videos: the D2 intro is 94 MB).
// Only for plain archive files, not installer entries.
class File {
public:
    File() = default;
    explicit File(HANDLE file) : file_(file) {}
    File(File&& other) noexcept : file_(std::exchange(other.file_, nullptr)) {}
    File& operator=(File&& other) noexcept { if (this != &other) { close(); file_ = std::exchange(other.file_, nullptr); } return *this; }
    ~File() { close(); }
    [[nodiscard]] std::uint64_t size() const {
        DWORD high = 0;
        const DWORD low = SFileGetFileSize(file_, &high);
        return std::uint64_t(high) << 32 | low;
    }
    // Bytes read (0 at the end).
    std::size_t read(void* buf, std::size_t count) {
        DWORD got = 0;
        SFileReadFile(file_, buf, DWORD(count), &got, nullptr);
        return got;
    }
    // Absolute position; returns the new one.
    std::uint64_t seek(std::uint64_t pos) {
        LONG high = LONG(pos >> 32);
        return SFileSetFilePointer(file_, LONG(pos & 0xffffffffu), &high, FILE_BEGIN) | std::uint64_t(DWORD(high)) << 32;
    }
private:
    void close() { if (file_) SFileCloseFile(file_); file_ = nullptr; }
    HANDLE file_ = nullptr;
};

class Archive {
public:
    explicit Archive(const std::filesystem::path& path) {
        // StormLib's TCHAR is wchar_t on Windows when UNICODE is defined, char otherwise.
        // Lookups are by name (hash), so the (listfile) / (attributes) StormLib
        // would parse on open aren't needed: skipping them took startup's
        // game data from ~900 ms to ~180 ms.
        if (!SFileOpenArchive(path.string<TCHAR>().c_str(), 0,
                              MPQ_OPEN_READ_ONLY | STREAM_FLAG_READ_ONLY | MPQ_OPEN_NO_LISTFILE | MPQ_OPEN_NO_ATTRIBUTES,
                              &handle_)) {
            throw std::runtime_error("MPQ open failed: " + path.string());
        }
    }
    ~Archive() { if (handle_) SFileCloseArchive(handle_); }

    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;

    Archive(Archive&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)), remap_(std::move(other.remap_)) {}

    Archive& operator=(Archive&& other) noexcept {
        if (this != &other) {
            if (handle_) SFileCloseArchive(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
            remap_ = std::move(other.remap_);
        }
        return *this;
    }

    // A Blizzard patch installer (LODPatch_114d.exe: an MPQ appended to an
    // exe) read as the patch_d2.mpq it would install. Files sit flat in the
    // archive; patch.lst maps game paths to them
    // ("data\global\excel\armor.txt;armor.txt;0x0", ENG patchstring.tbl =
    // patchstring~01.tbl)
    // and each carries a 24-byte header:
    //   u16 size (24), u8 4, u8 stored (1 = raw, 0 = delta),
    //   u32 CRC-32 of the base file, u32 base size, u32 output size,
    //   u64 filetime.
    // Raw entries (stored = 1) come back directly; the rest are BNUpdate
    // deltas against the same file in the base MPQs, which Stack applies
    // (bnpatch.hpp) — Archive alone can't see the base.
    [[nodiscard]] static Archive installer(const std::filesystem::path& path) {
        Archive archive(path);
        const auto lst = archive.try_read("patch.lst");
        if (!lst) throw std::runtime_error("not a patch installer: " + path.string());
        std::string_view all(reinterpret_cast<const char*>(lst->data()), lst->size());
        while (!all.empty()) {
            auto line = all.substr(0, all.find('\n'));
            all.remove_prefix(std::min(all.size(), line.size() + 1));
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            const auto first_semicolon = line.find(';');
            if (first_semicolon == line.npos) continue;
            const auto second_semicolon = line.find(';', first_semicolon + 1);
            archive.remap_.emplace(key(line.substr(0, first_semicolon)),
                             std::string(line.substr(first_semicolon + 1, second_semicolon == line.npos ? second_semicolon : second_semicolon - first_semicolon - 1)));
        }
        return archive;
    }

    [[nodiscard]] bool is_installer() const noexcept { return !remap_.empty(); }

    [[nodiscard]] bool installer_lists(std::string_view name) const { return remap_.contains(key(name)); }

    // Installer only: the entry for a game path, 24-byte header included.
    [[nodiscard]] std::optional<std::vector<std::byte>>
    installer_entry(std::string_view name) const {
        const auto found = remap_.find(key(name));
        if (found == remap_.end()) return std::nullopt;
        return read_raw(found->second);
    }

    [[nodiscard]] bool contains(std::string_view name) const {
        if (!remap_.empty()) return try_read(name).has_value();
        return SFileHasFile(handle_, std::string(name).c_str());
    }

    // Returns the decompressed file bytes. Throws if the file is missing OR
    // the read fails — callers who tolerate absence should use try_read.
    [[nodiscard]] std::vector<std::byte> read(std::string_view name) const {
        auto data = try_read(name);
        if (!data) throw std::runtime_error("MPQ file not found: " + std::string(name));
        return std::move(*data);
    }

    [[nodiscard]] std::optional<std::vector<std::byte>>
    try_read(std::string_view name) const {
        if (!remap_.empty()) {
            const auto found = remap_.find(key(name));
            if (found == remap_.end()) return std::nullopt;
            auto raw = read_raw(found->second);
            if (!raw || raw->size() < 24) return std::nullopt;
            const auto byte_at = [&](std::size_t offset) { return std::uint8_t((*raw)[offset]); };
            const std::uint32_t len = byte_at(12) | byte_at(13) << 8 | byte_at(14) << 16 | std::uint32_t(byte_at(15)) << 24;
            if ((byte_at(0) | byte_at(1) << 8) != 24 || byte_at(3) != 1 || 24 + std::size_t(len) > raw->size())
                return std::nullopt;   // compressed entry: not decodable yet
            return std::vector<std::byte>(raw->begin() + 24, raw->begin() + 24 + len);
        }
        return read_raw(name);
    }

    [[nodiscard]] std::optional<File> open(std::string_view name) const {
        if (!remap_.empty()) return std::nullopt;
        HANDLE file{};
        if (!SFileOpenFileEx(handle_, std::string(name).c_str(), 0, &file)) return std::nullopt;
        return File(file);
    }

    // Case/slash-insensitive lookup key, like MPQ name hashing.
    static std::string key(std::string_view name) {
        std::string normalised(name);
        for (auto& letter : normalised) letter = letter == '/' ? '\\' : char(std::tolower((unsigned char)letter));
        return normalised;
    }

private:
    [[nodiscard]] std::optional<std::vector<std::byte>>
    read_raw(std::string_view name) const {
        const std::string namez(name);
        HANDLE file{};
        if (!SFileOpenFileEx(handle_, namez.c_str(), 0, &file)) {
            // StormLib 9.31 renamed its error accessor. Its Windows DLL doesn't
            // export the new name (DllMain.def), which there only wraps the
            // Win32 error anyway. An existence check cannot distinguish a
            // corrupt entry from a missing one: it runs the same validation as
            // the open.
#if !defined(_WIN32) && STORMLIB_VERSION >= 0x091F
            if (SErrGetLastError() == ERROR_FILE_NOT_FOUND) return std::nullopt;
#else
            if (::GetLastError() == ERROR_FILE_NOT_FOUND) return std::nullopt;
#endif
            throw std::runtime_error("MPQ open failed: " + namez);
        }
        const DWORD size = SFileGetFileSize(file, nullptr);
        std::vector<std::byte> buf(size);
        bool ok = true;
        // A worker's read in pieces, the main thread let in between them (t_between_pieces).
        constexpr DWORD kPiece = 32 * 1024;
        for (DWORD at = 0; ok && at < size;) {
            const DWORD want = t_between_pieces ? std::min(kPiece, size - at) : size - at;
            DWORD got = 0;
            ok = SFileReadFile(file, buf.data() + at, want, &got, nullptr) && got == want;
            at += got;
            if (ok && at < size && t_between_pieces) (*t_between_pieces)();
        }
        SFileCloseFile(file);
        if (!ok) throw std::runtime_error("MPQ read failed: " + namez);
        return buf;
    }

    HANDLE handle_ = nullptr;
    std::unordered_map<std::string, std::string> remap_;   // installer only
};

// Every file in the archive at `path` read through, decompressed, and
// checked against its sector CRCs / (attributes) CRC32 and MD5 where the
// archive has them (SFileVerifyFile; of D2's, only d2exp, d2xtalk and
// d2xmusic carry (attributes), CRC32s only). Names unknown to the archive's
// listfile (d2video.mpq has none) come back as StormLib's File00000000.xxx.
// `progress` gets each file's packed size; false from it stops early.
// Throws when the archive won't open.
struct Verified {
    std::size_t files = 0;
    std::vector<std::string> damaged;
};
[[nodiscard]] inline Verified verify(const std::filesystem::path& path,
                                     const std::function<bool(std::uint64_t)>& progress) {
    HANDLE archive{};
    if (!SFileOpenArchive(path.string<TCHAR>().c_str(), 0, MPQ_OPEN_READ_ONLY | STREAM_FLAG_READ_ONLY, &archive))
        throw std::runtime_error("MPQ open failed: " + path.string());
    Verified result;
    SFILE_FIND_DATA found{};
    HANDLE find = SFileFindFirstFile(archive, "*", &found, nullptr);
    for (bool more = find != nullptr; more; more = SFileFindNextFile(find, &found)) {
        const std::string_view name = found.cFileName;
        if (name == "(listfile)" || name == "(attributes)" || name == "(signature)") continue;
        ++result.files;
        // LoD's (attributes) CRC32s for WAVs are of the WAV before its lossy
        // ADPCM packing, so they never match (bugs.md); the read still counts.
        const DWORD ignore = name.ends_with(".wav") || name.ends_with(".WAV") ? VERIFY_FILE_CHECKSUM_ERROR : 0;
        if (SFileVerifyFile(archive, found.cFileName, SFILE_VERIFY_ALL) & VERIFY_FILE_ERROR_MASK & ~ignore)
            result.damaged.emplace_back(name);
        if (!progress(found.dwCompSize)) break;
    }
    if (find) SFileFindClose(find);
    SFileCloseArchive(archive);
    return result;
}

class Stack {
public:
    // Highest priority first. D2 pushes patch_d2.mpq before base archives.
    void push(const std::filesystem::path& path) { const std::scoped_lock lock(shared_->mutex); shared_->archives.emplace_back(path); shared_->sources.emplace_back(path, false); }
    void push_installer(const std::filesystem::path& path) {
        const std::scoped_lock lock(shared_->mutex);
        shared_->archives.push_back(Archive::installer(path));
        shared_->sources.emplace_back(path, true);
    }
    // The same archives for another thread. StormLib's handles aren't safe
    // to use from two threads at once, and opening every archive again
    // costs its tables again (9 MB for the game's six), so the copy shares
    // the handles and every read takes the stack's lock.
    [[nodiscard]] Stack reopen() const { return *this; }

    [[nodiscard]] bool empty() const noexcept { return shared_->archives.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return shared_->archives.size(); }

    [[nodiscard]] bool contains(std::string_view name) const {
        const std::scoped_lock lock(shared_->mutex);
        for (const auto& archive : shared_->archives) if (archive.contains(name)) return true;
        return false;
    }

    [[nodiscard]] std::vector<std::byte> read(std::string_view name) const {
        auto data = try_read(name);
        if (!data) throw std::runtime_error((shared_->failed.contains(Archive::key(name)) ? "MPQ read failed: " : "MPQ stack: file not found: ")
                                            + std::string(name));
        return std::move(*data);
    }

    // First archive that has the file wins. An installer's delta entry is
    // applied to the file from the archives below it; when that base
    // doesn't match (CRC/size), the lookup falls through to the base unless
    // strict_installer is set for patch generation.
    // A file that's there but won't read (a damaged MPQ) is logged once and
    // from then on is missing: no retry each time a sound or frame wants it.
    [[nodiscard]] std::optional<std::vector<std::byte>>
    try_read(std::string_view name, bool strict_installer = false) const {
        using Clock = std::chrono::steady_clock;
        const auto asked = Clock::now();
        const bool on_main = std::this_thread::get_id() == g_main_thread;
        // A worker lets a waiting main thread in first (bounded: a quarter second, never stuck behind it).
        const auto let_main_in = [] {
            for (int waited = 0; waited < 250 && g_main_waiting.load(std::memory_order_acquire) > 0; ++waited)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        };
        const bool urgent = on_main || t_urgent_reads;
        auto& waiting = on_main ? g_main_waiting : g_urgent_waiting;
        if (urgent) waiting.fetch_add(1, std::memory_order_acq_rel);
        else {
            let_main_in();
            for (int waited = 0; waited < 100 && g_urgent_waiting.load(std::memory_order_acquire) > 0; ++waited)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::unique_lock lock(shared_->mutex);
        if (urgent) waiting.fetch_sub(1, std::memory_order_acq_rel);
        const auto locked = Clock::now();
        struct Done {
            std::string_view name; bool on_main; Clock::time_point asked, locked;
            ~Done() {
                const auto ns = [](auto span) { return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(span).count()); };
                g_read_stats.add(on_main, name, ns(Clock::now() - asked), ns(locked - asked));
            }
        } done{ name, on_main, asked, locked };
        const std::function<void()> step_aside = [&] {
            if (g_main_waiting.load(std::memory_order_acquire) == 0) return;
            lock.unlock();
            let_main_in();
            lock.lock();
        };
        // Only the outermost read of this thread steps aside (the lock is recursive).
        static thread_local int depth = 0;
        struct Depth { const std::function<void()>* before; Depth(bool aside, const std::function<void()>* step) : before(t_between_pieces) { ++depth; if (aside && depth == 1) t_between_pieces = step; else t_between_pieces = nullptr; } ~Depth() { --depth; t_between_pieces = before; } } piece_guard(!on_main, &step_aside);
        if (shared_->failed.contains(Archive::key(name))) return std::nullopt;
        try {
            return find(name, strict_installer);
        } catch (const InstallerMismatch& error) {   // not damage: a later plain read still gets the base
            d2d::log::warn("{}.", error.what());
            return std::nullopt;
        } catch (const std::exception& error) {
            shared_->failed.insert(Archive::key(name));
            d2d::log::warn("{}. A damaged MPQ? Run Verify files in the launcher. Not read again this run.", error.what());
            return std::nullopt;
        }
    }

    // There but unreadable: try_read logged it already.
    [[nodiscard]] bool failed(std::string_view name) const { const std::scoped_lock lock(shared_->mutex); return shared_->failed.contains(Archive::key(name)); }

    // First plain archive that has the file, for streaming. The File reads
    // outside the stack's lock: only where no other thread reads this stack.
    [[nodiscard]] std::optional<File> open(std::string_view name) const {
        const std::scoped_lock lock(shared_->mutex);
        for (const auto& archive : shared_->archives)
            if (auto file = archive.open(name)) return file;
        return std::nullopt;
    }

private:
    // A strict read's listed delta that doesn't apply; the files themselves read fine.
    struct InstallerMismatch : std::runtime_error { using std::runtime_error::runtime_error; };

    [[nodiscard]] std::optional<std::vector<std::byte>>
    find(std::string_view name, bool strict_installer) const {
        for (std::size_t i = 0; i < shared_->archives.size(); ++i) {
            const auto& archive = shared_->archives[i];
            if (auto data = archive.try_read(name)) return data;
            if (!archive.is_installer()) continue;
            const auto entry = archive.installer_entry(name);
            if (!entry) {
                if (strict_installer && archive.installer_lists(name)) throw InstallerMismatch("installer entry missing: " + std::string(name));
                continue;
            }
            for (std::size_t j = i + 1; j < shared_->archives.size(); ++j)
                if (auto base = shared_->archives[j].try_read(name)) {
                    if (auto out = bnpatch::apply(*entry, *base)) return out;
                    break;
                }
            if (strict_installer) throw InstallerMismatch("installer delta failed: " + std::string(name));
        }
        return std::nullopt;
    }

    struct Shared {
        std::recursive_mutex mutex;
        std::vector<Archive> archives;
        std::vector<std::pair<std::filesystem::path, bool>> sources;   // what was pushed: path, installer
        // Keys of files that failed to read.
        std::unordered_set<std::string> failed;
    };
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
};

}  // namespace d2d::mpq
