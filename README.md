# E-IME (Enhanced IME)

以 C++20 開發的 Windows 注音輸入法，保留微軟注音使用者熟悉的整句組字、回頭選字與中英切換方式，讓詞庫、選字規則及候選窗可以自行維護。

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](CMakeLists.txt)
[![Windows x64](https://img.shields.io/badge/Windows-10%20%2F%2011%20x64-0078D6.svg)](#支援範圍)
[![License: MIT](https://img.shields.io/badge/Code-MIT-yellow.svg)](LICENSE)

**目前是私人預覽階段。** 此 README 以 `feature/aggressive-correction` 為準；CMake 基礎版本仍為 `0.4.0`，功能分支尚未合併至 `master`。兩者的功能差異見 [文件索引與分支對照](docs/README.md)。

## 開發方式

**本專案的全部程式碼均由 AI 生成。作者沒有手寫任何一行程式碼，僅提供指示。** 程式碼的生成方式與第三方資料來源分別記錄於 [實作與來源紀錄](docs/IMPLEMENTATION_PROVENANCE.md) 及 [第三方聲明](THIRD_PARTY_NOTICES.md)。

## 想改善什麼

設計出發點是沿用熟悉的注音操作，再改善日常使用中想自己調整、卻不容易處理的部分。

| 使用痛點 | E-IME 的做法 |
| --- | --- |
| 詞庫不合自己的用語，需要加入新詞或專業詞彙 | 使用 UTF-8 純文字詞庫與自訂詞檔，可直接編輯；另有二進位快取減少重複解析。 |
| 換裝置後希望保留個人選字學習 | 每個輸入法實例各寫一份學習日誌，可由外部私人雲端硬碟傳送；跨機實際傳輸仍待驗證。 |
| 候選字太小、選項太少，想調整顯示方式 | 候選字使用 24px 字級、38px 列高；本分支可展開最多四欄、每頁 36 項。樣式目前需修改原始碼。 |
| 長句中某個詞選錯，想回頭改字 | 移動組字游標、開啟候選並鎖定手選詞，其餘未鎖定部分重新解碼；選字後仍能再次改選。 |
| 快速打字時聲韻或聲調落鍵順序錯亂 | 支援聲韻槽位重排；本分支另對尚無詞庫匹配的已結音音節，嘗試補入晚到的介音或韻母。 |
| 某些按鍵行為或輸入 bug 需要自行修正 | 本專案程式碼採 MIT 授權，可重建 DLL、執行測試並用版本目錄部署。詞庫與映射資料另依第三方授權分發。 |

E-IME 參考注音使用需求與操作行為進行實作，系統整合使用公開的 TSF／Win32 API；詞庫及繁簡字元映射來自開源專案。目前開發紀錄未建立可供核對的正式淨室隔離流程，因此不以「淨室開發」描述本專案。實作依據、資料來源及紀錄範圍見 [實作與來源紀錄](docs/IMPLEMENTATION_PROVENANCE.md) 及 [第三方聲明](THIRD_PARTY_NOTICES.md)。

## 建置與安裝

需要 Windows 10／11 x64、Git、PowerShell 7、CMake 3.20+，以及 Visual Studio 2022 的「使用 C++ 的桌面開發」工具組。MinGW-w64 GCC 13+ 與 Ninja 也可建置。

在 PowerShell 7 取得功能分支：

```powershell
git clone --branch feature/aggressive-correction https://github.com/vmjoejoe3396408-afk/E-IME.git
cd E-IME
```

以下使用 MSVC；安裝器需要時會要求管理員權限：

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
pwsh -File .\scripts\install.ps1 -SourceDll .\build-msvc\Release\eime_tip.dll
```

一般建置直接使用庫內詞庫，不需要 Python 或重新下載資料。請確認每個步驟成功後再繼續。完整 MinGW 步驟、更新、停用與移除方式見 [換機與安裝指南](docs/SECOND_DEVICE_TEST.md)。

安裝後開啟新的記事本，以 `Win+Space` 選取 E-IME。**請保留專案目錄**：安裝器會複製 DLL 至本機版本目錄，但詞庫仍指向專案的 `data/`。

## 基本操作

| 操作 | 按鍵 |
| --- | --- |
| 輸入「你好」 | 中文模式輸入 `su3cl3`，再按 Enter |
| 開啟候選／確認候選 | ↓／數字 1–9 或 Enter；選字後再按 Enter 才提交整句 |
| 展開多欄候選 | 候選多於 9 項時按 →；←／→ 切欄，PgUp／PgDn 翻頁 |
| 切換中英 | 單按 Shift |
| 切換繁簡 | Ctrl+Shift+F |
| 指定繁體／簡體 | **右 Ctrl＋右 Shift＋T／S** |
| 逗號／句號／頓號 | Ctrl+,／Ctrl+.／Ctrl+' |

本分支使用直角淺色候選窗、實線組字底線，錯誤提示預設靜音。繁簡模式以切換時的短暫提示顯示，候選窗不常駐 `[繁]`／`[簡]` 標籤。更多標點、選字細節與容錯限制見 [操作指南](docs/USAGE.md)。

## 支援範圍

- 目前針對 Windows x64 與標準大千排列；未提供 32 位元 WoW64 模組或其他鍵盤排列。
- 解碼以詞頻、局部鎖定及少量固定字詞搭配規則為主，尚無訓練式語言模型；不保證所有句子都能自動改對。
- 每次 CTest 有 5 組測試，核心套件含 15 項模組；TSF 自動測試使用 mock 宿主。自動測試通過不能替代記事本、瀏覽器、編輯器與多螢幕的實機觀察。
- 個人日誌與快取不經 Git 傳送；`data/custom_words.txt` 是已追蹤的公開範例，加入私人詞彙後請留意不要提交。
- 歷次測試的版本、範圍及證據見 [測試索引](testbench/README.md)，目前待驗證事項見 [研發紀錄與路線](docs/PROCESS_AND_ROADMAP.md)。

## 文件與原始碼

[文件索引／分支差異](docs/README.md) · [操作指南](docs/USAGE.md) · [安裝與換機](docs/SECOND_DEVICE_TEST.md) · [系統架構](docs/ARCHITECTURE.md) · [更新紀錄](docs/CHANGELOG.md)

核心邏輯在 `engine/include/`，TSF 外殼在 `tsf/`，候選窗在 `ui/`，學習日誌在 `sync/`；安裝腳本、資料產生器與測試分別位於 `scripts/`、`tools/`、`tests/` 及 `testbench/`。

本專案程式碼採 [MIT](LICENSE)；libchewing 衍生詞庫採 LGPL-2.1-or-later，OpenCC 衍生映射採 Apache-2.0。完整文本與作者聲明隨庫保存於 [LICENSES](LICENSES/)，使用範圍及重建方式見 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
