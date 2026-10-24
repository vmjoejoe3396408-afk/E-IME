# E-IME 獨立實作歷程與架構對照紀錄
# Independent Implementation & Architecture Provenance Notes

**專案名稱**：基於 MS-IME 操作習慣之現代注音輸入法 (E-IME)  
**文件版本**：v1.1.0  
**更新日期**：2026-10-24  
**文件目的**：記錄 E-IME 在不接觸任何微軟專有原始碼、反組譯二進位檔案或內部私有結構之前提下，基於公開標準與黑盒外部行為觀察之獨立自研實作歷程、來源依據與實測結果。

---

## 1. 研發原則與資訊隔離機制

在系統級軟體重構過程中，本專案依循標準軟體工程中的獨立開發（Clean-Room Design）原則，確保技術實現的透明性與合法性：

1. **外部黑盒行為規格（Functional & Black-Box Specification）**：
   * **規格提出**：使用者依據微軟新注音（MS-IME）數十年累積的操作直覺與打字習慣，提出具體的邊界測試案例（例如：先按韻母再按聲母的容錯、標點符號組字底線與候選清單展開、長句回頭改字之局部鎖定等）。
   * **非接觸原則**：未曾使用反組譯器（如 IDA Pro、Ghidra）逆向分析微軟閉源元件（如 `msutb.dll`、`imjp10.ime`、`ChsIME.dll`），亦未讀取微軟專有內部程式碼。

2. **自研架構與公有標準實現（Independent Implementation）**：
   * **開發實現**：程式碼完全由空白工程開始撰寫，演算法設計採用計算機科學公有領域已知模型（前綴字典樹 Trie、動態規劃 Viterbi、CRDT 增量日誌）。
   * **系統介面**：嚴格依循微軟公開出版之 MSDN Windows Text Services Framework (TSF) 與 Win32 API 規範進行介面對接。

---

## 2. 核心技術模組來源與對照

以下為 E-IME 核心模組與傳統系統輸入法之實作技術對照：

| 模組 | 微軟新注音 (MS-IME) 實作方式 | E-IME 獨立實作方式 | 技術來源與參考依據 |
| :--- | :--- | :--- | :--- |
| **注音音節模型** | 內部封閉式狀態轉移矩陣 | 自研 C++20 `Syllable` 結構體與 `ZhuyinParser`，以位元遮罩與槽位管理按鍵 | 教育部公布之國語注音符號規則與標準大千鍵盤佈局 |
| **整句最佳化解碼** | 專有 HMM / 類神經轉換引擎 | 自研 `ZhuyinTrie` 前綴字典樹 + **支援手動鎖定約束（Pinned Constraints）的動態規劃 Viterbi 演算法** | 經典 Viterbi 動態規劃演算法 |
| **字詞資料庫** | 閉源二進位 `.lex` 檔與系統註冊表綁定 | 開放純文字 UTF-8 詞庫 (`core_dict.txt`) 與二進位快取序列化器 (`dict_loader.h`) | 衍生自開源 `libchewing-data` 詞庫（LGPL 2.1） |
| **跨裝置個人學習** | 封閉式漫遊機制或雲端微軟帳號同步 | 自研 **Per-Instance Append-Only Delta Journal (`device_<hostname>_<writer-id>.delta`)**，以每檔各詞累計值合併新增量 | CRDT（無衝突複製資料型態）無鎖聯集原則 |
| **繁簡漢字轉換** | 系統語言套件轉換 | 自研 `hanzi_conv.h` 極速查表轉碼器 (`Ctrl + Shift + F`) | 衍生自開源 `OpenCC` 繁簡字元對照表（Apache 2.0） |
| **Windows 系統整合** | 私有未公開 TIP 介面與系統掛鉤 | 依據微軟公開 MSDN 文件實作之標準 COM 介面：`ITfTextInputProcessorEx`、`ITfKeyEventSink`、`ITfDisplayAttributeProvider`、`ITfEditSession` | 微軟官方 MSDN Text Services Framework API |
| **候選字懸浮窗** | 現代 XAML / DirectComposition 系統元件 | 獨立實作之 Win32 API 輕量化無焦點浮動視窗 (`WS_EX_NOACTIVATE`)，搭配純 GDI 高清雙緩衝渲染 | Windows Win32 API 視窗規範 |

---

## 3. 典型邊界行為之自研案例

在開發過程中，對於使用者反饋之特定操作習慣，均以純黑盒狀態機重新推導並實作：

1. **案例一：注音輸入先韻後聲自動重組（解決「婀ㄌ / ㄜㄌ」問題）**：
   * *外部現象*：使用者盲打時若先鍵入韻母鍵 `k` (ㄜ) 再按聲母鍵 `x` (ㄌ)，預期應組合為 `ㄌㄜ`，而非將 `ㄜ` 結算成「婀」並把 `ㄌ` 當成下一音節。
   * *實作機制*：在 `engine/include/input_state_machine.h` 中設計槽位重填機制與鍵入順序堆疊（`component_order_`），無論聲韻輸入先後順序，均組裝入對應欄位，並於無對應漢字（如空白一聲）時維持原生注音符號不硬猜字。
2. **案例二：標點符號組字區虛線底線與候選清單**：
   * *外部現象*：句中輸入標點符號需保留在組字區內帶有底線，按向下鍵可展開替代符號選單。
   * *實作機制*：在組字槽位引入 `literal_texts_` 虛擬音節結構，並透過公開的 TSF `ITfProperty` 規範附加 `TF_LS_DOT` 虛線底線，游標停留在標點時按向下鍵呼叫 `ZhuyinParser::get_punctuation_candidates()` 展開候選。

---

## 4. 實測驗證結果 (Test Verification Results)

專案目前內建全自動化單元與整合測試套件（[`tests/test_main.cpp`](../tests/test_main.cpp)），最新實測結果（13/13 測試全數通過）：

* **[PASS] 測試 1**：大千鍵盤映射與注音音節解析器。
* **[PASS] 測試 2**：長句游標回頭局部改字與手動鎖定重算（Constrained Viterbi）。
* **[PASS] 測試 3**：跨裝置無鎖增量同步與自造詞漫遊（Per-Device Delta Journal）。
* **[PASS] 測試 4**：Shift 中英切換與全形標點符號。
* **[PASS] 測試 5**：Win32 WS_EX_NOACTIVATE 現代淺色候選字視窗建立與雙緩衝渲染。
* **[PASS] 測試 6**：Windows TSF COM 伺服器動態載入、類別工廠實例化與 COM 生命週期。
* **[PASS] 測試 7**：繁簡中文即時切換與轉碼（`Ctrl + Shift + F`）。
* **[PASS] 測試 8**：詞庫二進位極速快取序列化與反序列化（啟動耗時 $< 3\text{ms}$）。
* **[PASS] 測試 9**：句中插入音節預覽與 UTF-16 游標偏移追蹤。
* **[PASS] 測試 10**：高頻多音字（些、得、長、行）與方向鍵候選導航。
* **[PASS] 測試 11**：微軟新注音音節語音規範與智慧重組機制（先韻後聲、拒絕第 4 個注音早期中斷、無字保底）。
* **[PASS] 測試 12**：句中標點符號組字區保留與精確游標定位。
* **[PASS] 測試 13**：單字、整詞與句中選字可反覆改選與明確確認上屏。

---

## 5. 已知限制與適用範圍 (Known Limitations)

本專案現階段仍存在以下明確的架構與環境限制，使用者與評估者應予以知悉：

1. **64 位元架構優先**：
   * 目前發布與測試主要針對 Windows x86_64（64 位元）原生應用程式。尚未針對 32 位元舊型應用程式編譯與註冊 32 位元 WoW64 DLL。
2. **鍵盤排列限制**：
   * 目前僅實作標準大千（微軟注音鍵盤）排列，暫未支援倚天 26 鍵、倚天 41 鍵、許氏鍵盤或漢音鍵盤。
3. **語意模型邊界**：
   * 整句解碼依賴統計詞頻與受限 Viterbi 動態規劃，並非基於深度學習大語言模型（LLM）。在遇到冷門專業術語或長跨度多歧義句型時，仍需依賴使用者手動選字確認。
4. **宿主相容性限制**：
   * 完全依賴標準 Windows TSF 協定。在極少數完全未實作 TSF 或採用專有自繪輸入框之舊式 Direct3D 獨占全螢幕遊戲中，行內組字底線可能無法正常顯示。
