# E-IME 0.4.0 預覽版發布準備

日期：2026-10-24。範圍：GitHub 原始碼預覽版。本文件記錄上傳前的本機驗證；私人預覽倉庫為 [vmjoejoe3396408-afk/E-IME](https://github.com/vmjoejoe3396408-afk/E-IME)，遠端建置狀態以該庫的 Actions 結果為準。換機測試步驟見 [另一台裝置測試指南](SECOND_DEVICE_TEST.md)。

## 已修正

- 同機多個宿主原本共用同一份日誌，各自推進已讀行數，造成漏讀與重算。新版每個實例使用獨立隨機 writer ID，各詞以每份檔案的累計值合併新增量。
- 原版兩個實例各學習一次的重現結果為 `你=0、好=2`，預期為 `1、1`；壓縮後讀者的權重從 `1` 變成 `2`。這兩種情況已有回歸測試。
- 日誌壓縮採暫存檔替換，半行紀錄等待換行後再讀取，無效數字忽略，寫入失敗不改學習權重，過大權重飽和而不溢位。
- 完整第三方授權及 OpenCC 作者名單隨庫保存。兩支產生器固定上游 commit，可於暫存目錄重建比對；既有詞庫與繁簡映射本體未變動。
- CMake 版本統一為 0.4.0，加入 MSVC UTF-8／靜態 CRT 設定、CTest 及 Windows MSVC／MinGW 工作流程。
- 名稱更新 `.cmd` 改呼叫 PowerShell 7 安裝器，不再寫死 DLL 版本；安裝器支援標準 Ninja／MSVC 輸出及指定 DLL，部署編譯失敗時會停止。

## 驗證結果

本機環境：Windows 10 x64（10.0.19045）、MinGW-w64 UCRT GCC 16.1.0、CMake 4.4.0、PowerShell 7.6.5。

| 驗證 | 結果 |
| --- | --- |
| 從空白建置目錄配置及 Release 編譯 | 通過 |
| 核心測試 | 13 / 13 模組通過 |
| 壓力測試 | 5 / 5 模組通過 |
| 同步回歸 | 同機實例、壓縮、重啟、舊格式、半行、無效數字、失敗寫入、計數上限、800 筆並行寫入均通過 |
| TSF 生命週期 | 25 次載入／卸載、500 組提交／取消與過期回呼通過 |
| TSF 輸入 | 標點、小鍵盤、反覆改選、FIFO 提交與 UTF-16 案例通過 |
| CTest 完整執行 | 5 / 5 組通過（含兩組並行執行） |
| 同步與 TSF 輸入重複執行 | 各連續 3 次通過 |
| 上游資料重建比對 | 詞庫及轉換表均一致 |
| PowerShell 語法 | scripts/ 與 testbench/ 內腳本解析通過 |
| GitHub Actions 工作流程 | actionlint 1.7.12 檢查通過 |

CTest 每次執行都建立新的獨立資料副本。這同時修復了重複測試沿用先前選字紀錄、改變預期候選順位的問題；成功後清除該次暫存目錄，失敗則保留證據路徑。

重現命令（PowerShell 7，MinGW 已在 PATH）：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build -C Release --output-on-failure
py -3 tools/verify_generated_data.py
```

## 發布範圍與限制

- 可作為 0.4.0 原始碼預覽版公開。GitHub 上首次 push 後，仍須確認 MSVC 與 MinGW 兩個 CI 工作成功；本機沒有 MSVC 工具組，因此未把 MSVC 列為已實測通過。
- 本次沒有執行管理員安裝／解除安裝，也沒有以新 DLL 重跑真實桌面宿主或視覺驗證；TSF 自動化使用 mock 宿主。歷次桌面驗證見 [testbench](../testbench/README.md)。
- 真實跨機雲端傳輸、Windows 11 桌面相容性及 32 位元應用不在本次驗證範圍。新版保留舊格式讀取，但升級時須先停止舊版宿主；已遺失的紀錄不能復原。請保留日誌檔名，不要複製成另一份 `.delta`。
- 日誌是累積學習資料，每次啟動的新 writer 會增加檔案；目前不自動清除舊 writer 檔案，避免遺失學習歷史。同步會掃描各檔，長期大量紀錄的效能尚未量測。
- 若製作 DLL／安裝包，另外核對編譯器 runtime 的分發聲明，並一併提供相符原始碼、詞庫與授權。個人自訂詞、日誌、二進位快取及本機路徑不應放入發布包。
