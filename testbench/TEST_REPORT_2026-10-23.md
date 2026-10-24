# E-IME 實機測試紀錄 — 2026-10-23

## 結果

Windows 原生記事本中的 6 項功能測試全部通過；9 項核心測試及 5 項壓力測試也全部通過。候選窗的視覺樣式尚未確認。

| 案例 | 預期輸出 | 實際輸出 | 結果 |
| --- | --- | --- | --- |
| 日常問候 | 你好 | 你好 | PASS |
| 科技名詞 | 台積電 | 台積電 | PASS |
| Ctrl+Shift+F 繁簡切換 | 台积电 | 台积电 | PASS |
| Shift 中英切換 | hello world | hello world | PASS |
| 長句連續輸入 | 今天天氣很好 | 今天天氣很好 | PASS |
| 左移 4 次、下方向鍵、第二候選、Enter | 今天公事很好 | 今天公事很好 | PASS |

以 computer-use 的 `@oai/sky` API 開啟獨立記事本，以 `press_key` 送出鍵碼，每鍵間隔 100 ms。每個案例從編輯區的可及性文字值取得實際輸出，再與預期值比對。多案例接續輸入造成的前後空白會正規化。選字完成後，焦點仍留在記事本編輯區。

結構化逐項證據、受測 DLL SHA-256 與工具限制記錄於 [JSON 報告](../build/agent_desktop_test_report.json)。最終可及性介面讀出的文字保存在 [輸出快照](../build/desktop_actual_output.txt)。

## 實測發現與修正

第一輪「你好」實際輸出為 `ㄋㄧˇㄏㄠˇ`。記事本已載入 E-IME DLL，但詞庫載入失敗。獨立探測結果：檔案存在、原生路徑可開啟、窄字元路徑無法開啟、詞庫載入數為 0。

`DictLoader` 的文字及二進位檔案介面改為接收 `std::filesystem::path`；TSF 初始化不再將中文絕對路徑轉成 `std::string`。保留 Windows 原生寬字元路徑後，六項實機案例全部通過。

現有第 8 項測試增加中文絕對路徑的文字詞庫載入、二進位讀寫、快取建立及快取命中驗證。重新編譯 DLL 後，再次執行全部核心測試通過。

## 壓力測試

- 22,000 次擊鍵平均延遲：3.01464 µs。
- 8 執行緒寫入與合併：800 / 800 筆。
- 40 音節解碼：111 µs。
- 500 次長句簡體轉碼及 5,000 次隨機輸入：通過。

原始輸出：[核心測試](../build/unit_test_output.txt)、[壓力測試](../build/stress_test_output.txt)。8 執行緒測試使用本機資料夾，未驗證真實多台電腦間的雲端傳輸。

## 視覺驗證限制

computer-use 可讀取記事本可及性文字並送出鍵盤事件；畫面擷取回報 `FrameArrived timed out: timed out waiting on channel`，座標輸入回報 `coordinate input geometry is unavailable`。因此候選窗圓角、陰影、配色與組字底線對齊未經視覺驗證，不能列為已通過。
