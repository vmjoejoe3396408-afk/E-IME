"""
E-IME 全面擴充台灣語料庫腳本
從 libchewing-data 抓取最新開源詞庫 (tsi.csv 與 word.csv)
整合現代科技詞彙與破音字完整支援，產出高頻繁體注音詞庫
"""

import urllib.request
import sys
from pathlib import Path
from build_sample_dict import CORE_WORDS

TSI_URL = "https://raw.githubusercontent.com/chewing/libchewing-data/main/dict/chewing/tsi.csv"
WORD_URL = "https://raw.githubusercontent.com/chewing/libchewing-data/main/dict/chewing/word.csv"

def main():
    print("[E-IME] 正在下載官方開源台灣注音語料 (tsi.csv & word.csv)...")
    try:
        tsi_raw = urllib.request.urlopen(TSI_URL, timeout=30).read().decode("utf-8")
        word_raw = urllib.request.urlopen(WORD_URL, timeout=30).read().decode("utf-8")
    except Exception as e:
        print(f"下載失敗: {e}，請檢查網路連線。")
        return 1

    entries = {}

    # 1. 優先載入 E-IME 核心高頻現代詞彙 (最高優先權)
    for word, zhuyin, freq in CORE_WORDS:
        entries[(word, zhuyin)] = freq + 500000

    # 2. 載入 tsi.csv (涵蓋完整單字與詞組，具備官方統計之真實語料頻率與完整破音字)
    tsi_count = 0
    for line in tsi_raw.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(",")
        if len(parts) >= 3:
            word = parts[0].strip()
            try:
                freq = int(parts[1].strip())
            except:
                freq = 1
            zhuyin = parts[2].strip()

            if not word or not zhuyin:
                continue

            # 詞頻縮放：最高 900,000，最低 200,000
            scaled_freq = min(900000, 200000 + freq * 150)
            key = (word, zhuyin)
            if key not in entries or entries[key] < scaled_freq:
                entries[key] = scaled_freq
                tsi_count += 1

    print(f"自 tsi.csv 載入詞彙/破音字: {tsi_count} 條")

    # 3. 載入 word.csv (作為罕見單字之保底補充，避免生僻字缺失)
    word_fallback_count = 0
    for line in word_raw.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(",")
        if len(parts) >= 3:
            char = parts[0].strip()
            zhuyin = parts[2].strip()
            if not char or not zhuyin:
                continue
            key = (char, zhuyin)
            if key not in entries:
                # 罕用生僻單字保底頻率
                entries[key] = 150000
                word_fallback_count += 1

    print(f"自 word.csv 保底補充罕用單字: {word_fallback_count} 條")

    out_file = Path(__file__).resolve().parent.parent / "data" / "core_dict.txt"
    with open(out_file, "w", encoding="utf-8") as f:
        f.write("# E-IME Full Taiwan Lexicon (Core + Libchewing)\n")
        f.write("# 格式: 詞彙\\t注音(以空白分隔)\\t基礎詞頻\n")
        # 依詞頻高到低排序寫入
        sorted_items = sorted(entries.items(), key=lambda x: x[1], reverse=True)
        for (word, zhuyin), freq in sorted_items:
            f.write(f"{word}\t{zhuyin}\t{freq}\n")

    print(f"[E-IME] 成功編譯出全量繁體注音詞庫: {out_file} (總計: {len(sorted_items)} 條)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
