# 第三方資料與授權聲明

E-IME 本專案程式碼採用 [MIT License](LICENSE)。以下資料保留各自的授權，MIT 不會取代其授權條件。完整授權文本隨原始碼置於 [LICENSES/](LICENSES/)，分發時請保留本文件及該目錄。

## libchewing-data

- 專案：[GitHub 封存庫](https://github.com/chewing/libchewing-data)，現已遷移至 [Codeberg](https://codeberg.org/chewing/libchewing-data)。
- 固定來源版本：`c44e81aef24b06f1509f19e1be54c99812d0c43f`。
- 原始資料：[tsi.csv](https://github.com/chewing/libchewing-data/blob/c44e81aef24b06f1509f19e1be54c99812d0c43f/dict/chewing/tsi.csv)、[word.csv](https://github.com/chewing/libchewing-data/blob/c44e81aef24b06f1509f19e1be54c99812d0c43f/dict/chewing/word.csv)。兩份檔案的原始聲明均為 `Copyright (c) 2025 libchewing Core Team`、`LGPL-2.1-or-later`。
- 本專案衍生檔案：[data/core_dict.txt](data/core_dict.txt)，依 **LGPL-2.1-or-later** 分發。完整條款：[LGPL 2.1](LICENSES/LGPL-2.1.txt)，其引用的 [GPL 2.0](LICENSES/GPL-2.0.txt) 亦隨附。
- E-IME contributors 於 2026-10-24 記錄的修改：合併 [CORE_WORDS](tools/build_sample_dict.py)、縮放詞頻、補充罕用字、排序及轉換為 UTF-8 TSV。資料本體與發布前既有詞庫一致，新增來源及修改聲明。
- 可編輯的衍生資料與完整轉換腳本均包含於本庫。重建命令：`python tools/build_full_dict.py`（Python 3.10+，需要網路）。

## OpenCC

- 專案：[Open Chinese Convert](https://github.com/BYVoid/OpenCC)。
- 固定來源版本：`bcf847cf3fbd79c74779567f9a491ea8ab878183`。
- 原始資料：[TSCharacters.txt](https://github.com/BYVoid/OpenCC/blob/bcf847cf3fbd79c74779567f9a491ea8ab878183/data/dictionary/TSCharacters.txt)。
- 原有版權聲明保留：`Copyright (c) 2010-2023 BYVoid and OpenCC Contributors`。上游作者及貢獻者名單完整保留於 [OpenCC-AUTHORS.txt](LICENSES/OpenCC-AUTHORS.txt)。
- 本專案衍生資料：位於 [engine/include/hanzi_conv.h](engine/include/hanzi_conv.h) 的繁簡字元映射表，依 **Apache-2.0** 分發。完整條款：[Apache 2.0](LICENSES/Apache-2.0.txt)。該標頭中的 E-IME 轉換程式碼依 MIT 分發。
- E-IME contributors 於 2026-10-24 記錄的修改：僅取單字元對照、選擇第一個簡體候選並依 Unicode 排序，轉為 C++ 查表資料。映射本體與發布前既有映射一致，新增來源及修改聲明。
- 重建命令：`python tools/generate_hanzi_conv.py`（Python 3.10+，需要網路）。

## 重建與分發

一般編譯直接使用庫內資料，不會下載上游檔案。兩支產生器均固定至上述 commit，可用 `--output <path>` 輸出至其他位置；執行 `python tools/verify_generated_data.py` 可在暫存目錄重建並比對庫內內容。

本次準備的是原始碼預覽版。若另行分發 DLL 或安裝包，請一併提供上述授權、聲明、可編輯詞庫及與二進位檔相符的完整原始碼，並檢查所用編譯器及其靜態連結執行庫的分發聲明。不要把個人的 `custom_words.txt`、學習日誌、快取或 `data-root.txt` 包入發布檔。

## Microsoft TraceLogging（僅診斷建置）

- 專案：[microsoft/tracelogging](https://github.com/microsoft/tracelogging)。
- 固定版本：`daaff5ffd5e3613f2c1a91f323350e98e93e80b3`。
- 使用 `etw/cpp/traceloggingdynamic/TraceLoggingDynamic.h`，SHA-256：`6eb2a1e41cd335c8c00382284203ec81aea4fb58874cec6be2911c67c6f4c37c`。
- 授權：[MIT](https://github.com/microsoft/tracelogging/blob/daaff5ffd5e3613f2c1a91f323350e98e93e80b3/LICENSE)，Copyright (c) Microsoft Corporation。
- 診斷配置會下載未修改的標頭及完整授權至該建置目錄的 `_deps/microsoft-tracelogging/`，並驗證兩者雜湊。一般開發與產品配置不下載、不編譯此依賴。
- 本專案自行實作的診斷接線由 AI 生成；上述第三方標頭為 Microsoft 程式碼。若另行分發診斷二進位，須隨附快取中的完整 MIT 授權。現行 Runtime 產物不含此依賴。
