# 測試與證據索引

現行對象：主線 `master`（功能基準 `9b73ae7`）。操作與合併前差異見 [文件索引](../docs/README.md)。

## 自動化測試

完成建置後，從 PowerShell 7 執行以下其中一個命令：

```powershell
# MSVC
ctest --test-dir build-msvc -C Release --output-on-failure

# MinGW
ctest --test-dir build -C Release --output-on-failure
```

| CTest 名稱 | 來源 | 覆蓋範圍 |
| --- | --- | --- |
| `core` | [test_main.cpp](../tests/test_main.cpp) | 15 個核心模組：解析、局部鎖定、同步、候選窗建立、COM、繁簡、快取、游標、重選、聲調提早修正、多欄候選及標點 |
| `sync_journal` | [sync_journal.cpp](../tests/sync_journal.cpp) | 同機多實例、壓縮、重啟、舊格式、半行、失敗寫入、計數上限及 800 筆並行學習 |
| `stress` | [stress_suite.cpp](stress_suite.cpp) | 22,000 次擊鍵、8 執行緒日誌、40 音節解碼、繁簡轉換及 5,000 次隨機輸入 |
| `tsf_lifecycle` | [tsf_lifecycle.cpp](../tests/tsf_lifecycle.cpp) | 25 次載入／卸載、500 組提交／取消、顯示屬性生命週期及過期回呼 |
| `tsf_input` | [tsf_input.cpp](../tests/tsf_input.cpp) | 小鍵盤、標點與修飾鍵快照、FIFO 提交、重選、底線、UTF-16 及右側繁簡快捷鍵 |

[run_test.cmake](../tests/run_test.cmake) 每次建立獨立資料副本及受測 DLL；成功後清理該次目錄，失敗時保留路徑供檢查。使用 CTest 可避免舊學習紀錄改變候選順位，也不會讀寫使用者的實際日誌。

2026-10-25 本機重新建置並執行上述功能基準，**5／5 組通過**，含兩組並行測試。環境為 Windows 10 x64、MinGW-w64 UCRT GCC 16.1.0、CMake 4.4.0、PowerShell 7.6.5。本次僅修改文件，未執行管理員安裝或新版桌面視覺驗證。

核心測試中的候選窗案例只涵蓋建立及程式路徑，不是像素比對；TSF 測試主要使用 [mock_tsf.h](mock_tsf.h)。壓力測試數字也不等於所有宿主的端到端延遲或記憶體用量。

## 桌面與換機驗證

以 [換機指南的實機清單](../docs/SECOND_DEVICE_TEST.md#實機檢查清單) 為準。每個案例需記錄：

- 分支、commit、DLL SHA-256、Windows 與宿主版本。
- 實際按鍵及修飾鍵的按住／放開順序。
- `Expected`、`Actual` 及 `PASS`／`FAIL`／`BLOCKED`。
- 必要時擷取候選窗、底線、游標及顯示縮放的畫面。

先使用實際按鍵輸入，再從宿主讀回文字比對；直接貼上預期中文字不能證明輸入法處理正確。目前版本需特別檢查四欄選字、直角窗、實線底線及繁簡提示。

[agent_desktop_runner.ps1](agent_desktop_runner.ps1) 是舊版送鍵煙霧腳本，使用已安裝的 TIP、啟動記事本並輸出 `build/legacy_key_injection_report.json`；它不比對實際文字，`COMPLETED` 只代表按鍵送出，不能當成 PASS。人工或桌面代理比對報告可另存 `build/agent_desktop_test_report.json`。這些本機證據預設不提交。

## 歷史紀錄

以下保留原受測版本的描述。舊紀錄中的虛線、圓角、13 項測試或更早的數量，不代表目前主線的狀態；報告連到 `build/` 的原始檔僅為當時本機證據，未隨庫發布。

| 日期／主題 | 紀錄 |
| --- | --- |
| 2026-10-23 記事本實測 | [基本輸入、改字與中文路徑修復](TEST_REPORT_2026-10-23.md) |
| 2026-10-23 宿主崩潰 | [COM／TSF 生命週期修復](REPAIR_2026-10-23.md) |
| 2026-10-24 標點 | [Ctrl+, 封包與游標](COMMA_2026-10-24.md)、[按住 Ctrl 連打逗號](REPEAT_COMMA_2026-10-24.md) |
| 2026-10-24 選字 | [反覆改選與明確提交](RESELECT_2026-10-24.md) |
| 2026-10-24 小鍵盤／驚嘆號 | [小鍵盤組字](NUMPAD_2026-10-24.md)、[Ctrl+Shift+1](EXCLAMATION_2026-10-24.md) |
| 2026-10-24 主線預覽準備 | [同步回歸、授權與建置整理](../docs/RELEASE_READINESS_2026-10-24.md) |
