# E-IME (Enhanced IME)

> 現代 C++20 輕量化 Windows 注音輸入法 (Text Services Framework, TSF)

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%2F%2011%20(x64)-0078D6.svg)](https://microsoft.com/windows)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

目前版本：**0.4.0 預覽版**。原始碼可自行建置；系統整合目前以 Windows x64 為主。MIT 適用於自研程式碼，詞庫及轉換資料的授權見 [第三方聲明](THIRD_PARTY_NOTICES.md)。

私人預覽階段的換機操作請參閱 [另一台裝置測試指南](docs/SECOND_DEVICE_TEST.md)。

---

## 1. 專案簡介 (Overview)

**E-IME (Enhanced IME / 改進型注音輸入法)** 是一套以現代 C++20 從頭獨立自研的 Windows 系統級注音輸入法。其設計理念在於**貼合微軟新注音長年累積的操作習慣與肌肉記憶**，同時針對傳統輸入法的痛點進行工程優化：

* **貼合微軟注音操作直覺**：
  * **整句組字與局部鎖定**：支援在組字區內按 `←` / `→` 移動游標、按 `↓` 展開候選窗、選字後局部鎖定（Pinned Segments），整句其餘部分自動重新尋求全局最優解。
  * **容錯重組**：盲打時若先按韻母再按聲母（如 `k` (ㄜ) + `2` (ㄉ)），自動重排為標準音節，無字保底輸出原生注音符號。
  * **標點符號與快捷鍵**：支援 `Ctrl+,`（，）、`Ctrl+.`（。）、`Ctrl+'`（、）組字底線與候選展開；單擊 `Shift` 切換中英；`Ctrl+Shift+F` 繁簡即時轉碼輸出。
* **輕量化與詞庫快取**：
  * MinGW 靜態連結標準庫；MSVC 使用靜態 CRT。一般建置直接使用庫內資料，不需下載詞庫。
  * 內建詞庫二進位快取（`core_dict.txt.bin`）；效能數字依硬體及測試情境而異，歷次量測見 [測試紀錄](testbench/README.md)。
* **無鎖跨裝置雲端同步**：
  * 每個宿主內的輸入法實例使用自己的 `device_<hostname>_<writer-id>.delta`，以各詞累計值的新增量合併，避免同機多實例互相覆寫及壓縮後重算。由外部雲端硬碟負責傳輸檔案；真實跨機雲端傳輸尚未驗證。
* **無焦點懸浮視窗**：
  * Win32 輕量視窗套用 `WS_EX_NOACTIVATE` 樣式，搭配 GDI 雙緩衝渲染，避免啟用候選窗時轉移宿主焦點。

---

## 2. 專案目錄結構 (Directory Layout)

```text
├── CMakeLists.txt              # 核心建置腳本 (支援 MinGW / MSVC)
├── README.md                   # 專案總覽與建置指南
├── LICENSE                     # MIT 開源授權協議
├── THIRD_PARTY_NOTICES.md      # 第三方詞庫與資料授權聲明 (libchewing, OpenCC)
├── LICENSES/                  # 完整第三方授權與作者聲明
├── .github/workflows/ci.yml    # Windows MSVC / MinGW 自動建置與 CTest
├── .gitignore                  # Git 排除規則 (排除本機快取與私人日誌)
├── engine/                     # 輸入法純邏輯引擎 (Trie / Viterbi / 狀態機)
│   └── include/                # zhuyin.h, trie.h, viterbi.h, input_state_machine.h
├── tsf/                        # Windows TSF COM 系統外殼層 (TIP)
│   ├── dllmain.cpp             # COM 伺服器入口點 (DllRegisterServer 等)
│   ├── text_service.cpp        # ITfTextInputProcessorEx 與按鍵攔截
│   └── register.cpp            # TSF 語言設定檔與 Category 註冊
├── ui/                         # Win32 輕量化無焦點候選字視窗 (GDI)
├── sync/                       # 跨裝置無鎖同步引擎 (SyncJournal)
├── data/                       # 核心詞庫與使用者自造詞
│   ├── core_dict.txt           # 繁體語料庫 (衍生自 libchewing-data)
│   ├── custom_words.txt        # 使用者自訂詞庫 (純文字擴充)
│   └── sync/                   # 跨設備增量同步目錄
├── scripts/                    # 一鍵安裝、反註冊與熱部署腳本
├── tests/                      # 13 個核心模組、同步回歸及 TSF 測試
└── docs/                       # 技術架構、研發歷程與邊界實作對照文檔
```

---

## 3. 編譯與安裝指南 (Build & Installation)

### 3.1 前置環境要求
* **作業系統**：Windows 10 / 11 (64-bit)
* **編譯器**：MinGW-w64 GCC 13+，或 Visual Studio 2022 的「使用 C++ 的桌面開發」工具組
* **建置工具**：CMake 3.20+ 與 Ninja
* **腳本環境**：PowerShell 7 (`pwsh`)
* **選用**：Python 3.10+ 與網路連線，僅供重新產生／核對第三方資料

### 3.2 編譯步驟
開啟 PowerShell 7，確定 MinGW 的 `g++`、CMake 與 Ninja 已在 PATH，執行：

```powershell
# 1. 建立建置目錄並配置 Ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++

# 2. 執行編譯
cmake --build build

# 3. 執行全自動單元與整合測試
ctest --test-dir build -C Release --output-on-failure
```

MSVC 可使用以下替代步驟（不需要 Ninja）：

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

CTest 執行核心、同步回歸、壓力、TSF 生命週期及 TSF 輸入共 5 組測試。資料與日誌使用建置目錄內的隔離副本。GitHub Actions 會在 push／pull request 時以 MSVC 和 MinGW 分別建置與測試；第一次上傳後請確認兩個工作都成功。

### 3.3 註冊與安裝至 Windows
編譯完成後，以**系統管理員身分**執行安裝腳本：

```powershell
pwsh -File .\scripts\install.ps1
```
安裝成功後，即可在 Windows 工作列輸入法清單中選取「**E-IME (改進型注音)**」開始使用。

MSVC 或其他建置位置請指定 DLL：`pwsh -File .\scripts\install.ps1 -SourceDll .\build-msvc\Release\eime_tip.dll`。根目錄的名稱更新 `.cmd` 也會呼叫同一個安裝腳本，依序尋找標準建置輸出或目前註冊版本，並回報失敗。

安裝器會把 DLL 複製至本機版本目錄，詞庫仍指向此專案的 `data/`；安裝後請保留專案目錄。更新前先儲存工作並關閉使用舊版輸入法的應用程式，更新後重新開啟。

如需反註冊並移除：
```powershell
pwsh -File .\scripts\uninstall.ps1
```

### 3.4 舊日誌升級與隱私

從舊版升級前，先關閉所有載入舊 DLL 的應用程式（必要時登出再登入），並備份 `data/sync/`。新版可讀取舊 `device_<hostname>.delta`，新紀錄則寫入每個實例自己的檔案；不要讓舊版與新版持續共寫，也不要手動更名或複製日誌。舊版已經遺失的紀錄無法自動復原。

學習日誌包含選過的字詞及時間，請只放在可信任的私人同步資料夾；`.gitignore` 會排除日誌與快取。`data/custom_words.txt` 目前是公開範例，若加入私人詞彙，發布前應還原該檔案。

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

另有獨立的 [同步回歸測試](tests/sync_journal.cpp)，驗證同機多實例、日誌壓縮、重啟、半行及舊格式紀錄、寫入失敗、計數上限與 800 筆並行學習。最新發布準備驗證見 [2026-10-24 紀錄](docs/RELEASE_READINESS_2026-10-24.md)。自動化 TSF 測試使用 mock 宿主；不代表所有桌面程式或候選窗視覺皆已驗證。

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
