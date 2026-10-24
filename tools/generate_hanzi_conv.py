"""
產生超高速繁簡轉換標頭檔 (engine/include/hanzi_conv.h)
資料來源：OpenCC 官方固定版本 TSCharacters.txt
"""

import urllib.request
import sys
import argparse
from pathlib import Path

UPSTREAM_REVISION = "bcf847cf3fbd79c74779567f9a491ea8ab878183"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent.parent / "engine" / "include" / "hanzi_conv.h")
    args = parser.parse_args()
    url = f"https://raw.githubusercontent.com/BYVoid/OpenCC/{UPSTREAM_REVISION}/data/dictionary/TSCharacters.txt"
    print(f"正在從 {url} 下載 OpenCC 繁簡對照表...")
    try:
        data = urllib.request.urlopen(url, timeout=15).read().decode("utf-8")
    except Exception as e:
        print(f"下載失敗: {e}")
        return 1

    t2s_pairs = []
    for line in data.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) >= 2:
            trad = parts[0].strip()
            # 若有多個簡體，取第一個主要簡體字
            simp = parts[1].split(" ")[0].strip()
            if len(trad) == 1 and len(simp) == 1:
                t2s_pairs.append((trad, simp))

    # 去重並依繁體字 Unicode 排序
    t2s_dict = dict(t2s_pairs)
    sorted_pairs = sorted(t2s_dict.items(), key=lambda x: ord(x[0]))
    print(f"成功整理 {len(sorted_pairs)} 組繁簡對應字！")

    out_file = args.output
    out_file.parent.mkdir(parents=True, exist_ok=True)

    with open(out_file, "w", encoding="utf-8", newline="\n") as f:
        f.write("// Mapping data derived from OpenCC, Apache-2.0.\n")
        f.write("// Copyright (c) 2010-2023 BYVoid and OpenCC Contributors\n")
        f.write(f"// Source revision: {UPSTREAM_REVISION}\n")
        f.write("// Modified by E-IME contributors, 2026-10-24: select first single-character mapping and sort by Unicode.\n")
        f.write("// E-IME conversion code is MIT licensed. See THIRD_PARTY_NOTICES.md and LICENSES/.\n")
        f.write("#pragma once\n\n")
        f.write("#include <string>\n")
        f.write("#include <string_view>\n")
        f.write("#include <vector>\n")
        f.write("#include <algorithm>\n")
        f.write("#include <cstdint>\n\n")
        f.write("namespace eime {\n\n")
        f.write("enum class OutputCharset : uint8_t {\n")
        f.write("    Traditional, // 繁體中文 (預設)\n")
        f.write("    Simplified   // 簡體中文\n")
        f.write("};\n\n")
        f.write("class HanziConverter {\n")
        f.write("public:\n")
        f.write("    struct CharPair {\n")
        f.write("        char32_t trad;\n")
        f.write("        char32_t simp;\n")
        f.write("        bool operator<(const CharPair& other) const { return trad < other.trad; }\n")
        f.write("        bool operator<(char32_t code) const { return trad < code; }\n")
        f.write("    };\n\n")

        # 寫入靜態排序表
        f.write("    static const std::vector<CharPair>& get_t2s_table() {\n")
        f.write(f"        static const std::vector<CharPair> table = {{\n")
        for trad, simp in sorted_pairs:
            f.write(f"            {{0x{ord(trad):04X}, 0x{ord(simp):04X}}}, // {trad} -> {simp}\n")
        f.write("        };\n")
        f.write("        return table;\n")
        f.write("    }\n\n")

        # 繁體轉簡體函式
        f.write("""    static std::string to_simplified(std::string_view utf8_str) {
        if (utf8_str.empty()) return "";
        const auto& table = get_t2s_table();
        std::string result;
        result.reserve(utf8_str.size());

        size_t i = 0;
        while (i < utf8_str.size()) {
            size_t len = 1;
            char32_t codepoint = 0;
            unsigned char c = static_cast<unsigned char>(utf8_str[i]);

            if (c < 0x80) {
                codepoint = c;
                len = 1;
            } else if ((c & 0xE0) == 0xC0) {
                codepoint = c & 0x1F;
                len = 2;
            } else if ((c & 0xF0) == 0xE0) {
                codepoint = c & 0x0F;
                len = 3;
            } else if ((c & 0xF8) == 0xF0) {
                codepoint = c & 0x07;
                len = 4;
            }

            for (size_t k = 1; k < len && i + k < utf8_str.size(); ++k) {
                codepoint = (codepoint << 6) | (static_cast<unsigned char>(utf8_str[i + k]) & 0x3F);
            }

            // 二分搜尋尋找對應簡體字
            auto it = std::lower_bound(table.begin(), table.end(), codepoint,
                [](const CharPair& pair, char32_t val) { return pair.trad < val; });

            if (it != table.end() && it->trad == codepoint) {
                append_utf8(result, it->simp);
            } else {
                result.append(utf8_str.substr(i, len));
            }
            i += len;
        }
        return result;
    }

private:
    static void append_utf8(std::string& out, char32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
};

} // namespace eime
""")

    print(f"已產出高性能繁簡轉換標頭檔: {out_file}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
