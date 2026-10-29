# ETW 診斷流程

使用 Windows ETW、Microsoft TraceLogging、logman 與 tracerpt。專案僅提供輸入法事件埋點、工具接線及行為驗收；不自建 ETW 解碼器。一般開發與產品版預設 `EIME_ENABLE_DIAGNOSTICS=OFF`，不含埋點或第三方診斷程式碼。

## 建置與端到端驗證

PowerShell 7，MinGW 範例：

```powershell
cmake --preset mingw-diagnostic
cmake --build --preset mingw-diagnostic
ctest --preset mingw-diagnostic
pwsh -File scripts/diagnose.ps1 -Action Verify -BuildDirectory build/mingw-diagnostic
```

MSVC 改為 `msvc-diagnostic`。首次診斷配置需要網路，下載固定版本／SHA-256 的 Microsoft 標頭及 MIT 授權；產品配置維持離線。診斷版有 6 組測試，產品版仍不建立測試。

Verify 先把測試程式、DLL 與核心詞庫複製到獨立資料夾，不讀寫個人學習資料、不註冊輸入法、不發送桌面按鍵。錄製 ETW 時需要管理員權限，腳本會在必要時要求 UAC 並用隱藏的子程序錄製。

驗收案例包含延遲提交的「你好 → 左移 → 等號 → Enter」，確認得到「你=好」；再讓測試宿主刻意對 `Preview.SetText` 回傳一次 `E_ACCESSDENIED`。驗證要求：事件種類齊全、事件無遺失、提交開始／結束配對且順序遞增、恰好捕捉該次預期錯誤，以及實際載入的 DLL SHA-256 與測試檔相符。這是故障注入驗證，不代表記事本等真實宿主已完成驗收。

## 真實宿主錄製

先安裝診斷 DLL；依工具鏈擇一：

```powershell
# MinGW
pwsh -File scripts/install.ps1 -SourceDll build/mingw-diagnostic/eime_tip.dll
# MSVC
pwsh -File scripts/install.ps1 -SourceDll build/msvc-diagnostic/RelWithDebInfo/eime_tip.dll
```

儲存工作並關閉舊宿主。開始錄製後，再開啟新宿主並重現問題：

```powershell
pwsh -File scripts/diagnose.ps1 -Action Record -Seconds 20
```

腳本正常或發生可捕捉例外時都會停止自己的 session，不停止其他人的 ETW session。錄製最長 60 秒，ETL 使用 64 MB 循環上限。若錄製程序被強制終止，可能需依輸出中的 session 名稱執行 `logman stop <名稱> -ets`。循環覆寫或其他缺漏會降低證據完整性，不能當成驗證通過。

結束後可用 release preset 重建並重新安裝產品 DLL。未安裝診斷 DLL、未重開宿主或宿主沒有事件時，報告會明確報錯，不能用空報告判定正常。診斷版禁止經 Runtime 元件暫存為產品。

## 報告與查詢

預設輸出至 `%LOCALAPPDATA%/E-IME/diagnostics/<唯一識別碼>/`；可用 `-OutputDirectory` 指定新目錄。既有 ETL 不覆寫。

- `trace.etl`：原始 ETW 證據，可交給 WPA 等既有工具。
- `events.xml`：tracerpt 解碼結果。
- `events.json`：可直接以 PowerShell 查詢的事件資料。
- `report.json`：模組版本、編譯器、作業系統、宿主與 DLL 路徑／雜湊、事件統計、HRESULT 錯誤及完整性檢查。
- `etw-summary.txt`：Windows 工具回報的事件遺失數與統計。
- Verify 另保留測試程式／資料與 `binary.json`，方便核對或再次執行固定案例。

```powershell
pwsh -File scripts/diagnose.ps1 -Action Report -OutputDirectory '<輸出目錄>'
$events = Get-Content '<輸出目錄>/events.json' -Raw | ConvertFrom-Json
$events | Where-Object Operation -eq 'Preview.SetText' |
    Select-Object Process, Service, Key, Edit, Result
```

`Process + Service` 識別服務實例，`Key` 關聯按鍵事件，`Edit` 關聯提交快照，`Generation` 區分宿主／啟用世代；`Sequence` 用來檢查事件連續性。Snapshot 記錄 UTF-16 游標與文字 UTF-8 長度。Key 的值僅為 Ctrl／Shift／Alt 狀態，沒有虛擬鍵碼、輸入文字、候選內容或文字雜湊。

這些資訊用於追查流程與 API 結果，不能由其還原完整輸入或證明宿主實際畫出底線。完整性檢查也不取代輸入法行為測試；真正的程序重播／反向除錯仍需另用 WinDbg TTD，本階段未接入。

資料仍包含 PID、時間、程式碼版本與本機宿主／DLL 路徑，分享前請檢查。CI 僅錄製合成測試案例，保留報告及 ETL artifact 7 天；分析工具與診斷產物不進 Runtime。
