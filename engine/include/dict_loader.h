#pragma once

#include "trie.h"
#include "zhuyin.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <vector>
#include <cstring>

namespace eime {

struct RawDictEntry {
    std::string word;
    std::vector<Syllable> syllables;
    uint32_t freq = 0;
};

class DictLoader {
public:
    static constexpr char kBinaryMagic[8] = {'E', 'M', 'S', 'I', 'B', 'I', 'N', '1'};

    static bool save_to_binary(const std::filesystem::path& bin_path, const std::vector<RawDictEntry>& entries) {
        try {
            std::ofstream out(bin_path, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) return false;

            out.write(kBinaryMagic, 8);
            uint32_t count = static_cast<uint32_t>(entries.size());
            out.write(reinterpret_cast<const char*>(&count), sizeof(count));

            auto write_str = [&](const std::string& s) {
                uint8_t len = static_cast<uint8_t>(std::min<size_t>(s.size(), 255));
                out.write(reinterpret_cast<const char*>(&len), sizeof(len));
                if (len > 0) out.write(s.data(), len);
            };

            for (const auto& e : entries) {
                uint16_t w_len = static_cast<uint16_t>(e.word.size());
                out.write(reinterpret_cast<const char*>(&w_len), sizeof(w_len));
                if (w_len > 0) out.write(e.word.data(), w_len);

                uint8_t syl_count = static_cast<uint8_t>(e.syllables.size());
                out.write(reinterpret_cast<const char*>(&syl_count), sizeof(syl_count));
                for (const auto& s : e.syllables) {
                    write_str(s.consonant);
                    write_str(s.medial);
                    write_str(s.rhyme);
                    uint8_t tone_val = static_cast<uint8_t>(s.tone);
                    out.write(reinterpret_cast<const char*>(&tone_val), sizeof(tone_val));
                }
                out.write(reinterpret_cast<const char*>(&e.freq), sizeof(e.freq));
            }
            return out.good();
        } catch (...) {
            return false;
        }
    }

    static bool load_from_binary(const std::filesystem::path& bin_path, ZhuyinTrie& trie, bool verbose = true) {
        try {
            std::ifstream in(bin_path, std::ios::binary);
            if (!in.is_open()) return false;

            char magic[8] = {0};
            in.read(magic, 8);
            if (std::memcmp(magic, kBinaryMagic, 8) != 0) return false;

            uint32_t count = 0;
            in.read(reinterpret_cast<char*>(&count), sizeof(count));
            if (!in.good()) return false;

            auto read_str = [&](std::string& s) {
                uint8_t len = 0;
                in.read(reinterpret_cast<char*>(&len), sizeof(len));
                s.resize(len);
                if (len > 0) in.read(&s[0], len);
            };

            for (uint32_t i = 0; i < count; ++i) {
                uint16_t w_len = 0;
                in.read(reinterpret_cast<char*>(&w_len), sizeof(w_len));
                std::string word(w_len, '\0');
                if (w_len > 0) in.read(&word[0], w_len);

                uint8_t syl_count = 0;
                in.read(reinterpret_cast<char*>(&syl_count), sizeof(syl_count));
                std::vector<Syllable> syllables;
                syllables.reserve(syl_count);
                for (uint8_t s = 0; s < syl_count; ++s) {
                    Syllable syl;
                    read_str(syl.consonant);
                    read_str(syl.medial);
                    read_str(syl.rhyme);
                    uint8_t tone_val = 0;
                    in.read(reinterpret_cast<char*>(&tone_val), sizeof(tone_val));
                    syl.tone = static_cast<Tone>(tone_val);
                    syllables.push_back(std::move(syl));
                }

                uint32_t freq = 0;
                in.read(reinterpret_cast<char*>(&freq), sizeof(freq));

                if (!syllables.empty()) {
                    trie.insert(syllables, word, freq);
                }
            }

            trie.finalize_probabilities();
            if (verbose) {
                std::cout << "[eime::DictLoader] 成功自二進位快取快速載入 " << count << " 條詞彙 ("
                          << bin_path << ", 累計總詞數: " << trie.word_count() << ")" << std::endl;
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    static bool load_from_file(const std::filesystem::path& filepath, ZhuyinTrie& trie, bool verbose = true) {
        namespace fs = std::filesystem;
        std::error_code ec;

        // Preserve the native wide Windows path when opening dictionaries and caches.
        fs::path bin_path = filepath;
        bin_path += ".bin";
        if (fs::exists(bin_path, ec) && fs::exists(filepath, ec)) {
            auto bin_time = fs::last_write_time(bin_path, ec);
            auto txt_time = fs::last_write_time(filepath, ec);
            if (!ec && bin_time >= txt_time) {
                if (load_from_binary(bin_path, trie, verbose)) {
                    return true;
                }
            }
        }

        std::ifstream file(filepath);
        if (!file.is_open()) {
            if (verbose) {
                std::cerr << "[eime::DictLoader] 無法開啟詞庫檔案: " << filepath << std::endl;
            }
            return false;
        }

        std::string line;
        size_t count = 0;
        std::vector<RawDictEntry> entries;
        entries.reserve(120000);

        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;

            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                line.pop_back();
            }

            std::stringstream ss(line);
            std::string word;
            std::string zhuyin_seq;
            std::string freq_str;

            if (std::getline(ss, word, '\t') &&
                std::getline(ss, zhuyin_seq, '\t')) {

                uint32_t freq = 500000;
                if (std::getline(ss, freq_str, '\t') && !freq_str.empty()) {
                    try {
                        freq = static_cast<uint32_t>(std::stoul(freq_str));
                    } catch (...) {
                        freq = 500000;
                    }
                }

                auto syllables = ZhuyinParser::parse_multi_syllables(zhuyin_seq);
                if (!syllables.empty()) {
                    trie.insert(syllables, word, freq);
                    entries.push_back({word, syllables, freq});
                    count++;
                }
            }
        }

        trie.finalize_probabilities();
        if (verbose) {
            std::cout << "[eime::DictLoader] 成功載入 " << count << " 條詞彙 (" << filepath
                      << ", 累計總詞數: " << trie.word_count() << ")" << std::endl;
        }

        // 產生高速二進位快取供下次冷啟動使用
        if (!entries.empty()) {
            save_to_binary(bin_path, entries);
        }

        return true;
    }
};

} // namespace eime
