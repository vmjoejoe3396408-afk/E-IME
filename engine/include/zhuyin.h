#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <string_view>
#include <cctype>

namespace eime {

enum class Tone : uint8_t {
    None  = 0,
    Tone1 = 1, // 陰平 (一聲 / 空白)
    Tone2 = 2, // 陽平 (二聲 ˊ) - key '6'
    Tone3 = 3, // 上聲 (三聲 ˇ) - key '3'
    Tone4 = 4, // 去聲 (四聲 ˋ) - key '4'
    Tone5 = 5  // 輕聲 (五聲 ˙) - key '7'
};

// 注音音節 (一個中文字對應一個音節)
struct Syllable {
    std::string consonant; // 聲母: ㄅㄆㄇㄈㄉㄊㄋㄌㄍㄎㄏㄐㄑㄒㄓㄔㄕㄖㄗㄘㄙ
    std::string medial;    // 介音: ㄧㄨㄩ
    std::string rhyme;     // 韻母: ㄚㄛㄜㄝㄞㄟㄠㄡㄢㄣㄤㄥㄦ
    Tone tone = Tone::None;

    bool is_empty() const {
        return consonant.empty() && medial.empty() && rhyme.empty() && tone == Tone::None;
    }

    bool has_phonetic_body() const {
        return !consonant.empty() || !medial.empty() || !rhyme.empty();
    }

    // 格式化為標準注音字串，例如 "ㄅㄧㄢˋ"
    std::string to_string() const {
        std::string res = consonant + medial + rhyme;
        switch (tone) {
            case Tone::Tone2: res += "ˊ"; break;
            case Tone::Tone3: res += "ˇ"; break;
            case Tone::Tone4: res += "ˋ"; break;
            case Tone::Tone5: res += "˙"; break;
            case Tone::Tone1:
            default: break;
        }
        return res;
    }

    // 不含聲調的聲韻字串 (用於聲調模糊匹配或簡拼輔助)
    std::string body_string() const {
        return consonant + medial + rhyme;
    }

    // 計算此音節在 UTF-16/寬字元中的字元長度 (聲母+介音+韻母+聲調符號)
    size_t symbol_count() const {
        size_t count = 0;
        if (!consonant.empty()) count++;
        if (!medial.empty()) count++;
        if (!rhyme.empty()) count++;
        if (tone >= Tone::Tone2 && tone <= Tone::Tone5) count++;
        return count;
    }

    bool operator==(const Syllable& other) const {
        return consonant == other.consonant &&
               medial == other.medial &&
               rhyme == other.rhyme &&
               tone == other.tone;
    }
};

// 標準大千鍵盤解析器 (MS-IME Standard Daqian Layout)
class ZhuyinParser {
public:
    static bool is_daqian_key(char c) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        static const std::string valid = "1234567890-qwertyuiopasdfghjkl;zxcvbnm,./ ";
        return valid.find(c) != std::string::npos;
    }

    struct KeyMapping {
        enum class Category { Consonant, Medial, Rhyme, Tone, Unknown };
        Category category = Category::Unknown;
        std::string symbol;
        Tone tone = Tone::None;
    };

    static KeyMapping map_key(char c) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        KeyMapping km;

        switch (c) {
            // 聲母 (21)
            case '1': km = {KeyMapping::Category::Consonant, "ㄅ", Tone::None}; break;
            case 'q': km = {KeyMapping::Category::Consonant, "ㄆ", Tone::None}; break;
            case 'a': km = {KeyMapping::Category::Consonant, "ㄇ", Tone::None}; break;
            case 'z': km = {KeyMapping::Category::Consonant, "ㄈ", Tone::None}; break;
            case '2': km = {KeyMapping::Category::Consonant, "ㄉ", Tone::None}; break;
            case 'w': km = {KeyMapping::Category::Consonant, "ㄊ", Tone::None}; break;
            case 's': km = {KeyMapping::Category::Consonant, "ㄋ", Tone::None}; break;
            case 'x': km = {KeyMapping::Category::Consonant, "ㄌ", Tone::None}; break;
            case 'e': km = {KeyMapping::Category::Consonant, "ㄍ", Tone::None}; break;
            case 'd': km = {KeyMapping::Category::Consonant, "ㄎ", Tone::None}; break;
            case 'c': km = {KeyMapping::Category::Consonant, "ㄏ", Tone::None}; break;
            case 'r': km = {KeyMapping::Category::Consonant, "ㄐ", Tone::None}; break;
            case 'f': km = {KeyMapping::Category::Consonant, "ㄑ", Tone::None}; break;
            case 'v': km = {KeyMapping::Category::Consonant, "ㄒ", Tone::None}; break;
            case '5': km = {KeyMapping::Category::Consonant, "ㄓ", Tone::None}; break;
            case 't': km = {KeyMapping::Category::Consonant, "ㄔ", Tone::None}; break;
            case 'g': km = {KeyMapping::Category::Consonant, "ㄕ", Tone::None}; break;
            case 'b': km = {KeyMapping::Category::Consonant, "ㄖ", Tone::None}; break;
            case 'y': km = {KeyMapping::Category::Consonant, "ㄗ", Tone::None}; break;
            case 'h': km = {KeyMapping::Category::Consonant, "ㄘ", Tone::None}; break;
            case 'n': km = {KeyMapping::Category::Consonant, "ㄙ", Tone::None}; break;

            // 介音 (3)
            case 'u': km = {KeyMapping::Category::Medial, "ㄧ", Tone::None}; break;
            case 'j': km = {KeyMapping::Category::Medial, "ㄨ", Tone::None}; break;
            case 'm': km = {KeyMapping::Category::Medial, "ㄩ", Tone::None}; break;

            // 韻母 (13)
            case '8': km = {KeyMapping::Category::Rhyme, "ㄚ", Tone::None}; break;
            case 'i': km = {KeyMapping::Category::Rhyme, "ㄛ", Tone::None}; break;
            case 'k': km = {KeyMapping::Category::Rhyme, "ㄜ", Tone::None}; break;
            case ',': km = {KeyMapping::Category::Rhyme, "ㄝ", Tone::None}; break;
            case '9': km = {KeyMapping::Category::Rhyme, "ㄞ", Tone::None}; break;
            case 'o': km = {KeyMapping::Category::Rhyme, "ㄟ", Tone::None}; break;
            case 'l': km = {KeyMapping::Category::Rhyme, "ㄠ", Tone::None}; break;
            case '.': km = {KeyMapping::Category::Rhyme, "ㄡ", Tone::None}; break;
            case '0': km = {KeyMapping::Category::Rhyme, "ㄢ", Tone::None}; break;
            case 'p': km = {KeyMapping::Category::Rhyme, "ㄣ", Tone::None}; break;
            case ';': km = {KeyMapping::Category::Rhyme, "ㄤ", Tone::None}; break;
            case '/': km = {KeyMapping::Category::Rhyme, "ㄥ", Tone::None}; break;
            case '-': km = {KeyMapping::Category::Rhyme, "ㄦ", Tone::None}; break;

            // 聲調 (5)
            case ' ': km = {KeyMapping::Category::Tone, " ", Tone::Tone1}; break;
            case '6': km = {KeyMapping::Category::Tone, "ˊ", Tone::Tone2}; break;
            case '3': km = {KeyMapping::Category::Tone, "ˇ", Tone::Tone3}; break;
            case '4': km = {KeyMapping::Category::Tone, "ˋ", Tone::Tone4}; break;
            case '7': km = {KeyMapping::Category::Tone, "˙", Tone::Tone5}; break;

            default: break;
        }
        return km;
    }

    // 從純注音字串解析出 Syllable
    static Syllable parse_zhuyin_string(std::string_view str) {
        Syllable s;
        size_t i = 0;
        while (i < str.size()) {
            size_t char_len = 1;
            unsigned char lead = static_cast<unsigned char>(str[i]);
            if ((lead & 0xE0) == 0xE0) char_len = 3;
            else if ((lead & 0xC0) == 0xC0) char_len = 2;
            else if ((lead & 0xF0) == 0xF0) char_len = 4;

            std::string sym(str.substr(i, char_len));
            i += char_len;

            if (is_consonant(sym)) {
                s.consonant = sym;
            } else if (is_medial(sym)) {
                s.medial = sym;
            } else if (is_rhyme(sym)) {
                s.rhyme = sym;
            } else if (sym == "ˊ") {
                s.tone = Tone::Tone2;
            } else if (sym == "ˇ") {
                s.tone = Tone::Tone3;
            } else if (sym == "ˋ") {
                s.tone = Tone::Tone4;
            } else if (sym == "˙") {
                s.tone = Tone::Tone5;
            } else if (sym == " " || sym == "ˉ") {
                s.tone = Tone::Tone1;
            }
        }
        if (s.tone == Tone::None && s.has_phonetic_body()) {
            s.tone = Tone::Tone1;
        }
        return s;
    }

    // 解析以空白分隔的多音節注音字串 (如 "ㄊㄞˊ ㄐㄧ ㄉㄧㄢˋ")
    static std::vector<Syllable> parse_multi_syllables(std::string_view zhuyin_seq) {
        std::vector<Syllable> res;
        size_t start = 0;
        while (start < zhuyin_seq.size()) {
            while (start < zhuyin_seq.size() && zhuyin_seq[start] == ' ') ++start;
            if (start >= zhuyin_seq.size()) break;
            size_t end = zhuyin_seq.find(' ', start);
            if (end == std::string_view::npos) end = zhuyin_seq.size();
            auto syl = parse_zhuyin_string(zhuyin_seq.substr(start, end - start));
            if (!syl.is_empty()) {
                res.push_back(syl);
            }
            start = end + 1;
        }
        return res;
    }

    // 全形標點符號轉換 (MS-IME Ctrl+標點 或 前導引號)
    static std::optional<std::string> map_chinese_punctuation(char c) {
        switch (c) {
            case ',': return "，";
            case '.': return "。";
            case '?': return "？";
            case '/': return "？";
            case '!': return "！";
            case ':': return "：";
            case ';': return "；";
            case '[': return "「";
            case ']': return "」";
            case '{': return "『";
            case '}': return "』";
            case '(': return "（";
            case ')': return "）";
            case '\\': return "、";
            case '\'': return "、";
            default: return std::nullopt;
        }
    }

    // 取得標點符號的候選替換清單 (如同微軟新注音在標點處按向下鍵展開候選)
    static std::vector<std::string> get_punctuation_candidates(std::string_view punct) {
        if (punct == "，" || punct == "、" || punct == "﹐" || punct == ",") {
            if (punct == "、") return {"、", "，", "﹐", ","};
            return {"，", "、", "﹐", ","};
        }
        if (punct == "。" || punct == "." || punct == "．" || punct == "｡") {
            return {"。", "．", ".", "｡"};
        }
        if (punct == "？" || punct == "?" || punct == "﹖") {
            return {"？", "?", "﹖"};
        }
        if (punct == "！" || punct == "!" || punct == "﹗") {
            return {"！", "!", "﹗"};
        }
        if (punct == "：" || punct == ":" || punct == "﹕") {
            return {"：", ":", "﹕"};
        }
        if (punct == "；" || punct == ";" || punct == "﹔") {
            return {"；", ";", "﹔"};
        }
        if (punct == "「" || punct == "『" || punct == "“" || punct == "‘" || punct == "\"" || punct == "'") {
            return {"「", "『", "“", "‘", "\"", "'"};
        }
        if (punct == "」" || punct == "』" || punct == "”" || punct == "’") {
            return {"」", "』", "”", "’", "\"", "'"};
        }
        if (punct == "（" || punct == "(" || punct == "〔" || punct == "【" || punct == "《" || punct == "〈" || punct == "{") {
            return {"（", "(", "〔", "【", "《", "〈", "{"};
        }
        if (punct == "）" || punct == ")" || punct == "〕" || punct == "】" || punct == "》" || punct == "〉" || punct == "}") {
            return {"）", ")", "〕", "】", "》", "〉", "}"};
        }
        return {std::string(punct)};
    }

    // 檢查是否為可單獨成音的獨立聲母 (ㄓㄔㄕㄖㄗㄘㄙ 帶有空韻 [ɨ])
    static bool is_independent_consonant(std::string_view s) {
        return s == "ㄓ" || s == "ㄔ" || s == "ㄕ" || s == "ㄖ" ||
               s == "ㄗ" || s == "ㄘ" || s == "ㄙ";
    }

private:
    static bool is_consonant(const std::string& s) {
        static const std::string c = "ㄅㄆㄇㄈㄉㄊㄋㄌㄍㄎㄏㄐㄑㄒㄓㄔㄕㄖㄗㄘㄙ";
        return c.find(s) != std::string::npos;
    }
    static bool is_medial(const std::string& s) {
        static const std::string m = "ㄧㄨㄩ";
        return m.find(s) != std::string::npos;
    }
    static bool is_rhyme(const std::string& s) {
        static const std::string r = "ㄚㄛㄜㄝㄞㄟㄠㄡㄢㄣㄤㄥㄦ";
        return r.find(s) != std::string::npos;
    }
};

} // namespace eime
