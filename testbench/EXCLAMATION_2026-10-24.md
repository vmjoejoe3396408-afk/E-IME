# 2026-10-24：Ctrl+Shift+1 全形驚嘆號

已部署版本：`e6f501ff95724f47fee2537bd60789ff34fe3f51929195b37dc21d241cf51372`。

標點對照表已有 ! → ！，但中文模式的 Ctrl 快捷鍵只接收 OEM 標點鍵，未接收數字列 1，導致 Ctrl+Shift+1 被放行。現在明確接收並轉交既有全形標點插入流程，在游標位置輸入「！」並保留組字底線。Shift+1 仍輸入半形 !，英文模式不攔截 Ctrl+Shift+1。

舊版隔離 DLL 在新增的 Ctrl+Shift+1 原生測試失敗；新版的單獨輸入、連續兩次、句中插入、底線長度及游標位置測試通過，英文模式放行也通過。13 個核心模組及生命週期測試通過；兩者沿用前次測試執行檔載入新 DLL。TSF input 使用本次新增測試的執行檔。

新開記事本透過 computer-use 實際按鍵得到「！！你！好!」，逐字比對通過，前三個驚嘆號為 U+FF01，最後的 Shift+1 為 U+0021。已確認記事本載入本次版本。既有程式需正常結束後重新開啟。

證據：[實機結果](results/20261024-exclamation/desktop.json)、[修復前失敗](results/20261024-exclamation/baseline-input.txt)、[核心測試](results/20261024-exclamation/unit.txt)、[TSF input](results/20261024-exclamation/input.txt)、[生命週期](results/20261024-exclamation/lifecycle.txt)、[版本與載入模組](results/20261024-exclamation/manifest.json)。
