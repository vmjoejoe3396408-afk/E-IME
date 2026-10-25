# 實作依據與資料來源

更新日期：2026-10-25。本文整理目前可核對的實作依據、第三方資料與程式碼位置；功能以 `feature/aggressive-correction` 為準，歷史功能驗證見 [測試索引](../testbench/README.md)。

## 開發方式與紀錄範圍

**本專案的全部程式碼均由 AI 生成，作者沒有手寫任何一行程式碼，僅提供指示。** AI 依照作者的指示生成程式碼及後續修改；不將作者描述為手寫實作的程式設計者。

這項聲明描述本專案程式碼的產生方式。libchewing 詞庫與 OpenCC 繁簡映射仍屬第三方來源資料，依下表及第三方聲明記錄其來源與授權，不改稱為 AI 原創資料。

E-IME 以注音使用需求、操作行為觀察及公開的 Windows TSF／Win32 介面為實作依據。字詞資料與繁簡映射使用下表列出的第三方來源，並保留相應授權及轉換腳本。

現有紀錄沒有建立可供核對的規格與實作人員隔離、資訊流管制及流程審查證據。因此，本專案不宣稱採用正式「淨室開發」（clean-room development），本文也不是合規稽核或法律認證。「自行實作」與「採用淨室流程」是不同層次的描述，不能互相替代。

微軟注音的操作習慣作為互動參考；本文不推測其內部資料格式、模型或私有介面。E-IME 並非微軟官方專案。來源記錄應以提交歷史、引用資料、授權與實際流程證據為依據，不由功能測試通過推導整體來源或法律結論。

## 實作與資料來源

| 項目 | E-IME 實作／資料 | 來源與紀錄 |
| --- | --- | --- |
| 注音解析 | `Syllable`、`ZhuyinParser`，以字串槽位保存聲母、介音、韻母及聲調 | 標準大千排列與注音規則；[zhuyin.h](../engine/include/zhuyin.h) |
| 詞彙查詢 | `ZhuyinTrie`，含詞頻與個人學習權重 | 專案內實作；[trie.h](../engine/include/trie.h) |
| 組字與解碼 | `InputStateMachine`、`ViterbiDecoder`、`PinnedSegment` | 專案內的狀態機與動態規劃；[架構文件](ARCHITECTURE.md) |
| 語境修正 | `compute_transition_score()` 中的有限固定字詞搭配規則 | 可在程式碼中檢查的固定規則，非訓練式模型 |
| 核心詞庫 | `data/core_dict.txt` | libchewing-data 衍生資料，LGPL-2.1-or-later，含專案詞彙合併及頻率調整 |
| 繁簡映射 | `hanzi_conv.h` 內的單字元查表資料 | OpenCC 衍生資料，Apache-2.0；E-IME 轉換程式碼依 MIT 分發 |
| 個人學習 | `SyncJournal` 每實例日誌、累計值新增量合併 | 專案內實作；[sync_journal.h](../sync/include/sync_journal.h) |
| 系統外殼 | `ITfTextInputProcessor`、`ITfKeyEventSink`、`ITfCompositionSink`、`ITfDisplayAttributeProvider` 與編輯會話 | 公開 Windows TSF／COM API；[text_service.h](../tsf/text_service.h) |
| 候選窗 | Win32 無啟用浮窗與 GDI 雙緩衝 | 公開 Win32 API；[candidate_window.cpp](../ui/candidate_window.cpp) |

第三方資料的固定 commit、原作者聲明、修改內容、完整授權及重建命令，集中於 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)，不在多份文件重複維護。

## 由使用需求推導的行為

- 先韻後聲：音節尚未完成時，依槽位重排聲韻，並記錄實際輸入順序供 Backspace 回退。
- 回頭選字：手選區段用 `PinnedSegment` 鎖定，其他未鎖定區段重新解碼；選字與整句提交分成兩次動作。
- 標點留在組字：標點用 literal 槽位保存，參與游標、刪除及候選替換。主線歷史報告描述虛線，本功能分支已改實線。
- 快速打字補全：聲調提早但前一音節尚無精確詞庫匹配時，嘗試吸收晚到的聲韻；保護有效單字及手選詞。

這些規則由 E-IME 的程式碼與測試描述；不宣稱與微軟注音在所有邊界情況完全一致。

## 驗證紀錄的範圍

功能分支核心測試為 15 個模組，另有同步回歸、壓力及兩組 TSF 測試，共 5 組 CTest。2026-10-25 本機重建通過；當前 UI 的實機視覺、各宿主相容性及真實跨機雲端傳輸仍須分別記錄證據。

2026-10-23～24 的日期報告保留當時結果，不把後續功能補寫為當時已驗證。文件未把固定記憶體、奈秒轉碼或冷啟動數字列為無條件保證。
