#pragma once

#include "../../engine/include/zhuyin.h"
#include "../../engine/include/trie.h"
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <unordered_map>
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace eime {

// 無鎖跨裝置雲端同步日誌引擎 (Per-Device Append-Only Delta Journal)
// 原理：每台 Windows 裝置僅寫入自己的 `device_<id>.delta`，讀取時聚合目錄下所有裝置的增量檔。
// 徹底避免 OneDrive / Google Drive / Dropbox 多機同步時的檔案鎖定與衝突副本問題。
class SyncJournal {
public:
    explicit SyncJournal(std::filesystem::path sync_dir, std::string custom_device_id = "")
        : sync_dir_(std::move(sync_dir)),
          device_id_(custom_device_id.empty() ? detect_device_id() : std::move(custom_device_id)) {
        std::error_code ec;
        std::filesystem::create_directories(sync_dir_, ec);
    }

    const std::string& device_id() const { return device_id_; }
    const std::filesystem::path& sync_dir() const { return sync_dir_; }

    std::filesystem::path own_delta_path() const {
        return sync_dir_ / ("device_" + device_id_ + ".delta");
    }

    // 記錄使用者選字學習：即時更新記憶體字典樹 + Append 寫入本機專屬增量日誌
    bool record_learning(
        ZhuyinTrie& trie,
        const std::vector<Syllable>& syllables,
        const std::string& word,
        uint32_t delta = 1
    ) {
        if (syllables.empty() || word.empty() || delta == 0) return false;

        // 1. 立即更新記憶體內 Trie 權重
        trie.apply_user_delta(syllables, word, delta);

        // 2. 序列化注音字串
        std::string zhuyin_str;
        for (size_t i = 0; i < syllables.size(); ++i) {
            if (i > 0) zhuyin_str += ' ';
            zhuyin_str += syllables[i].to_string();
        }

        // 3. Append-Only 寫入本機專屬 .delta 檔
        auto path = own_delta_path();
        std::ofstream out(path, std::ios::app);
        if (!out.is_open()) {
            return false;
        }

        auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        out << now_sec << '\t' << word << '\t' << zhuyin_str << '\t' << delta << '\n';
        out.flush();

        // 更新已讀行數避免下次 sync 重複計算自己剛寫入的那一行
        std::string filename = path.filename().string();
        synced_lines_per_file_[filename]++;
        return true;
    }

    // 掃描同步目錄下所有設備 (device_*.delta) 的新紀錄並無衝突合併至 Trie
    // 回傳本次新合併的增量筆數
    size_t sync_all_devices(ZhuyinTrie& trie) {
        std::error_code ec;
        if (!std::filesystem::exists(sync_dir_, ec)) return 0;

        size_t newly_merged = 0;
        for (const auto& entry : std::filesystem::directory_iterator(sync_dir_, ec)) {
            if (!entry.is_regular_file()) continue;
            auto path = entry.path();
            if (path.extension() != ".delta") continue;

            std::string filename = path.filename().string();
            size_t already_read_lines = synced_lines_per_file_[filename];

            std::ifstream in(path);
            if (!in.is_open()) continue;

            std::string line;
            size_t current_line = 0;
            while (std::getline(in, line)) {
                current_line++;
                if (current_line <= already_read_lines) continue;

                if (line.empty() || line[0] == '#') continue;
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                    line.pop_back();
                }

                std::stringstream ss(line);
                std::string ts_str, word, zhuyin_seq, delta_str;
                if (std::getline(ss, ts_str, '\t') &&
                    std::getline(ss, word, '\t') &&
                    std::getline(ss, zhuyin_seq, '\t') &&
                    std::getline(ss, delta_str, '\t')) {

                    uint32_t delta = 1;
                    try { delta = static_cast<uint32_t>(std::stoul(delta_str)); } catch (...) {}

                    auto syllables = ZhuyinParser::parse_multi_syllables(zhuyin_seq);
                    if (!syllables.empty()) {
                        trie.apply_user_delta(syllables, word, delta);
                        newly_merged++;
                    }
                }
            }
            synced_lines_per_file_[filename] = current_line;
        }
        return newly_merged;
    }

    // 本機日誌壓縮 (Compaction)：將本機自己的 .delta 內重複詞彙合併累加，保持檔案極小化
    bool compact_own_journal() {
        auto path = own_delta_path();
        std::ifstream in(path);
        if (!in.is_open()) return false;

        struct AggEntry {
            int64_t latest_ts = 0;
            std::string word;
            std::string zhuyin_seq;
            uint32_t total_delta = 0;
        };
        std::unordered_map<std::string, AggEntry> aggregated;

        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();

            std::stringstream ss(line);
            std::string ts_str, word, zhuyin_seq, delta_str;
            if (std::getline(ss, ts_str, '\t') &&
                std::getline(ss, word, '\t') &&
                std::getline(ss, zhuyin_seq, '\t') &&
                std::getline(ss, delta_str, '\t')) {
                int64_t ts = 0;
                uint32_t d = 1;
                try { ts = std::stoll(ts_str); d = static_cast<uint32_t>(std::stoul(delta_str)); } catch (...) {}

                std::string key = word + "\t" + zhuyin_seq;
                auto& agg = aggregated[key];
                agg.latest_ts = std::max(agg.latest_ts, ts);
                agg.word = word;
                agg.zhuyin_seq = zhuyin_seq;
                agg.total_delta += d;
            }
        }
        in.close();

        std::ofstream out(path, std::ios::trunc);
        if (!out.is_open()) return false;

        out << "# E-IME Per-Device Compacted Delta Journal (" << device_id_ << ")\n";
        size_t lines_written = 1;
        for (const auto& [k, agg] : aggregated) {
            out << agg.latest_ts << '\t' << agg.word << '\t' << agg.zhuyin_seq << '\t' << agg.total_delta << '\n';
            lines_written++;
        }
        out.flush();
        synced_lines_per_file_[path.filename().string()] = lines_written;
        return true;
    }

    static std::string detect_device_id() {
#ifdef _WIN32
        char buf[256] = {0};
        DWORD size = sizeof(buf);
        if (GetComputerNameA(buf, &size) && size > 0) {
            std::string name(buf, size);
            for (char& c : name) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
            }
            return name;
        }
#endif
        return "win_default_host";
    }

private:
    std::filesystem::path sync_dir_;
    std::string device_id_;
    std::unordered_map<std::string, size_t> synced_lines_per_file_;
};

} // namespace eime
