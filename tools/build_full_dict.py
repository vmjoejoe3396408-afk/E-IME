"""
E-IME 全面擴充台灣語料庫腳本
從 libchewing-data 固定版本抓取開源詞庫 (tsi.csv 與 word.csv)
整合現代科技詞彙與破音字完整支援，產出高頻繁體注音詞庫
"""

import urllib.request
import sys
import argparse
from pathlib import Path
from build_sample_dict import CORE_WORDS

UPSTREAM_REVISION = "c44e81aef24b06f1509f19e1be54c99812d0c43f"
BASE_URL = f"https://raw.githubusercontent.com/chewing/libchewing-data/{UPSTREAM_REVISION}/dict/chewing"
TSI_URL = f"{BASE_URL}/tsi.csv"
WORD_URL = f"{BASE_URL}/word.csv"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent.parent / "data" / "core_dict.txt")
    args = parser.parse_args()
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

    out_file = args.output
    out_file.parent.mkdir(parents=True, exist_ok=True)
    with open(out_file, "w", encoding="utf-8", newline="\n") as f:
        f.write("# E-IME Full Taiwan Lexicon (Core + Libchewing)\n")
        f.write("# SPDX-License-Identifier: LGPL-2.1-or-later\n")
        f.write("# Upstream: Copyright (c) 2025 libchewing Core Team\n")
        f.write(f"# Source revision: {UPSTREAM_REVISION}\n")
        f.write("# Modified by E-IME contributors, 2026-10-24: merge core vocabulary, rescale frequencies, TSV conversion.\n")
        f.write("# License and source details: ../THIRD_PARTY_NOTICES.md\n")
        f.write("# 格式: 詞彙\\t注音(以空白分隔)\\t基礎詞頻\n")
        # 依詞頻高到低排序寫入
        sorted_items = sorted(entries.items(), key=lambda x: x[1], reverse=True)
        for (word, zhuyin), freq in sorted_items:
            f.write(f"{word}\t{zhuyin}\t{freq}\n")

    print(f"[E-IME] 成功編譯出全量繁體注音詞庫: {out_file} (總計: {len(sorted_items)} 條)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
