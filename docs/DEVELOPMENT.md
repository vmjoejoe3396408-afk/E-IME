# 開發、產品與診斷的邊界

目前採「同庫管理、分目標建置、分產物發布」。開發／產品建置已分離，另提供可選 ETW 診斷建置。診斷使用 Microsoft TraceLogging 與 Windows logman／tracerpt，沒有自建追蹤解碼器；TTD 與模糊測試引擎尚未接入。

## 共用入口

需要 CMake 3.20+，Windows 命令使用 PowerShell 7。

| Preset | 工具鏈 | 內容 |
| --- | --- | --- |
| `mingw-dev` | MinGW-w64／Ninja | RelWithDebInfo、產品、CLI 與 5 組測試 |
| `msvc-dev` | Visual Studio 2022 x64 | 同上 |
| `mingw-release` | MinGW-w64／Ninja | Release，停用全部測試與開發工具 |
| `msvc-release` | Visual Studio 2022 x64 | 同上 |
| `mingw-diagnostic`／`msvc-diagnostic` | 對應工具鏈 | ETW 埋點、開發工具、6 組測試；首次需下載固定版本依賴 |

```powershell
cmake --preset mingw-dev
cmake --build --preset mingw-dev
ctest --preset mingw-dev
```

MSVC 改用 `msvc-dev`。共用設定保存在 `CMakePresets.json`；本機覆寫可放 `CMakeUserPresets.json`，該檔不提交。傳統 `cmake -S . -B ...` 命令仍可使用；`BUILD_TESTING` 控制測試目標，`EIME_BUILD_TOOLS` 控制 CLI。

## 產品暫存與檢查

```powershell
cmake --preset mingw-release
cmake --build --preset mingw-release
cmake --install build/mingw-release --config Release --prefix out/runtime --component Runtime
pwsh -File scripts/verify-runtime.ps1 -Path out/runtime
```

使用新的空目錄作為 `--prefix`。暫存不會刪除舊檔；驗證器會拒絕多餘或缺少的檔案。MSVC 對應 `msvc-release` 與 `build/msvc-release`。

Runtime 僅包含 DLL、純文字核心詞庫、安裝／停用／解除註冊腳本、啟動器、說明與授權。測試、CLI、分析器、除錯符號、自訂詞、日誌、快取與資料路徑指標都不納入。`cmake --install` 只建立暫存目錄，不執行 Windows 輸入法註冊。

目前不建立或上傳正式二進位 Release；暫存目錄仍須完成相符原始碼、編譯器執行庫聲明及實機驗收等分發工作。來源倉庫的安裝指南見 [SECOND_DEVICE_TEST.md](SECOND_DEVICE_TEST.md)。

## 診斷工具的接入方式

- 產品只保留必要事件介面；分析工具與測試可以依賴產品核心，產品不能反向依賴它們。
- ETW／WPR 設定、測試輸入、工具版本與執行命令跟程式碼一起管理；工具本體交由開發環境取得。
- 產生的 trace、報表及重現附件放 `diagnostics/` 或 `out/`，不隨 Runtime 分發。
- 接入每種工具前先確認具體要找的問題、現有工具鏈相容性與驗證方式，再新增對應目標及 CI。尚無功能的開關與 preset 不先建立。

ETW 的驗證、實機錄製與報告查詢見 [診斷指南](DIAGNOSTICS.md)。後續可按實際問題評估既有狀態式測試工具。
