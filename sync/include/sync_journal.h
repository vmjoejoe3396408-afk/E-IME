#pragma once

#include "../../engine/include/zhuyin.h"
#include "../../engine/include/trie.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace eime {

// Each instance owns one file, including instances in different hosts on one PC.
// Readers merge increases in per-file, per-word totals, not mutable line offsets.
// Existing device_<hostname>.delta files remain readable (stop old writers first).
// One journal belongs to one trie and is used by one thread at a time.
class SyncJournal {
public:
    explicit SyncJournal(std::filesystem::path sync_dir, std::string custom_device_id = "")
        : sync_dir_(std::move(sync_dir)),
          device_id_(safe_device_id(custom_device_id.empty() ? detect_device_id() : custom_device_id)),
          writer_id_(new_writer_id()) {
        std::error_code ec;
        std::filesystem::create_directories(sync_dir_, ec);
    }

    SyncJournal(const SyncJournal&) = delete;
    SyncJournal& operator=(const SyncJournal&) = delete;

    const std::string& device_id() const { return device_id_; }
    const std::filesystem::path& sync_dir() const { return sync_dir_; }

    std::filesystem::path own_delta_path() const {
        return sync_dir_ / ("device_" + device_id_ + "_" + writer_id_ + ".delta");
    }

    bool record_learning(ZhuyinTrie& trie, const std::vector<Syllable>& syllables,
                         const std::string& word, uint32_t delta = 1) {
        if (syllables.empty() || word.empty() || delta == 0 ||
            word.find_first_of("\t\r\n") != std::string::npos) return false;

        std::string zhuyin;
        for (const auto& syllable : syllables) {
            if (!zhuyin.empty()) zhuyin += ' ';
            zhuyin += syllable.to_string();
        }
        const auto path = own_delta_path();
        std::ofstream out(path, std::ios::app | std::ios::binary);
        if (!out) return false;
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        out << now << '\t' << word << '\t' << zhuyin << '\t' << delta << '\n';
        out.close();
        if (!out) return false;

        // Only mark successfully persisted learning as applied to this trie.
        trie.apply_user_delta(syllables, word, delta);
        applied_totals_[path.filename().string()][word + "\t" + zhuyin] += delta;
        return true;
    }

    // Returns the number of word counters whose totals increased in this scan.
    size_t sync_all_devices(ZhuyinTrie& trie) {
        std::error_code ec;
        size_t merged = 0;
        std::filesystem::directory_iterator it(sync_dir_, ec), end;
        while (!ec && it != end) {
            const auto entry = *it;
            const auto path = entry.path();
            const auto filename = path.filename().string();
            std::error_code file_ec;
            if (entry.is_regular_file(file_ec) && filename.starts_with("device_") &&
                path.extension() == ".delta") {
                Totals totals;
                if (read_totals(path, totals)) {
                    auto& applied = applied_totals_[filename];
                    for (const auto& [key, value] : totals) {
                        auto& previous = applied[key];
                        if (value.total <= previous) continue;
                        const auto delta = static_cast<uint32_t>(std::min<uint64_t>(
                            value.total - previous, std::numeric_limits<uint32_t>::max()));
                        trie.apply_user_delta(value.syllables, value.word, delta);
                        previous = value.total;
                        ++merged;
                    }
                }
            }
            it.increment(ec);
        }
        return merged;
    }

    // Rewrite only this instance's file. Readers retain a cumulative high-water
    // mark even if a cloud client temporarily presents an older snapshot.
    bool compact_own_journal() {
        const auto path = own_delta_path();
        Totals totals;
        if (!read_totals(path, totals)) return false;
        auto temporary = path;
        temporary += ".tmp";
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << "# E-IME cumulative word totals; preserve this writer filename\n";
        for (const auto& [key, value] : totals) {
            out << value.latest_ts << '\t' << key << '\t' << value.total << '\n';
        }
        out.close();
        bool replaced = false;
        if (out) {
#ifdef _WIN32
            replaced = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
            std::error_code ec;
            std::filesystem::rename(temporary, path, ec);
            replaced = !ec;
#endif
        }
        if (!replaced) {
            std::error_code ec;
            std::filesystem::remove(temporary, ec);
        }
        return replaced;
    }

    static std::string detect_device_id() {
#ifdef _WIN32
        char buf[256] = {};
        DWORD size = sizeof(buf);
        if (GetComputerNameA(buf, &size) && size > 0) return safe_device_id(std::string(buf, size));
#endif
        return "default_host";
    }

private:
    struct Total {
        uint64_t latest_ts = 0;
        uint64_t total = 0;
        std::string word;
        std::vector<Syllable> syllables;
    };
    using Totals = std::unordered_map<std::string, Total>;

    static std::string safe_device_id(std::string value) {
        for (char& c : value) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_')) c = '_';
        }
        return value.empty() ? "default_host" : value;
    }

    static std::string new_writer_id() {
        std::random_device random;
        std::ostringstream id;
        id << std::hex << std::setfill('0');
        for (int i = 0; i < 4; ++i) id << std::setw(8) << static_cast<uint32_t>(random());
        return id.str();
    }

    static bool parse_uint(const std::string& text, uint64_t& value) {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }

    static bool read_totals(const std::filesystem::path& path, Totals& totals) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::string line;
        while (std::getline(in, line)) {
            // Revisit an incomplete append/cloud transfer once its newline exists.
            if (in.eof()) break;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.front() == '#') continue;
            std::istringstream fields(line);
            std::string timestamp, word, zhuyin, count;
            uint64_t ts = 0, delta = 0;
            if (!std::getline(fields, timestamp, '\t') || !std::getline(fields, word, '\t') ||
                !std::getline(fields, zhuyin, '\t') || !std::getline(fields, count) ||
                word.empty() || !parse_uint(timestamp, ts) || !parse_uint(count, delta) || delta == 0) continue;
            auto syllables = ZhuyinParser::parse_multi_syllables(zhuyin);
            if (syllables.empty()) continue;
            auto& value = totals[word + "\t" + zhuyin];
            if (delta > std::numeric_limits<uint64_t>::max() - value.total) return false;
            value.latest_ts = std::max(value.latest_ts, ts);
            value.total += delta;
            value.word = std::move(word);
            value.syllables = std::move(syllables);
        }
        return !in.bad();
    }

    std::filesystem::path sync_dir_;
    std::string device_id_;
    std::string writer_id_;
    std::unordered_map<std::string, std::unordered_map<std::string, uint64_t>> applied_totals_;
};

} // namespace eime
