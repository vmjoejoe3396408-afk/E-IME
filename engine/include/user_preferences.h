#pragma once
#include "zhuyin.h"
#include <map>
#include <set>
#include <sstream>
#include <algorithm>

namespace eime {
using UserWordKey = std::pair<std::string, std::string>; // reading, text
struct UserPreferences {
    std::set<UserWordKey> words;
    std::map<std::string, std::vector<std::string>> ranks;
};
struct WordReading {
    std::string text;
    std::string reading;
};
inline std::string reading_for(const std::vector<Syllable>& syllables, size_t start, size_t length) {
    if (start > syllables.size() || length > syllables.size() - start) return {};
    std::string result;
    for (size_t i = start; i < start + length; ++i) {
        if (!syllables[i].has_phonetic_body()) return {};
        if (!result.empty()) result += ' ';
        result += syllables[i].to_string();
    }
    return result;
}
enum class RankAction { Top, Up, Down, Reset };

inline size_t unicode_length(const std::string& text) {
    size_t count = 0;
    for (size_t i = 0; i < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[i++]);
        unsigned code = first;
        int trailing = 0;
        if (first < 0x20 || first == 0x7f) return 0;
        if (first >= 0xc2 && first <= 0xdf) { code = first & 0x1f; trailing = 1; }
        else if (first >= 0xe0 && first <= 0xef) { code = first & 0x0f; trailing = 2; }
        else if (first >= 0xf0 && first <= 0xf4) { code = first & 7; trailing = 3; }
        else if (first >= 0x80) return 0;
        if (i + trailing > text.size()) return 0;
        for (int j = 0; j < trailing; ++j) {
            unsigned char next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return 0;
            code = (code << 6) | (next & 0x3f);
        }
        if ((trailing == 1 && code < 0x80) || (trailing == 2 && code < 0x800) ||
            (trailing == 3 && code < 0x10000) || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
        ++count;
    }
    return count;
}
inline std::string canonical_reading(const std::string& reading) {
    if (reading.find_first_of("\t\r\n") != std::string::npos) return {};
    std::istringstream input(reading);
    std::string token, result;
    size_t count = 0;
    while (input >> token) {
        const auto syllable = ZhuyinParser::parse_zhuyin_string(token);
        if (!syllable.has_phonetic_body() || syllable.to_string() != token || ++count > 64) return {};
        if (!result.empty()) result += ' ';
        result += token;
    }
    return result;
}
inline bool valid_user_word(const std::string& reading, const std::string& text) {
    const auto normalized = canonical_reading(reading);
    const auto count = unicode_length(text);
    return !normalized.empty() && count > 0 && count <= 64 &&
        count == 1 + static_cast<size_t>(std::count(normalized.begin(), normalized.end(), ' '));
}
}
