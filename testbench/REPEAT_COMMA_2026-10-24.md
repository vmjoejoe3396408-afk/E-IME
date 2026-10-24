# 2026-10-24：按住 Ctrl 連續輸入逗號

已部署 DLL SHA-256：`8efb6a9c2954f2b39b224ab395d1d4c6a88ac887777c5b42afe56fd5475a6ee3`。

## 回報與修復

使用者在新開的記事本持續按住 Ctrl，第一次逗號正常，第二次及之後變成 ㄝ。原程式每次只查詢通用 VK_CONTROL，並在 OnTestKeyDown、OnKeyDown 重複讀取修飾鍵及文字，沒有追蹤 Ctrl 的按下／放開事件。

新增原生測試模擬第一下有通用 Ctrl 狀態、後續保留 Ctrl-down 事件但狀態查詢缺失。舊 DLL 在第二次逗號的文字比對失敗；新版通過。這證實舊版在此事件情境下存在缺陷，但未對使用者的實體鍵盤事件做追蹤，因此不宣稱已確認其實際事件序列。

修復包含：

- 左右 Ctrl 分別追蹤，直到各自放開；初始未觀察到 Ctrl 事件時，查詢通用及左右修飾鍵狀態。
- OnTestKeyDown 接受的修飾鍵及 Unicode 文字以快照交給對應的 OnKeyDown，避免同一按鍵改走不同路徑。
- 失焦與 Deactivate 清除追蹤及待用快照；Ctrl 放開後，單按逗號仍輸入大千 ㄝ。

GetKeyState 與訊息處理時序有關，因此沒有改用讀取即時實體狀態的 GetAsyncKeyState 混入較晚的修飾鍵狀態。參考 [Microsoft 對兩者差異的說明](https://devblogs.microsoft.com/oldnewthing/20041130-00/?p=37173)。

## 驗證與限制

- 13 個核心測試模組通過。
- 原生 TSF input：左右 Ctrl 各按住連續輸入 8 個逗號，逐次驗證文字、未結束的組字及底線長度；放開後 ㄝ 正常。同次事件在測試及輸入回呼之間改變 Ctrl 查詢狀態，也仍輸入逗號。原有重選字、中英混合與 200 次延遲提交測試通過。
- 25 輪載入／卸載、500 組重入提交／取消及過期回呼測試通過。
- computer-use 新開記事本，20 次 Ctrl+, 後接「你好」，再輸入「些」（vu, 一聲），結果完全相符。確認宿主實際載入上述版本。

computer-use 的 press_key 每次送出完整組合鍵，實機測試沒有模擬 Ctrl 全程按住；持續按住的路徑由原生 TSF 回歸測試涵蓋。已開啟的程式保留舊 DLL，需儲存並正常關閉、重新開啟才會載入新版。

證據：[修復前失敗](results/20261024-repeat-comma/baseline-input.txt)、[核心測試](results/20261024-repeat-comma/unit.txt)、[TSF input](results/20261024-repeat-comma/input.txt)、[生命週期](results/20261024-repeat-comma/lifecycle.txt)、[實機結果](results/20261024-repeat-comma/desktop.json)、[版本及載入模組](results/20261024-repeat-comma/manifest.json)。
