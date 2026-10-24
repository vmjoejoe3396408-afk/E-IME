# E-IME (Enhanced IME)

> 現代 C++20 輕量化 Windows 注音輸入法 (Text Services Framework, TSF)

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%2F%2011%20(x64)-0078D6.svg)](https://microsoft.com/windows)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

---

## 1. 專案簡介 (Overview)

**E-IME (Enhanced IME / 改進型注音輸入法)** 是一套以現代 C++20 從頭獨立自研的 Windows 系統級注音輸入法。其設計理念在於**貼合微軟新注音長年累積的操作習慣與肌肉記憶**，同時針對傳統輸入法的痛點進行工程優化：

* **貼合微軟注音操作直覺**：
  * **整句組字與局部鎖定**：支援在組字區內按 `←` / `→` 移動游標、按 `↓` 展開候選窗、選字後局部鎖定（Pinned Segments），整句其餘部分自動重新尋求全局最優解。
  * **容錯重組**：盲打時若先按韻母再按聲母（如 `k` (ㄜ) + `2` (ㄉ)），自動重排為標準音節，無字保底輸出原生注音符號。
  * **標點符號與快捷鍵**：支援 `Ctrl+,`（，）、`Ctrl+.`（。）、`Ctrl+'`（、）組字底線與候選展開；單擊 `Shift` 切換中英；`Ctrl+Shift+F` 繁簡即時轉碼輸出。
* **輕量極速與零外部 Runtime 依賴**：
  * 核心 DLL 靜態連結標準庫，常駐記憶體約 10MB，單次按鍵解碼延遲微秒級。
  * 內建詞庫二進位極速快取（`core_dict.txt.bin`），冷啟動載入時間 $< 3\text{ms}$。
* **無鎖跨裝置雲端同步**：
  * 捨棄容易在 OneDrive / Google Drive 發生檔案鎖定衝突的單一資料庫模式，採用 **Per-Device Append-Only Delta 日誌**，寫入僅追加本地設備紀錄，讀取時記憶體原地聯集（CRDT 原則）。
* **無焦點懸浮視窗**：
  * Win32 輕量視窗套用 `WS_EX_NOACTIVATE` 樣式，搭配 GDI+ 雙緩衝渲染，保證永不搶奪宿主程式焦點。

---

## 2. 專案目錄結構 (Directory Layout)

```text
├── CMakeLists.txt              # 核心建置腳本 (支援 MinGW / MSVC)
├── README.md                   # 專案總覽與建置指南
├── LICENSE                     # MIT 開源授權協議
├── THIRD_PARTY_NOTICES.md      # 第三方詞庫與資料授權聲明 (libchewing, OpenCC)
├── .gitignore                  # Git 排除規則 (排除本機快取與私人日誌)
├── engine/                     # 輸入法純邏輯引擎 (Trie / Viterbi / 狀態機)
│   └── include/                # zhuyin.h, trie.h, viterbi.h, input_state_machine.h
├── tsf/                        # Windows TSF COM 系統外殼層 (TIP)
│   ├── dllmain.cpp             # COM 伺服器入口點 (DllRegisterServer 等)
│   ├── text_service.cpp        # ITfTextInputProcessorEx 與按鍵攔截
│   └── register.cpp            # TSF 語言設定檔與 Category 註冊
├── ui/                         # Win32 輕量化無焦點候選字視窗 (GDI+)
├── sync/                       # 跨裝置無鎖同步引擎 (SyncJournal)
├── data/                       # 核心詞庫與使用者自造詞
│   ├── core_dict.txt           # 繁體語料庫 (衍生自 libchewing-data)
│   ├── custom_words.txt        # 使用者自訂詞庫 (純文字擴充)
│   └── sync/                   # 跨設備增量同步目錄
├── scripts/                    # 一鍵安裝、反註冊與熱部署腳本
├── tests/                      # 13 大核心整合測試套件
└── docs/                       # 技術架構、研發歷程與邊界實作對照文檔
```

---

## 3. 編譯與安裝指南 (Build & Installation)

### 3.1 前置環境要求
* **作業系統**：Windows 10 / 11 (64-bit)
* **編譯器**：支援 C++20 的編譯器（如 MinGW-w64 GCC 13+、Clang 16+ 或 MSVC 2022+）
* **建置工具**：CMake 3.20+ 與 Ninja

### 3.2 編譯步驟
開啟 PowerShell，執行以下指令：

```powershell
# 1. 建立建置目錄並配置 Ninja
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 2. 執行編譯
cmake --build build

# 3. 執行全自動單元與整合測試
.\build\eime_tests.exe
```

### 3.3 註冊與安裝至 Windows
編譯完成後，以**系統管理員身分**執行安裝腳本：

```powershell
pwsh -File .\scripts\install.ps1
```
安裝成功後，即可在 Windows 工作列輸入法清單中選取「**E-IME (改進型注音)**」開始使用。

如需反註冊並移除：
```powershell
pwsh -File .\scripts\uninstall.ps1
```

---

## 4. 實測結果與已知限制 (Test Results & Limitations)

### 4.1 實測驗證覆蓋
本專案內建測試套件 [`tests/test_main.cpp`](tests/test_main.cpp) 涵蓋 13 項核心整合驗證，全數通過：
* 大千鍵盤映射與注音音節解析器
* 長句游標移動與受限 Viterbi 局部鎖定重算
* 跨裝置無鎖增量同步與自造詞漫遊
* Shift 中英狀態切換與全形標點符號
* Win32 WS_EX_NOACTIVATE 現代淺色候選字視窗渲染
* Windows TSF COM DLL 匯出符號與生命週期管理
* 繁簡中文即時切換與轉碼（`Ctrl + Shift + F`）
* 中文路徑下的文字詞庫與二進位快取讀寫序列化
* 句中插入音節預覽與 UTF-16 游標偏移追蹤
* 高頻多音字（些、得、長、行）與方向鍵候選導航
* 注音語音規範先韻後聲智慧重組、第 4 個注音早期攔截與無字保底
* 句中標點符號組字區保留與游標精確定位
* 單字、整詞與句中選字可反覆改選與明確確認上屏

### 4.2 已知限制
1. **64 位元應用優先**：目前發布與測試主要針對 Windows 64-bit 原生應用程式，尚未編譯與註冊 32 位元 WoW64 模組。
2. **鍵盤排列**：目前支援標準大千注音鍵盤排列，尚未支援倚天 26/41 鍵、許氏鍵盤等。
3. **語意解碼模型**：採用統計詞頻與動態規劃，非大型神經網絡語言模型，在特定罕見生僻詞句情境下仍需手動選字。

---

## 5. 授權與第三方聲明 (License & Notices)

* **專案原始碼**：採用 [MIT License](LICENSE) 授權。
* **第三方資料集**：
  * 核心詞庫衍生自 [libchewing-data](https://github.com/chewing/libchewing-data)（GNU LGPL v2.1 or later）。
  * 繁簡轉換映射衍生自 [OpenCC](https://github.com/BYVoid/OpenCC)（Apache License 2.0）。
  * 完整授權與版權宣告請參閱 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
