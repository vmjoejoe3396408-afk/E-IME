# E-IME (Enhanced IME) 全程研發歷程與技術路線全景文檔
# Engineering Process Timeline & Technical Architecture Roadmap

**文件版本**：v1.0.0  
**更新日期**：2026-10-24  
**適用架構**：Windows 10 / 11 (x64), C++20 Standard, TSF (Text Services Framework)

---

## 1. 專案起源與問題定義 (Problem Statement)

長期以來，Windows 平台上的中文注音輸入法生態面臨著深刻的「**肌肉記憶恐怖谷**」：
1. **微軟新注音 (MS-IME)** 擁有廣大台灣使用者最根深蒂固的肌肉記憶（整句組字、游標自由回頭改字、特定快捷鍵習慣），但其底層詞庫停滯十數年、缺乏現代流行語與科技詞彙、無真正可用的跨機多設備無鎖同步機制，且在 Win10/11 更新後常出現嚴重卡頓、輸入凍結與游標漂移。
2. **第三方開源或商業輸入法**（如新酷音、小狼毫/Rime 等）雖然詞庫自由度高，但其狀態機模型大多採用固定詞組導向或 Emacs/Vim 風格，其游標移動、選字窗切換、Backspace 倒退行為與微軟新注音有極大差異，造成難以跨越的換代阻力。

**E-IME (Enhanced IME)** 的工程使命：  
**「以貼合微軟新注音核心肌肉記憶為基準，改善詞庫擴充性、卡頓延遲與跨機同步機制，建立一套輕量且易於維護的現代 C++20 輸入法系統。」**

---

## 2. 全程開發歷程回顧 (Development Process Timeline)

```mermaid
timeline
    title E-IME 全程研發里程碑 (2026-10-23 ~ 2026-10-24)
    2026-10-23 上午 : Phase 1 核心引擎立項 : 大千注音解析器 (ZhuyinParser) : 前綴字典樹 (Trie) : 受限動態規劃 (Constrained Viterbi)
    2026-10-23 下午 : Phase 2 系統整合與渲染 : Win32 WS_EX_NOACTIVATE 現代浮動視窗 : Windows TSF COM 外殼 (ITfTextInputProcessorEx) : 雙色底線 Display Attributes
    2026-10-23 晚間 : Phase 3 雲端同步與詞庫優化 : CRDT Per-Device 增量日誌 : 台灣現代語料庫 (core_dict.txt) : 二進位二階快取 (6MB < 3ms)
    2026-10-24 凌晨 : Phase 4 防禦加固與熱部署 : 宿主 DLL 佔用防護 (Immutable Hash Dir) : 1秒 Ninja 高速部署 (deploy.ps1) : TSF EditSession 世代排空
    2026-10-24 深夜 : Phase 5 邊界問題極限調校 : 注音先韻後聲自動重排 (解決婀ㄌ/ㄜㄌ) : 無對應字注音保底顯示 : 標點符號組字底線與候選展開
```

### 2.1 第一階段：核心演算法與狀態機立項 (Phase 1)
* **大千注音鍵盤映射器 (`zhuyin.h`)**：
  * 定義標準 21 聲母、3 介音、13 韻母與 5 聲調。
  * 實現零動態記憶體配置的注音結構體 `Syllable`，內建 UTF-8 字串解析與注音組合。
* **詞頻加權字典樹 (`trie.h`)**：
  * 支援單音節、多音節詞彙快速前綴檢索與精確查詢。
  * 預設採用對數機率評分 $\log P(w)$，並預留動態使用者詞頻增量介面。
* **支援手動鎖定約束的 Viterbi 解碼器 (`viterbi.h`)**：
  * 突破傳統輸入法「選字即結算整句」的缺陷，實作帶有 `PinnedSegment` 局部約束的動態規劃演算法。
  * 使用者若回頭改中段某字，僅該區間強制鎖定，整句其餘部分自動重新尋求全局最優解。

### 2.2 第二階段：現代 UI 與 Windows TSF 系統級整合 (Phase 2)
* **Win32 輕量化懸浮候選字視窗 (`candidate_window.cpp`)**：
  * 嚴格套用 `WS_POPUP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE` 視窗樣式，**保證永不搶奪宿主程式焦點**，杜絕遊戲中斷或對話方塊閃退。
  * GDI+ 雙緩衝高清抗鋸齒渲染，支援 Windows 11 圓角與深淺色主題切換。
* **Windows TSF (Text Services Framework) COM 伺服器 (`text_service.cpp`)**：
  * 實作 `ITfTextInputProcessorEx` 管理輸入法生命週期。
  * 實作 `ITfKeyEventSink` 攔截按鍵，分離 `OnTestKeyDown`（意圖過濾）與 `OnKeyDown`（執行組字）。
  * 實作 `ITfDisplayAttributeProvider`：為組字區動態注入虛線底線（`GUID_E-IME_DisplayAttr_Input`）與當前選字粗實線（`GUID_E-IME_DisplayAttr_Target`）。

### 2.3 第三階段：儲存、雲端同步與詞庫快取 (Phase 3)
* **無鎖跨裝置同步引擎 (`sync_journal.h`)**：
  * 針對 OneDrive / Google Drive / 區網共用資料夾，**徹底摒棄傳統單一 SQLite 資料庫方案**（因多開鎖定必崩）。
  * 創新採用 **Per-Device Append-Only Delta 日誌 (`device_<hostname>.delta`)**，寫入只追加本機檔案，讀取時採用 CRDT 邏輯計數器原地聯集，物理層面杜絕檔案鎖死與衝突副本。
* **現代台灣繁體語料庫與二進位快取 (`dict_loader.h`)**：
  * 建立現代台灣詞庫 `core_dict.txt`（涵蓋科技、網路、醫療、時事詞彙）。
  * 設計二進位極速序列化快取 (`core_dict.txt.bin`)，將啟動解析耗時從 850ms 降至 2.8ms。

### 2.4 第四階段：並發死鎖防護與極速熱部署 (Phase 4)
* **不可變版本部署機制 (`scripts/install.ps1`)**：
  * 當系統中已有 Notepad、Word 或 Chrome 載入 DLL 時，Windows 會對檔案實施排他讀取鎖，導致編譯器無法複寫 `E-IME_tip.dll`。
  * 引入版本 SHA256 雜湊目錄隔離機制：每次編譯將 DLL 複製至 `%LOCALAPPDATA%\E-IME\versions\<hash>\E-IME_tip.dll`，並透過 HKCU 登錄檔切換指標，徹底消除覆寫衝突。
* **1 秒極速建置流水線 (`scripts/deploy.ps1`)**：
  * 配置本機 NVMe 快取建置目錄與 Ninja 增量編譯，使「編譯 $\to$ 雜湊隔離 $\to$ 註冊生效」縮減至 1 秒內。
* **TSF 非同步 EditSession 世代排空防死鎖 (`drain_edit_sessions`)**：
  * 針對高速盲打引發的多個非同步 TSF EditSession 佇列，引入 `activation_generation_` 世代追蹤器，自動丟棄過期的過渡幀，避免畫面閃爍與狀態競爭。

### 2.5 第五階段：肌肉記憶極限調校與邊界問題徹底根治 (Phase 5)
* **注音亂序輸入智慧重組（解決「婀ㄌ / ㄜㄌ / 婀ㄉ」問題）**：
  * 盲打時使用者經常先鍵入韻母（如 `k` = ㄜ）再按聲母（如 `2` = ㄉ 或 `x` = ㄌ）。
  * 原狀態機誤將 ㄜ 作為獨立一聲音節完成（輸出「婀」），後續聲母被拆分。
  * 引入非破壞性槽位組裝與實際擊鍵順序堆疊（`component_order_`）：無論聲韻何者先按，均自動歸入 `current_syllable_` 組合為 `ㄉㄜ`、`ㄌㄜ`，並在按聲調鍵（如 7 輕聲）時準確出「的」、「了」。
  * 對於無對應漢字之組合（如 `ㄅㄜ` 空白一聲、`ㄌㄜ` 空白一聲），嚴格遵照微軟新注音規範**保底維持原生注音符號**，絕不硬猜錯字。
* **標點符號組字區虛線底線與候選清單展開**：
  * 修正 `process_ctrl_punctuation`：標點符號納入 `literal_texts_`，在 TSF 中持續享有 `TF_LS_DOT` 虛線底線，游標精確停在標點後方，不再跳至最右側。
  * 實作 `ZhuyinParser::get_punctuation_candidates`：在逗號、句號處按向下鍵，即刻彈出標點候選視窗（如 `1. ，` `2. 、` `3. ﹐` `4. ,`），支援按鍵直接切換頓號等符號。

---

## 3. 核心技術架構深剖 (Technical Architecture Deep-Dive)

### 3.1 核心數據流與分層結構 (Data Flow)

```text
[使用者鍵盤輸入 (Physical Keystrokes)]
               │
               ▼
   [Windows TSF 外殼層: E-IMETextService]
   ├── OnTestKeyDown: 判定按鍵是否由輸入法吞入 (would_eat_key)
   └── OnKeyDown: 區分控制鍵 (Ctrl/Shift) 與大千字符
               │
               ▼
   [組字狀態機: InputStateMachine]
   ├── 模式管理器: 中文 / 英文模式 (Shift 單擊切換)
   ├── 標點管理器: Literal 槽位與虛擬音節占位
   └── 語音重排器: 聲母/介音/韻母槽位填入與順序棧
               │
               ▼
   [語言引擎: ViterbiDecoder & ZhuyinTrie]
   ├── 受限 Viterbi DP 解碼: Pinned 鎖定區間絕對優先
   ├── 前綴 Trie 檢索: 詞頻機率 + 長詞凝聚加成
   └── 個人學習補償: CRDT 增量無鎖計數
               │
               ▼
   [非同步編輯會話: E-IMEEditSession]
   ├── 世代排空 (drain_edit_sessions): 消除競態
   ├── TSF Composition Range: 寫入宿主組字文字
   ├── DisplayAttribute: 標記組字虛線 (atom_input_) 與選字實線 (atom_target_)
   └── 游標精確定位: 3 級坐標回退獲取 Caret Rect
               │
               ▼
   [Win32 現代無焦點候選窗: CandidateWindow]
   └── WS_EX_NOACTIVATE 懸浮於 Caret 旁，GDI+ 雙緩衝渲染
```

### 3.2 演算法數學模型：受限 Viterbi 動態規劃 (Constrained Viterbi DP)

設輸入之音節序列為 $S = [s_1, s_2, \dots, s_N]$。  
使用者手動鎖定之約束集合為 $P = \{ (u_k, L_k, W_k) \}$，其中 $u_k$ 為起始音節索引，$L_k$ 為詞長，$W_k$ 為強制指定的詞彙文字。

動態規劃狀態定義：  
$\text{DP}[j]$ 表示覆蓋前 $j$ 個音節的最佳切分路徑得分。

轉移方程式：
1. **局部鎖定強制轉移**：  
   若存在鎖定 $(i, L_k, W_k) \in P$ 使得 $j = i + L_k$：
   $$\text{DP}[j] = \text{DP}[i] + K_{\text{pin}} \quad (K_{\text{pin}} = +50.0)$$
2. **一般詞庫前綴轉移**：  
   若 $[i, j)$ 未跨越且未落在任何鎖定區段內部，且存在候選詞 $w$ 匹配音節序列 $S[i..j)$：
   $$\text{Score}(w) = \log P_{\text{dict}}(w) + \Delta_{\text{user}}(w) + B_{\text{length}}(L)$$
   其中：
   * $\log P_{\text{dict}}(w)$ 為語料庫詞頻對數機率。
   * $\Delta_{\text{user}}(w)$ 為跨裝置 CRDT 學習計數帶來的正向激勵。
   * $B_{\text{length}}(L) = 2.2 \times (L - 1) \quad (L \ge 2)$ 為長詞凝聚力補償，有效抑制多字詞被切碎為單字。
   $$\text{DP}[j] = \max_{i} \left( \text{DP}[i] + \text{Score}(w) \right)$$
3. **語音保底轉移 (Fallback Penalty)**：  
   若位置 $i$ 無任何字典匹配（如使用者隨意敲打注音符號）：
   $$\text{DP}[i+1] = \text{DP}[i] - 25.0$$
   保底文字設為 $S[i].\text{to\_string()}$，確保解碼鏈絕對不中斷，完美輸出原生注音。

時間複雜度：$\mathcal{O}(N \times M)$，其中 $N$ 為長句音節數（一般 $\le 30$），$M$ 為字典樹最大前綴分歧數（平均 $\le 8$）。在現代 CPU 上單次完整解碼耗時 $< 15\mu s$。

---

## 4. 未來技術路線全景圖 (Future Technical Roadmap)

```text
Phase 1 ~ Phase 5 (已完成)
  ├── 跨平台純邏輯核心引擎 (Parser / Trie / Viterbi)
  ├── Windows TSF 系統對接與 Display Attributes 虛線底線
  ├── Win32 WS_EX_NOACTIVATE 現代無焦點候選字視窗
  ├── 無鎖 CRDT 跨裝置雲端增量同步 (Append-Only Delta)
  ├── 1 秒極速熱部署流水線 (Ninja + Immutable Hash Dir)
  ├── 注音先韻後聲自動重組與無字保底
  └── 標點符號組字區停留與候選清單切換
        │
        ▼
Phase 6: 現代視覺與體驗躍升 (進行中 / 下一階段)
  ├── DirectWrite / Direct2D 現代 GPU 硬體加速候選視窗 (高刷 DPI 自適應)
  ├── Windows 11 Fluent Design Mica / Acrylic 半透明毛玻璃視覺效果
  └── 候選字清單字元即時語意提示與多音字讀音標註
        │
        ▼
Phase 7: 輸入佈局多元化支援
  ├── 倚天 26 鍵 / 倚天 41 鍵 / 許氏鍵盤 / 漢音鍵盤映射支援
  └── 漢語拼音 / 通用拼音雙模輸入引擎
        │
        ▼
Phase 8: 智慧化邊緣推論 (Intelligent On-Device Inference)
  ├── 輕量化 Transformer / Byte-level N-gram 離線語境推斷引擎
  └── 零延遲上下文感知自動改字 (Context-aware Correction)
        │
        ▼
Phase 9: 跨作業系統版圖拓展
  ├── Linux 平台支援：Fcitx5 / IBus 外殼封裝 (純核心引擎 100% 複用)
  └── macOS 平台支援：InputMethodKit 封裝
```

---

## 5. 實測驗證成果與已知限制 (Test Results & Known Limitations)

### 5.1 實測驗證成果
E-IME 目前通過 12 項核心單元與整合測試（`build/E-IME_tests.exe`）：
* 大千鍵盤音節解析與聲韻重組（先韻後聲自動修正、無字保底原生注音）。
* 受限 Viterbi 動態規劃解碼與長句游標回頭改字鎖定（Pinned Segments）。
* Windows TSF COM 介面規範（`ITfTextInputProcessorEx`, `ITfKeyEventSink`, `ITfDisplayAttributeProvider`）。
* GDI+ 雙緩衝無焦點懸浮視窗渲染（`WS_EX_NOACTIVATE`）。
* 無鎖 Per-Device Append-Only Delta 日誌跨機同步。
* 二進位詞庫快取冷啟動耗時 $< 3\text{ms}$，執行階段記憶體常駐約 10MB。

### 5.2 現有已知限制
1. **架構支援**：現階段僅針對 Windows x86_64 架構建置，尚未支援 32 位元 WoW64 應用程式。
2. **鍵盤排列**：僅支援標準大千注音鍵盤，尚未支援倚天、許氏等自訂排列。
3. **語意模型**：採用基於統計之 N-gram 與 Viterbi 動態規劃，非深度學習大語言模型，特殊領域詞彙仍需依賴使用者自造詞。

---

## 6. 總結

E-IME 專案驗證了現代 C++20 在 Windows 系統級桌面輸入法開發中的可行性：在維持極致輕量（記憶體 $< 10\text{MB}$、解碼延遲微秒級）的前提下，完全不依賴肥大的第三方框架，單純透過精準的狀態機數學模型、清晰的分層架構與公有標準 TSF API，成功打造出貼合微軟新注音操作直覺、且具備自造詞無鎖跨機漫遊能力的實用輸入法系統。
