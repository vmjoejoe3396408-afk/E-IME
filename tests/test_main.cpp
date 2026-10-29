#include <iostream>
#include <cassert>
#include <filesystem>
#include <string>
#include <vector>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <msctf.h>
#endif

#include "../engine/include/zhuyin.h"
#include "../engine/include/trie.h"
#include "../engine/include/viterbi.h"
#include "../engine/include/dict_loader.h"
#include "../engine/include/input_state_machine.h"
#include "../sync/include/sync_journal.h"
#include "../ui/candidate_window.h"
#include "../tsf/display_attribute.h"

// Standalone test executable also instantiates the inline COM metadata classes.
namespace eime { std::atomic<long> g_dllRefCount{0}; }

#define EIME_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (Line " << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (0)

bool test_zhuyin_parser() {
    std::cout << "[TEST 1] 驗證大千鍵盤映射與注音音節解析器..." << std::endl;
    auto s1 = eime::ZhuyinParser::parse_zhuyin_string("ㄅㄧㄢˋ");
    EIME_ASSERT(s1.consonant == "ㄅ", "聲母應為 ㄅ");
    EIME_ASSERT(s1.medial == "ㄧ", "介音應為 ㄧ");
    EIME_ASSERT(s1.rhyme == "ㄢ", "韻母應為 ㄢ");
    EIME_ASSERT(s1.tone == eime::Tone::Tone4, "聲調應為四聲");
    EIME_ASSERT(s1.to_string() == "ㄅㄧㄢˋ", "to_string 應還原為 ㄅㄧㄢˋ");

    auto multi = eime::ZhuyinParser::parse_multi_syllables("ㄊㄞˊ ㄐㄧ ㄉㄧㄢˋ");
    EIME_ASSERT(multi.size() == 3, "台積電應解析為 3 個音節");
    EIME_ASSERT(multi[0].to_string() == "ㄊㄞˊ", "第 1 音節應為 ㄊㄞˊ");
    EIME_ASSERT(multi[1].to_string() == "ㄐㄧ", "第 2 音節應為 ㄐㄧ");
    EIME_ASSERT(multi[2].to_string() == "ㄉㄧㄢˋ", "第 3 音節應為 ㄉㄧㄢˋ");

    std::cout << "  -> [PASS] 大千注音解析器驗證通過" << std::endl;
    return true;
}

bool test_mid_sentence_cursor_edit(eime::ZhuyinTrie& trie, const eime::ViterbiDecoder& decoder) {
    std::cout << "[TEST 2] 驗證 MS-IME 核心肌肉記憶：長句游標回頭局部改字 (Constrained Viterbi)..." << std::endl;
    eime::InputStateMachine sm(trie, decoder);
    std::string out;

    // 輸入：「今天公式很好」 -> rup (今) wu0 (天) ej/ (公) g4 (式) cp3 (很) cl3 (好)
    std::string keys = "rup wu0 ej/ g4cp3cl3";
    for (char c : keys) {
        sm.process_key(c, out);
    }
    EIME_ASSERT(sm.composition_text() == "今天公式很好", "初始整句預測應為『今天公式很好』，實際為: " + sm.composition_text());

    // 使用者發現「公式」想改成同音詞「公事」：
    // 按左方向鍵 4 次，將游標移到「公」(index 2)
    sm.process_special_key(eime::SpecialKey::LeftArrow, out);
    sm.process_special_key(eime::SpecialKey::LeftArrow, out);
    sm.process_special_key(eime::SpecialKey::LeftArrow, out);
    sm.process_special_key(eime::SpecialKey::LeftArrow, out);
    EIME_ASSERT(sm.cursor_idx() == 2, "游標應移至音節索引 2");

    // 按下方向鍵展開該位置候選詞
    sm.process_special_key(eime::SpecialKey::DownArrow, out);
    EIME_ASSERT(sm.state() == eime::EngineState::CandidateOpen, "應進入 CandidateOpen 狀態");

    auto page = sm.current_page_candidates();
    EIME_ASSERT(page.size() >= 2, "『ㄍㄨㄥ ㄕˋ』應有多個同音候選詞");
    // 尋找「公事」在候選清單中的編號並選取
    char target_key = '2';
    for (size_t i = 0; i < page.size(); ++i) {
        if (page[i].text == "公事") {
            target_key = static_cast<char>('1' + i);
            break;
        }
    }
    sm.process_key(target_key, out);

    // 選完後，前後的「今天」與「很好」必須保持不變，中間鎖定為「公事」！
    EIME_ASSERT(sm.composition_text() == "今天公事很好", "局部改字後整句應為『今天公事很好』，實際為: " + sm.composition_text());

    // 按 Enter 確認整句上屏
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out == "今天公事很好", "Enter 上屏文字應為『今天公事很好』");
    EIME_ASSERT(sm.state() == eime::EngineState::Idle, "上屏後應回到 Idle 狀態");

    std::cout << "  -> [PASS] 長句游標移動與局部鎖定重算驗證通過" << std::endl;
    return true;
}

bool test_multi_device_cloud_sync() {
    std::cout << "[TEST 3] 驗證無鎖跨裝置雲端同步 (Per-Device Delta Journal: 桌機 A -> 筆電 B)..." << std::endl;

    std::filesystem::path sync_dir = "build/test_cloud_sync";
    std::error_code ec;
    std::filesystem::remove_all(sync_dir, ec);

    // 準備兩台獨立設備的字典樹：Desktop_A 與 Laptop_B
    eime::ZhuyinTrie trie_desktop_a;
    eime::ZhuyinTrie trie_laptop_b;
    eime::DictLoader::load_from_file("data/core_dict.txt", trie_desktop_a, false);
    eime::DictLoader::load_from_file("data/core_dict.txt", trie_laptop_b, false);

    eime::SyncJournal journal_a(sync_dir, "Desktop_A");
    eime::SyncJournal journal_b(sync_dir, "Laptop_B");

    // 1. 驗證未學習前，筆電 B 輸入 "ej/ g4\r" (ㄍㄨㄥ ㄕˋ) 預設第一候選為「公式」
    eime::ViterbiDecoder decoder_b(trie_laptop_b);
    {
        eime::InputStateMachine sm_b(trie_laptop_b, decoder_b, &journal_b);
        std::string committed;
        for (char c : std::string("ej/ g4\r")) sm_b.process_key(c, committed);
        EIME_ASSERT(committed == "公式", "同步前筆電 B 預設應輸出『公式』");
    }

    // 2. 在桌機 A 上，使用者多次選用「攻勢」或自造新詞「矽盾」(ㄒㄧˋ ㄉㄨㄣˋ)
    auto gongshi_syls = eime::ZhuyinParser::parse_multi_syllables("ㄍㄨㄥ ㄕˋ");
    journal_a.record_learning(trie_desktop_a, gongshi_syls, "攻勢", 3);

    auto xidun_syls = eime::ZhuyinParser::parse_multi_syllables("ㄒㄧˋ ㄉㄨㄣˋ");
    journal_a.record_learning(trie_desktop_a, xidun_syls, "矽盾", 2);

    // 桌機 A 執行日誌壓縮 (Compaction)
    EIME_ASSERT(journal_a.compact_own_journal(), "桌機 A 日誌壓縮應成功");

    // 3. 筆電 B 透過雲端資料夾執行增量同步 sync_all_devices()
    size_t merged = journal_b.sync_all_devices(trie_laptop_b);
    EIME_ASSERT(merged >= 2, "筆電 B 應成功合併來自桌機 A 的 2 筆學習紀錄");

    // 4. 驗證筆電 B 現在打 "ej/ g4\r" 自動優先輸出「攻勢」，且打 "vu42jp4\r" (ㄒㄧˋ ㄉㄨㄣˋ) 能打出新詞「矽盾」！
    {
        eime::InputStateMachine sm_b(trie_laptop_b, decoder_b, &journal_b);
        std::string committed1, committed2;
        for (char c : std::string("ej/ g4\r")) sm_b.process_key(c, committed1);
        EIME_ASSERT(committed1 == "攻勢", "同步後筆電 B 應自動將『攻勢』列為第一順位，實際為: " + committed1);

        for (char c : std::string("vu42jp4\r")) sm_b.process_key(c, committed2);
        EIME_ASSERT(committed2 == "矽盾", "同步後筆電 B 應能直接打出桌機 A 學習的新詞『矽盾』，實際為: " + committed2);
    }

    std::cout << "  -> [PASS] 跨裝置無鎖增量同步與自造詞漫遊驗證通過" << std::endl;
    return true;
}

bool test_shift_and_punctuation(eime::ZhuyinTrie& trie, const eime::ViterbiDecoder& decoder) {
    std::cout << "[TEST 4] 驗證 Shift 中英切換與全形標點符號..." << std::endl;
    eime::InputStateMachine sm(trie, decoder);
    std::string out;

    // Shift keeps the composition; English joins the same buffer.
    for (char c : std::string("su3cl3")) sm.process_key(c, out);
    sm.process_special_key(eime::SpecialKey::ShiftToggle, out);
    EIME_ASSERT(out.empty() && sm.get_display_line() == "你好", "Shift 應保留尚未送出的組字");
    EIME_ASSERT(sm.mode() == eime::InputMode::English, "應切換為 English 模式");

    // 英文模式下按注音鍵應回傳 false (不攔截，直接放行給宿主)
    bool eaten = sm.process_key('a', out);
    EIME_ASSERT(eaten && sm.get_display_line() == "你好a", "英文應接在同一組字區");

    // 再次按 Shift 切回中文模式，並測試 Ctrl+, 與 [ ] 標點
    sm.process_special_key(eime::SpecialKey::ShiftToggle, out);
    EIME_ASSERT(sm.mode() == eime::InputMode::Chinese, "應切回 Chinese 模式");

    sm.process_ctrl_punctuation(',', out);
    EIME_ASSERT(out.empty() && sm.get_display_line() == "你好a，", "Ctrl+, 應在組字區插入全形逗號");

    sm.process_key('[', out);
    EIME_ASSERT(out.empty() && sm.get_display_line() == "你好a，「", "引號應保留在組字區");
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out == "你好a，「", "Enter 應送出整段中英標點");

    std::cout << "  -> [PASS] Shift 狀態切換與標點符號驗證通過" << std::endl;
    return true;
}

bool test_candidate_window_ui() {
    std::cout << "[TEST 5] 驗證 Win32 WS_EX_NOACTIVATE 現代候選字視窗建立與渲染..." << std::endl;
#ifdef _WIN32
    eime::CandidateWindow wnd;
    EIME_ASSERT(wnd.create(), "CandidateWindow 建立應成功");

    std::vector<eime::WordCandidate> cands = {
        {"台積電", 650000, 1, -1.0, 3},
        {"台灣", 950000, 0, -1.2, 2},
        {"台北", 750000, 0, -1.5, 2}
    };
    wnd.update_content("今天|台積電", cands, 0, 0, 1);
    wnd.show_at(300, 300);
    EIME_ASSERT(wnd.is_visible(), "CandidateWindow 顯示狀態應為 true");

    // 處理一次訊息佇列確保 WM_PAINT 無異常執行
    MSG msg;
    while (PeekMessageW(&msg, wnd.hwnd(), 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    wnd.hide();
    EIME_ASSERT(!wnd.is_visible(), "CandidateWindow 隱藏後應為 false");
    wnd.destroy();
#endif
    std::cout << "  -> [PASS] Win32 現代圓角候選字視窗渲染驗證通過" << std::endl;
    return true;
}

bool test_tsf_com_dll() {
    std::cout << "[TEST 6] 驗證 Windows TSF COM 伺服器 (eime_tip.dll / eime_tsf.dll) 動態載入與實例化..." << std::endl;
#ifdef _WIN32
    wchar_t configured_dll[32768] = {};
    GetEnvironmentVariableW(L"EIME_TEST_DLL", configured_dll, 32768);
    HMODULE hDll = configured_dll[0] ? LoadLibraryW(configured_dll) : LoadLibraryW(L"build\\eime_tip.dll");
    if (!hDll) hDll = LoadLibraryW(L"eime_tip.dll");
    if (!hDll) hDll = LoadLibraryW(L"build\\eime_tsf.dll");
    if (!hDll) hDll = LoadLibraryW(L"eime_tsf.dll");
    EIME_ASSERT(hDll != nullptr, "應能成功載入 eime_tip.dll (無外部遺失依賴)");

    using DllGetClassObjectFn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    using DllCanUnloadNowFn = HRESULT(WINAPI*)(void);

    auto fnGetClassObj = reinterpret_cast<DllGetClassObjectFn>(GetProcAddress(hDll, "DllGetClassObject"));
    auto fnCanUnload   = reinterpret_cast<DllCanUnloadNowFn>(GetProcAddress(hDll, "DllCanUnloadNow"));
    auto fnReg         = GetProcAddress(hDll, "DllRegisterServer");
    auto fnUnreg       = GetProcAddress(hDll, "DllUnregisterServer");

    EIME_ASSERT(fnGetClassObj != nullptr, "匯出表應包含 DllGetClassObject");
    EIME_ASSERT(fnCanUnload != nullptr, "匯出表應包含 DllCanUnloadNow");
    EIME_ASSERT(fnReg != nullptr, "匯出表應包含 DllRegisterServer");
    EIME_ASSERT(fnUnreg != nullptr, "匯出表應包含 DllUnregisterServer");

    static constexpr GUID kClsidEime = {
        0xd8e42b10, 0x7a5c, 0x4e91, {0x9b, 0x2a, 0x1f, 0x3c, 0x5d, 0x7e, 0x9a, 0x01}
    };

    IClassFactory* factory = nullptr;
    HRESULT hr = fnGetClassObj(kClsidEime, IID_IClassFactory, reinterpret_cast<void**>(&factory));
    EIME_ASSERT(SUCCEEDED(hr) && factory != nullptr, "DllGetClassObject 應成功取得 IClassFactory");

    std::cout << "[DEBUG 1] factory->CreateInstance..." << std::endl;
    ITfTextInputProcessor* tip = nullptr;
    hr = factory->CreateInstance(nullptr, IID_ITfTextInputProcessor, reinterpret_cast<void**>(&tip));
    EIME_ASSERT(SUCCEEDED(hr) && tip != nullptr, "IClassFactory::CreateInstance 應成功建立 ITfTextInputProcessor 實例");

    std::cout << "[DEBUG 2] QueryInterface KeySink..." << std::endl;
    ITfKeyEventSink* key_sink = nullptr;
    hr = tip->QueryInterface(IID_ITfKeyEventSink, reinterpret_cast<void**>(&key_sink));
    EIME_ASSERT(SUCCEEDED(hr) && key_sink != nullptr, "實例應支援 ITfKeyEventSink 介面");

    std::cout << "[DEBUG 3] QueryInterface DisplayAttributeProvider..." << std::endl;
    ITfDisplayAttributeProvider* dap = nullptr;
    hr = tip->QueryInterface(IID_ITfDisplayAttributeProvider, reinterpret_cast<void**>(&dap));
    EIME_ASSERT(SUCCEEDED(hr) && dap != nullptr, "實例應支援 ITfDisplayAttributeProvider 介面");

    std::cout << "[DEBUG 4] EnumDisplayAttributeInfo..." << std::endl;
    IEnumTfDisplayAttributeInfo* pEnum = nullptr;
    hr = dap->EnumDisplayAttributeInfo(&pEnum);
    EIME_ASSERT(SUCCEEDED(hr) && pEnum != nullptr, "應能列舉 DisplayAttributeInfo");

    std::cout << "[DEBUG 5] pEnum->Next..." << std::endl;
    ITfDisplayAttributeInfo* info = nullptr;
    ULONG fetched = 0;
    hr = pEnum->Next(1, &info, &fetched);
    EIME_ASSERT(SUCCEEDED(hr) && fetched == 1 && info != nullptr, "應能取得第 1 個 DisplayAttributeInfo (Input 實線)");

    std::cout << "[DEBUG 6] GetAttributeInfo..." << std::endl;
    TF_DISPLAYATTRIBUTE da = {};
    info->GetAttributeInfo(&da);
    EIME_ASSERT(da.lsStyle == TF_LS_SOLID, "組字底線樣式應為實線 (TF_LS_SOLID)");

    std::cout << "[DEBUG 7] Releases..." << std::endl;
    info->Release();
    pEnum->Release();
    dap->Release();

    key_sink->Release();
    tip->Release();
    factory->Release();

    std::cout << "[DEBUG 8] DllCanUnloadNow..." << std::endl;
    EIME_ASSERT(fnCanUnload() == S_OK, "所有 COM 物件釋放後 DllCanUnloadNow 應回傳 S_OK");
    std::cout << "[DEBUG 9] FreeLibrary..." << std::endl;
    FreeLibrary(hDll);
#endif
    std::cout << "  -> [PASS] Windows TSF COM DLL 匯出符號與 COM 生命週期驗證通過" << std::endl;
    return true;
}

bool test_traditional_simplified_toggle(eime::ZhuyinTrie& trie, const eime::ViterbiDecoder& decoder) {
    std::cout << "[TEST 7] 驗證繁簡中文即時切換與輸出 (Ctrl + Shift + F)..." << std::endl;

    // 1. 驗證 OpenCC 繁簡轉換函式
    std::string sample_trad = "繁體中文輸入法，台積電跨裝置雲端同步！";
    std::string sample_simp = eime::HanziConverter::to_simplified(sample_trad);
    EIME_ASSERT(sample_simp == "繁体中文输入法，台积电跨装置云端同步！",
        "HanziConverter 繁轉簡結果不符: " + sample_simp);

    // 2. 驗證狀態機中切換為簡體輸出
    eime::InputStateMachine sm(trie, decoder);
    std::string out;

    // 繁體模式下輸入「台積電」
    for (char c : std::string("w96ru 2u04\r")) sm.process_key(c, out);
    EIME_ASSERT(out == "台積電", "繁體模式下應輸出『台積電』");

    // 觸發繁簡切換 (Ctrl + Shift + F)
    sm.process_special_key(eime::SpecialKey::CharsetToggle, out);
    EIME_ASSERT(sm.charset() == eime::OutputCharset::Simplified, "狀態機應切換為 Simplified 模式");

    // 簡體模式下輸入「台積電」-> 應自動輸出「台积电」
    for (char c : std::string("w96ru 2u04\r")) sm.process_key(c, out);
    EIME_ASSERT(out == "台积电", "簡體模式下應輸出『台积电』，實際為: " + out);

    // 簡體模式下輸入「跨裝置雲端同步」-> 應自動輸出「跨装置云端同步」
    for (char c : std::string("dj845j; 54mp62j0 wj/61j4\r")) sm.process_key(c, out);
    EIME_ASSERT(out == "跨装置云端同步", "簡體模式下應輸出『跨装置云端同步』，實際為: " + out);

    // 再次觸發切換回繁體模式
    sm.process_special_key(eime::SpecialKey::CharsetToggle, out);
    EIME_ASSERT(sm.charset() == eime::OutputCharset::Traditional, "狀態機應切回 Traditional 模式");

    for (char c : std::string("w96ru 2u04\r")) sm.process_key(c, out);
    EIME_ASSERT(out == "台積電", "切回繁體後應再次輸出『台積電』");

    std::cout << "  -> [PASS] 繁簡切換與即時轉碼驗證通過" << std::endl;
    return true;
}

bool test_binary_dictionary_cache() {
    std::cout << "[TEST 8] 驗證詞庫高速二進位快取序列化與反序列化 (Binary Dict Cache)..." << std::endl;
    // Use native absolute Unicode paths, as the TSF DLL does in a Chinese project folder.
    auto unicode_dir = std::filesystem::absolute(std::filesystem::path("build") / std::filesystem::path(u"詞庫測試"));
    std::filesystem::path bin_file = unicode_dir / std::filesystem::path(u"詞庫.bin");
    std::error_code ec;
    std::filesystem::create_directories(unicode_dir, ec);
    EIME_ASSERT(!ec, "應能建立中文路徑測試目錄");
    std::filesystem::remove(bin_file, ec);

    std::vector<eime::RawDictEntry> entries = {
        {"台積電", eime::ZhuyinParser::parse_multi_syllables("ㄊㄞˊ ㄐㄧ ㄉㄧㄢˋ"), 99999},
        {"今天", eime::ZhuyinParser::parse_multi_syllables("ㄐㄧㄣ ㄊㄧㄢ"), 88888},
        {"天氣", eime::ZhuyinParser::parse_multi_syllables("ㄊㄧㄢ ㄑㄧˋ"), 77777}
    };

    bool saved = eime::DictLoader::save_to_binary(bin_file, entries);
    EIME_ASSERT(saved, "應能成功序列化二進位詞庫快取檔案");

    eime::ZhuyinTrie trie_loaded;
    bool loaded = eime::DictLoader::load_from_binary(bin_file, trie_loaded, false);
    EIME_ASSERT(loaded, "應能成功載入二進位詞庫快取");
    EIME_ASSERT(trie_loaded.word_count() == 3, "反序列化後的 Trie 詞數應為 3");

    auto cands = trie_loaded.lookup_exact(entries[0].syllables);
    EIME_ASSERT(!cands.empty() && cands[0].text == "台積電", "二進位載入之詞彙比對應精確吻合");

    auto text_file = unicode_dir / std::filesystem::path(u"核心詞庫.txt");
    auto text_cache = text_file;
    text_cache += ".bin";
    std::filesystem::remove(text_cache, ec);
    {
        std::ofstream text_out(text_file);
        text_out << "台積電\tㄊㄞˊ ㄐㄧ ㄉㄧㄢˋ\t99999\n";
        EIME_ASSERT(text_out.good(), "應能寫入中文路徑的文字詞庫");
    }
    eime::ZhuyinTrie text_trie;
    EIME_ASSERT(eime::DictLoader::load_from_file(text_file, text_trie, false), "應能載入中文絕對路徑的文字詞庫");
    EIME_ASSERT(text_trie.word_count() == 1, "文字詞庫應有一個詞彙");
    EIME_ASSERT(std::filesystem::exists(text_cache), "應能在中文路徑建立詞庫快取");
    // A fresh cache must be usable even when the text dictionary has no entries.
    { std::ofstream text_out(text_file, std::ios::trunc); }
    std::filesystem::last_write_time(text_cache, std::filesystem::last_write_time(text_file) + std::chrono::seconds(1));
    eime::ZhuyinTrie cached_trie;
    EIME_ASSERT(eime::DictLoader::load_from_file(text_file, cached_trie, false), "應能從中文路徑載入新快取");
    auto cached_cands = cached_trie.lookup_exact(entries[0].syllables);
    EIME_ASSERT(cached_trie.word_count() == 1 && !cached_cands.empty() && cached_cands[0].text == "台積電", "快取命中應保留原始詞彙");

    std::filesystem::remove(bin_file, ec);
    std::filesystem::remove(text_file, ec);
    std::filesystem::remove(text_cache, ec);
    std::filesystem::remove(unicode_dir, ec);
    std::cout << "  -> [PASS] 中文絕對路徑的文字詞庫與二進位快取讀寫驗證通過" << std::endl;
    return true;
}

bool test_mid_sentence_cursor_display(eime::ZhuyinTrie& trie, const eime::ViterbiDecoder& decoder) {
    std::cout << "[TEST 9] 驗證句中打字預覽與 UTF-16 游標精確追蹤 (Inline Mid-Sentence Preview & Cursor Offset)..." << std::endl;
    eime::InputStateMachine sm(trie, decoder);
    std::string out;

    // 輸入：「今天」 (rup  wu0 )
    for (char c : std::string("rup wu0 ")) sm.process_key(c, out);
    EIME_ASSERT(sm.composition_text() == "今天", "應組出『今天』");
    EIME_ASSERT(sm.cursor_idx() == 2, "游標應在末尾 (index 2)");
    EIME_ASSERT(sm.cursor_utf16_offset() == 2, "UTF-16 偏移應為 2");

    // 按左方向鍵 1 次，游標移到「今」與「天」中間 (index 1)
    sm.process_special_key(eime::SpecialKey::LeftArrow, out);
    EIME_ASSERT(sm.cursor_idx() == 1, "游標應移至 index 1");
    EIME_ASSERT(sm.cursor_utf16_offset() == 1, "UTF-16 偏移應為 1");

    // 打一個注音聲母 'c' (ㄏ)
    sm.process_key('c', out);
    EIME_ASSERT(sm.get_display_line() == "今ㄏ天", "句中插入聲母時顯示應為『今ㄏ天』，實際為: " + sm.get_display_line());
    EIME_ASSERT(sm.cursor_utf16_offset() == 2, "游標應在『ㄏ』後方 (offset 2)");

    // 再打一個介音 'u' (ㄧ)
    sm.process_key('u', out);
    EIME_ASSERT(sm.get_display_line() == "今ㄏㄧ天", "句中插入介音時顯示應為『今ㄏㄧ天』，實際為: " + sm.get_display_line());
    EIME_ASSERT(sm.cursor_utf16_offset() == 3, "游標應在『ㄧ』後方 (offset 3)");

    std::cout << "  -> [PASS] 句中插入音節預覽與 UTF-16 游標偏移追蹤驗證通過" << std::endl;
    return true;
}

bool test_polyphone_and_candidate_navigation(eime::ZhuyinTrie& trie, const eime::ViterbiDecoder& decoder) {
    std::cout << "[TEST 10] 驗證高頻多音字/破音字 (些、得、長、行) 與候選字方向鍵導航..." << std::endl;
    eime::InputStateMachine sm(trie, decoder);
    std::string out;

    // 1. 鍵入『vu, 』(ㄒㄧㄝ 一聲) -> 應自動首選『些』
    for (char c : std::string("vu, ")) sm.process_key(c, out);
    EIME_ASSERT(sm.composition_text() == "些", "『vu, 』注音首選應為『些』，實際為: " + sm.composition_text());

    // 2. 按 [↓] 展開選字
    sm.process_special_key(eime::SpecialKey::DownArrow, out);
    EIME_ASSERT(sm.state() == eime::EngineState::CandidateOpen, "應進入 CandidateOpen 狀態");
    EIME_ASSERT(sm.selected_in_page() == 0, "預設高亮第 1 項");

    auto cands = sm.current_page_candidates();
    EIME_ASSERT(!cands.empty() && cands[0].text == "些", "第 1 候選字應為『些』");

    // 3. 方向鍵 [→] 展開雙欄選字表並切換至右欄
    sm.process_special_key(eime::SpecialKey::RightArrow, out);
    EIME_ASSERT(sm.is_expanded(), "按 [→] 應展開為雙欄多頁選字表");
    EIME_ASSERT(sm.selected_in_page() >= 9, "按 [→] 應切換至右欄");

    // 4. 方向鍵 [←] 向左切換回左欄
    sm.process_special_key(eime::SpecialKey::LeftArrow, out);
    EIME_ASSERT(sm.selected_in_page() == 0, "按 [←] 應回到左欄第 1 項");

    // 5. 候選窗內 Enter 只確認選字，組字中的 Enter 才送出
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out.empty() && sm.state() == eime::EngineState::Composing, "選字後應保留組字，不能提前送出");
    EIME_ASSERT(sm.composition_text() == "些", "選字後組字應保留『些』");
    sm.process_special_key(eime::SpecialKey::DownArrow, out);
    EIME_ASSERT(sm.state() == eime::EngineState::CandidateOpen, "選過的字應可再次展開候選窗");
    sm.process_special_key(eime::SpecialKey::Escape, out);
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out == "些", "組字中的 Enter 應輸出『些』");

    // 6. 測試真實長句『讓我來作些許』中途回頭改選
    sm.reset();
    out.clear();
    for (char c : std::string("b;4ji3x96yji4vu, vm3")) sm.process_key(c, out);
    EIME_ASSERT(sm.composition_text() == "讓我來作些許", "長句預測應為『讓我來作些許』，實際為: " + sm.composition_text());

    // 游標左移 2 次到『些』
    sm.process_special_key(eime::SpecialKey::LeftArrow, out);
    sm.process_special_key(eime::SpecialKey::LeftArrow, out);
    EIME_ASSERT(sm.cursor_idx() == 4, "游標應在 index 4 (『些』)");

    // 展開選字
    sm.process_special_key(eime::SpecialKey::DownArrow, out);
    auto xie_cands = sm.current_page_candidates();
    bool has_xie = false;
    for (const auto& c : xie_cands) if (c.text == "些") has_xie = true;
    EIME_ASSERT(has_xie, "候選清單中必須包含『些』");

    std::cout << "  -> [PASS] 高頻字『些』與方向鍵候選導航驗證通過" << std::endl;
    return true;
}

bool test_phonotactics_typo_protection(eime::ZhuyinTrie& trie, const eime::ViterbiDecoder& decoder) {
    std::cout << "[TEST 11] 驗證微軟新注音音節語音規範與智慧重組機制 (MS-IME Syllable Permutation)..." << std::endl;
    eime::InputStateMachine sm(trie, decoder);
    std::string out;

    // 1. 打 k (ㄜ) 再按 2 (ㄉ) -> 自動調整順序為 ㄉㄜ，按 7 (輕聲) 後選字變為「的」
    sm.process_key('k', out);
    EIME_ASSERT(sm.get_display_line() == "ㄜ", "打 k 應為單韻母 ㄜ");
    sm.process_key('2', out);
    EIME_ASSERT(sm.get_display_line() == "ㄉㄜ", "打 k(ㄜ) 後按 2(ㄉ) 應自動重組為 ㄉㄜ，絕不可拆分為『婀ㄉ』，實際為: " + sm.get_display_line());
    sm.process_key('7', out);
    EIME_ASSERT(sm.composition_text() == "的", "按 7 輕聲後應選字為『的』，實際為: " + sm.composition_text());

    // 1b. 打 k (ㄜ) 再按 x (ㄌ) -> 自動調整順序為 ㄌㄜ，按 7 (輕聲) 後選字變為「了」
    sm.reset();
    sm.process_key('k', out);
    sm.process_key('x', out);
    EIME_ASSERT(sm.get_display_line() == "ㄌㄜ", "打 k(ㄜ) 後按 x(ㄌ) 應自動重組為 ㄌㄜ，絕不可拆分為『婀ㄌ』或『ㄜㄌ』，實際為: " + sm.get_display_line());
    sm.process_key('7', out);
    EIME_ASSERT(sm.composition_text() == "了", "按 7 輕聲後應選字為『了』，實際為: " + sm.composition_text());

    // 2. 打 k (ㄜ) 再按 1 (ㄅ) -> 自動調整順序為 ㄅㄜ，無字可選時保持注音符號「ㄅㄜ」
    sm.reset();
    sm.process_key('k', out);
    sm.process_key('1', out);
    EIME_ASSERT(sm.get_display_line() == "ㄅㄜ", "打 k(ㄜ) 後按 1(ㄅ) 應自動重組為 ㄅㄜ，絕不可拆分為『婀ㄅ』，實際為: " + sm.get_display_line());
    sm.process_key(' ', out); // 按空白鍵一聲
    EIME_ASSERT(sm.composition_text() == "ㄅㄜ", "『ㄅㄜ』無字可選，應保持注音符號『ㄅㄜ』不可亂選字，實際為: " + sm.composition_text());

    // 2b. 快速打字順序錯亂 (介音+韻母+聲母：u + 0 + w -> ㄧ + ㄢ + ㄊ)
    sm.reset();
    sm.process_key('u', out); // ㄧ (Medial)
    sm.process_key('0', out); // ㄢ (Rhyme)
    sm.process_key('w', out); // ㄊ (Consonant)
    EIME_ASSERT(sm.get_display_line() == "ㄊㄧㄢ", "順序錯亂輸入 u(ㄧ)+0(ㄢ)+w(ㄊ) 應自動重組為 ㄊㄧㄢ，實際為: " + sm.get_display_line());

    // 2c. 三個槽位已滿時仍可直接修正韻母，無須先退格。
    sm.process_key('p', out);
    EIME_ASSERT(sm.get_display_line() == "ㄊㄧㄣ", "滿三個槽位後應能直接替換韻母");
    sm.process_key('0', out);
    EIME_ASSERT(sm.get_display_line() == "ㄊㄧㄢ", "應能反覆修正同一韻母而不新增音節");

    // 按下聲調鍵 (空白鍵一聲) 進行重新組合並選字為「天」
    sm.process_key(' ', out);
    EIME_ASSERT(sm.composition_text() == "天", "按下聲調後應成功解碼並選出『天』，實際為: " + sm.composition_text());

    // 2d. 驗證依序輸入並按聲調切分打出「今天」：rup (今) + 空格，wu0 (天) + 空格
    sm.reset();
    for (char c : std::string("rup wu0 ")) sm.process_key(c, out);
    EIME_ASSERT(sm.composition_text() == "今天", "按聲調切分輸入 rup + 空格 + wu0 + 空格 應組出『今天』，實際為: " + sm.composition_text());

    // 2e. 未按聲調時，同類新按鍵修正目前音節，不提早切分為下一字。
    sm.reset();
    sm.process_key('r', out); // ㄐ
    sm.process_key('u', out); // ㄧ
    sm.process_key('p', out); // ㄣ
    EIME_ASSERT(sm.get_display_line() == "ㄐㄧㄣ", "rup 應為 ㄐㄧㄣ");
    sm.process_key('w', out);
    EIME_ASSERT(sm.get_display_line() == "ㄊㄧㄣ", "新的聲母應替換目前聲母，保留介音與韻母");
    sm.process_key('r', out);
    sm.process_key(' ', out); // 按空格一聲完成「今」
    EIME_ASSERT(sm.composition_text() == "今", "按空格後今字應上屏至組字區");

    // 3. 已有聲母後再按聲母 (例如 1 ㄅ 後按 2 ㄉ) -> 直接替換
    sm.reset();
    sm.process_key('1', out);
    EIME_ASSERT(sm.get_display_line() == "ㄅ", "打 1 應為 ㄅ");
    sm.process_key('2', out);
    EIME_ASSERT(sm.get_display_line() == "ㄉ", "新的聲母應直接替換舊聲母");

    // 4. 已有介音後再按介音 (例如 u ㄧ 後按 j ㄨ) -> 直接替換
    sm.reset();
    sm.process_key('u', out);
    EIME_ASSERT(sm.get_display_line() == "ㄧ", "打 u 應為 ㄧ");
    sm.process_key('j', out);
    EIME_ASSERT(sm.get_display_line() == "ㄨ", "新的介音應直接替換舊介音");

    // 5. 重複修正韻母後，退格一次應清掉目前韻母，不恢復錯字。
    sm.reset();
    sm.process_key('8', out);
    sm.process_key('9', out);
    EIME_ASSERT(sm.get_display_line() == "ㄞ", "新的韻母應直接替換舊韻母");
    sm.process_key('0', out);
    sm.process_key('0', out);
    sm.process_special_key(eime::SpecialKey::Backspace, out);
    EIME_ASSERT(sm.get_display_line().empty(), "重複按同類符號不能累積成多次退格");

    // 5b. 完整音節中修改介音，退格應先移除剛修正的介音。
    sm.reset();
    for (char c : std::string("su0jm")) sm.process_key(c, out);
    EIME_ASSERT(sm.get_display_line() == "ㄋㄩㄢ", "介音應可在滿三槽位後反覆修正");
    sm.process_special_key(eime::SpecialKey::Backspace, out);
    EIME_ASSERT(sm.get_display_line() == "ㄋㄢ", "退格應移除最後編輯的介音，保留聲母與韻母");
    sm.process_special_key(eime::SpecialKey::Backspace, out);
    EIME_ASSERT(sm.get_display_line() == "ㄋ", "第二次退格應依其餘輸入順序移除韻母");

    // 6. 空音節按下聲調鍵 (3, 4, 6, 7) -> 應被攔截
    sm.reset();
    sm.process_key('6', out);
    EIME_ASSERT(sm.get_display_line().empty() && out.empty(), "空音節按聲調鍵應被攔截且不輸出任何文字");

    std::cout << "  -> [PASS] 注音語音規範與智慧重組機制驗證通過" << std::endl;
    return true;
}

bool test_mid_sentence_punctuation(eime::ZhuyinTrie& trie, const eime::ViterbiDecoder& decoder) {
    std::cout << "[TEST 12] 驗證句中輸入標點符號保留組字區與精確游標位置 (Mid-Sentence Punctuation & Dotted Underline)..." << std::endl;
    eime::InputStateMachine sm(trie, decoder);
    std::string out;

    // 1. 打「你好」 (su3cl3)
    for (char c : std::string("su3cl3")) sm.process_key(c, out);
    EIME_ASSERT(sm.composition_text() == "你好", "應組出『你好』");
    EIME_ASSERT(sm.cursor_utf16_offset() == 2, "游標在末尾 offset 2");

    // 2. 左移游標至「你」與「好」中間
    sm.process_special_key(eime::SpecialKey::LeftArrow, out);
    EIME_ASSERT(sm.cursor_idx() == 1, "游標 index 應為 1");
    EIME_ASSERT(sm.cursor_utf16_offset() == 1, "游標 offset 應為 1");

    // 3. 輸入 Ctrl+, 逗號
    sm.process_ctrl_punctuation(',', out);
    EIME_ASSERT(out.empty(), "句中按 Ctrl+, 標點符號必須保留在組字區，絕不可提前送至宿主！");
    EIME_ASSERT(sm.composition_text() == "你，好", "組字區應呈現『你，好』");
    EIME_ASSERT(sm.cursor_idx() == 2, "游標 index 應在逗號後方 (2)");
    EIME_ASSERT(sm.cursor_utf16_offset() == 2, "游標 offset 應在逗號後方 (2)");

    // 4. 在逗號後方繼續打字「你」 (su3)
    for (char c : std::string("su3")) sm.process_key(c, out);
    EIME_ASSERT(sm.composition_text() == "你，你好", "應在句中逗號後正確插入『你』");
    EIME_ASSERT(sm.cursor_utf16_offset() == 3, "游標 offset 應在『你，你』後方 (3)");

    // 5. 按 Enter 整句送出
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out == "你，你好", "Enter 應整句送出包含逗號的完整句子");

    // 6. 驗證空閒狀態下輸入單個逗號 (Ctrl+,)：保留組字區底線，按向下鍵展開候選並選字
    sm.reset();
    sm.process_ctrl_punctuation(',', out);
    EIME_ASSERT(out.empty(), "單個逗號不應立即送出，應保留在組字區！");
    EIME_ASSERT(sm.composition_text() == "，", "組字區應呈現『，』");
    EIME_ASSERT(sm.state() == eime::EngineState::Composing, "應處於 Composing 組字狀態（顯示虛線底線）");
    sm.process_special_key(eime::SpecialKey::DownArrow, out);
    EIME_ASSERT(sm.state() == eime::EngineState::CandidateOpen, "按向下鍵應展開標點符號選字清單！");
    auto cands = sm.current_page_candidates();
    EIME_ASSERT(cands.size() >= 2 && cands[1].text == "、", "第 2 個候選標點應為頓號『、』");
    sm.process_key('2', out);
    EIME_ASSERT(out.empty() && sm.composition_text() == "、", "選標點後應保留組字與底線");
    sm.process_special_key(eime::SpecialKey::DownArrow, out);
    EIME_ASSERT(sm.state() == eime::EngineState::CandidateOpen, "標點選過後也應能重新選");
    cands = sm.current_page_candidates();
    auto comma = std::find_if(cands.begin(), cands.end(), [](const auto& c) { return c.text == "，"; });
    EIME_ASSERT(comma != cands.end(), "重選標點應包含逗號");
    sm.process_key(static_cast<char>('1' + std::distance(cands.begin(), comma)), out);
    EIME_ASSERT(out.empty() && sm.composition_text() == "，", "應能把頓號重新改成逗號");
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out == "，", "完成重選後 Enter 才送出逗號");

    std::cout << "  -> [PASS] 句中標點符號組字區保留與游標定位驗證通過" << std::endl;
    return true;
}

bool test_candidate_reselection() {
    std::cout << "[TEST 13] 驗證單字、整詞與句中選字可反覆改選..." << std::endl;
    eime::ZhuyinTrie trie;
    const auto ni = eime::ZhuyinParser::parse_multi_syllables("ㄋㄧˇ");
    const auto hao = eime::ZhuyinParser::parse_multi_syllables("ㄏㄠˇ");
    const auto nihao = eime::ZhuyinParser::parse_multi_syllables("ㄋㄧˇ ㄏㄠˇ");
    trie.insert(ni, "你", 100);
    trie.insert(ni, "妳", 90);
    trie.insert(hao, "好", 100);
    trie.insert(nihao, "你好", 200);
    trie.insert(nihao, "妳好", 190);
    trie.finalize_probabilities();
    eime::ViterbiDecoder decoder(trie);
    eime::InputStateMachine sm(trie, decoder);
    std::string out;
    auto choose = [&](const std::string& text) {
        sm.process_special_key(eime::SpecialKey::DownArrow, out);
        if (sm.state() != eime::EngineState::CandidateOpen) return false;
        auto cands = sm.current_page_candidates();
        auto found = std::find_if(cands.begin(), cands.end(), [&](const auto& c) { return c.text == text; });
        if (found == cands.end()) return false;
        return sm.process_key(static_cast<char>('1' + std::distance(cands.begin(), found)), out)
            && out.empty() && sm.state() == eime::EngineState::Composing;
    };

    for (char c : std::string("su3")) sm.process_key(c, out);
    EIME_ASSERT(choose("妳") && sm.composition_text() == "妳", "單字選擇不能結束組字");
    EIME_ASSERT(choose("你") && sm.composition_text() == "你", "單字應可重新改選");
    EIME_ASSERT(sm.cursor_idx() == 1 && sm.pinned_segments().size() == 1, "重選應取代原鎖定並保留正確游標");
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out == "你" && sm.state() == eime::EngineState::Idle, "單字應在 Enter 後才送出");

    for (char c : std::string("su3cl3")) sm.process_key(c, out);
    for (const auto& text : {"妳好", "你好", "妳好"}) {
        EIME_ASSERT(choose(text) && sm.composition_text() == text, "涵蓋整句的詞應可反覆改選");
        EIME_ASSERT(sm.cursor_idx() == 2 && sm.pinned_segments().size() == 1, "整詞重選應更新原鎖定");
    }
    // Candidate-window Enter also selects without committing.
    sm.process_special_key(eime::SpecialKey::DownArrow, out);
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out.empty() && sm.state() == eime::EngineState::Composing, "候選窗 Enter 也不能直接送出");
    const auto chosen = sm.composition_text();
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out == chosen && sm.state() == eime::EngineState::Idle, "第二次 Enter 才送出整詞");

    for (char c : std::string("su3")) sm.process_key(c, out);
    sm.process_ctrl_punctuation(',', out);
    for (char c : std::string("cl3")) sm.process_key(c, out);
    sm.process_special_key(eime::SpecialKey::Home, out);
    EIME_ASSERT(choose("妳") && sm.composition_text() == "妳，好", "句中改選應保留兩側組字");
    sm.process_special_key(eime::SpecialKey::LeftArrow, out);
    EIME_ASSERT(choose("你") && sm.composition_text() == "你，好", "移回句中字應能再次選字");
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out == "你，好", "重選後標點及後續文字應完整送出");
    std::cout << "  -> [PASS] 重複選字與明確送出行為驗證通過" << std::endl;
    return true;
}

bool test_aggressive_correction(eime::ZhuyinTrie& trie, const eime::ViterbiDecoder& decoder) {
    std::cout << "[TEST 14] 驗證激進修正：早按聲調 (ㄉˋㄠ -> ㄉㄠˋ)、左右鍵選字欄位切換與等號字元組字..." << std::endl;
    eime::InputStateMachine sm(trie, decoder);
    std::string out;

    // 1. 驗證早按聲調自動重組：2 (ㄉ) + 4 (ˋ) + l (ㄠ) -> 自動組合成「倒」或「到」
    sm.reset();
    sm.process_key('2', out); // ㄉ
    sm.process_key('4', out); // ˋ (提早按了四聲)
    sm.process_key('l', out); // ㄠ (韻母到來)
    EIME_ASSERT(sm.composition_text() == "到" || sm.composition_text() == "倒", "早按聲調輸入 ㄉ+ˋ+ㄠ 應自動重組成 ㄉㄠˋ 並選出『到』或『倒』，實際為: " + sm.composition_text());

    // 2. 驗證已完成的有效音節（如 ㄘˋ 次）決不會被後續的介音吸收變成「促」
    sm.reset();
    sm.process_key('h', out); // ㄘ
    sm.process_key('4', out); // ˋ -> 次
    EIME_ASSERT(sm.composition_text() == "次", "輸入 ㄘ+ˋ 應為『次』，實際為: " + sm.composition_text());
    sm.process_key('j', out); // ㄨ (介音，開始輸入下一個字「我」)
    EIME_ASSERT(sm.composition_text() == "次", "輸入 ㄨ 決不可吞噬前字變成『促』，組字文字應保留『次』，實際為: " + sm.composition_text());
    EIME_ASSERT(sm.get_display_line_with_cursor() == "次[ㄨ]", "組字行應顯示為 次[ㄨ]，實際為: " + sm.get_display_line_with_cursor());
    sm.process_key('i', out); // ㄛ
    sm.process_key('3', out); // ˇ -> 我
    EIME_ASSERT(sm.composition_text() == "次我", "連續輸入 ㄘˋ ㄨㄛˇ 應輸出『次我』，實際為: " + sm.composition_text());

    // 3. 驗證候選字窗展開與左右欄位切換
    sm.reset();
    for (char c : std::string("vu, ")) sm.process_key(c, out); // 些 (候選字 > 9 個)
    sm.process_special_key(eime::SpecialKey::DownArrow, out); // 展開選字窗
    EIME_ASSERT(sm.state() == eime::EngineState::CandidateOpen, "應進入 CandidateOpen 狀態");
    EIME_ASSERT(!sm.is_expanded(), "初始應為單欄");
    
    // 按右方向鍵展開雙欄多頁選字表，並切換到右欄
    sm.process_special_key(eime::SpecialKey::RightArrow, out);
    EIME_ASSERT(sm.is_expanded(), "按右鍵後應展開為雙欄多頁選字表");
    EIME_ASSERT(sm.selected_in_page() >= 9, "按右鍵後應選中右欄候選字");

    // 再按左方向鍵切換回左欄
    sm.process_special_key(eime::SpecialKey::LeftArrow, out);
    EIME_ASSERT(sm.selected_in_page() < 9, "按左鍵後應切換回左欄候選字");

    // 4. 驗證等號 (=) 與字元在組字區內
    sm.reset();
    for (char c : std::string("2l4")) sm.process_key(c, out); // 到 / 倒
    sm.process_literal("=", out);
    EIME_ASSERT(sm.composition_text() == "到=" || sm.composition_text() == "倒=", "組字中輸入 = 應納入組字底線內部，實際為: " + sm.composition_text());
    sm.process_special_key(eime::SpecialKey::Backspace, out);
    EIME_ASSERT(sm.composition_text() == "到" || sm.composition_text() == "倒", "按 Backspace 應刪除 =，實際為: " + sm.composition_text());

    // 5. 驗證右 Ctrl + 右 Shift 繁簡獨立快捷鍵：S (切換簡體) 與 T (切換繁體)
    sm.reset();
    sm.process_special_key(eime::SpecialKey::CharsetSimplified, out);
    EIME_ASSERT(sm.charset() == eime::OutputCharset::Simplified, "按右 Shift+Ctrl+S 應切換為簡體模式");
    sm.process_special_key(eime::SpecialKey::CharsetTraditional, out);
    EIME_ASSERT(sm.charset() == eime::OutputCharset::Traditional, "按右 Shift+Ctrl+T 應切換為繁體模式");

    std::cout << "  -> [PASS] 激進修正、左右選字切換與右側繁簡快捷鍵驗證通過" << std::endl;
    return true;
}

bool test_four_column_and_leader_comma(eime::ZhuyinTrie& trie, const eime::ViterbiDecoder& decoder) {
    std::cout << "[Test 15] 四欄位選字導航與標點符號/前導鍵輸入測試..." << std::endl;
    eime::InputStateMachine sm(trie, decoder);
    std::string out;

    // 1. 測試前導鍵 ` + 逗號/句號/問號
    sm.process_key('`', out);
    sm.process_key(',', out);
    EIME_ASSERT(sm.composition_text() == "，", "前導鍵 ` + , 組字區應呈現全形逗號『，』，實際為: " + sm.composition_text());
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out == "，", "按 Enter 應輸出全形逗號『，』");

    out.clear();
    sm.process_key('`', out);
    sm.process_key('.', out);
    EIME_ASSERT(sm.composition_text() == "。", "前導鍵 ` + . 組字區應呈現全形句號『。』，實際為: " + sm.composition_text());
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out == "。", "按 Enter 應輸出全形句號『。』");

    out.clear();
    sm.process_key('`', out);
    sm.process_key('/', out);
    EIME_ASSERT(sm.composition_text() == "？", "前導鍵 ` + / 組字區應呈現全形問號『？』，實際為: " + sm.composition_text());
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out == "？", "按 Enter 應輸出全形問號『？』");

    // 2. 測試 Ctrl 標點符號快捷鍵 (Ctrl+, -> ，, Ctrl+; -> ；, Ctrl+Shift+; / Ctrl+: -> ：)
    out.clear();
    sm.process_ctrl_punctuation(',', out);
    EIME_ASSERT(sm.composition_text() == "，", "Ctrl+, 組字區應呈現『，』，實際為: " + sm.composition_text());
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out == "，", "按 Enter 應輸出『，』");

    out.clear();
    sm.process_ctrl_punctuation(';', out);
    EIME_ASSERT(sm.composition_text() == "；", "Ctrl+; 組字區應呈現『；』，實際為: " + sm.composition_text());
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out == "；", "按 Enter 應輸出『；』");

    out.clear();
    sm.process_ctrl_punctuation(':', out);
    EIME_ASSERT(sm.composition_text() == "：", "Ctrl+Shift+; (Ctrl+:) 組字區應呈現『：』，實際為: " + sm.composition_text());
    sm.process_special_key(eime::SpecialKey::Enter, out);
    EIME_ASSERT(out == "：", "按 Enter 應輸出『：』");

    // 3. 測試 4 欄位展開 (candidates_per_page == 36)
    sm.reset();
    out.clear();
    for (char c : std::string("u ")) sm.process_key(c, out);
    sm.process_special_key(eime::SpecialKey::DownArrow, out);
    EIME_ASSERT(sm.state() == eime::EngineState::CandidateOpen, "應進入選字狀態");
    EIME_ASSERT(!sm.is_expanded(), "未按右方向鍵前應為單欄 (9 個)");
    EIME_ASSERT(sm.candidates_per_page() == 9, "未展開時每頁 9 個");

    // 按向右方向鍵展開為 4 欄位 (每頁 36 個)
    sm.process_special_key(eime::SpecialKey::RightArrow, out);
    EIME_ASSERT(sm.is_expanded(), "按右方向鍵應展開選字表");
    EIME_ASSERT(sm.candidates_per_page() == 36, "展開後每頁應為 36 個 (4 欄位 x 9 列)");
    EIME_ASSERT(sm.selected_in_page() >= 9, "按右鍵後游標應切換至第 2 欄 (index >= 9)");

    // 欄位內垂直上下移動 (在第 2 欄 9..17 內循環)
    size_t cur = sm.selected_in_page();
    sm.process_special_key(eime::SpecialKey::DownArrow, out);
    EIME_ASSERT(sm.selected_in_page() >= 9 && sm.selected_in_page() <= 17, "第 2 欄向下移動應維持在第 2 欄");
    sm.process_special_key(eime::SpecialKey::UpArrow, out);
    EIME_ASSERT(sm.selected_in_page() == cur, "第 2 欄向上移動應回到原位置");

    // 向左方向鍵切換回第 1 欄
    sm.process_special_key(eime::SpecialKey::LeftArrow, out);
    EIME_ASSERT(sm.selected_in_page() < 9, "按左鍵應切換回第 1 欄 (index < 9)");

    // 再次按左方向鍵應收回展開模式
    sm.process_special_key(eime::SpecialKey::LeftArrow, out);
    EIME_ASSERT(!sm.is_expanded(), "在第 1 欄按左鍵應收合為單欄選字表");

    std::cout << "  -> [PASS] 四欄位選字導航與標點符號/前導鍵輸入測試通過" << std::endl;
    return true;
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::cout << "==========================================================" << std::endl;
    std::cout << "  E-IME (Enhanced IME) 自動化單元與整合測試套件         " << std::endl;
    std::cout << "==========================================================" << std::endl;

    eime::ZhuyinTrie trie;
    if (!eime::DictLoader::load_from_file("data/core_dict.txt", trie, false)) {
        if (!eime::DictLoader::load_from_file("../data/core_dict.txt", trie, false)) {
            std::cerr << "無法載入 data/core_dict.txt" << std::endl;
            return 1;
        }
    }
    eime::ViterbiDecoder decoder(trie);

    bool ok = true;
    ok = ok && test_zhuyin_parser();
    ok = ok && test_mid_sentence_cursor_edit(trie, decoder);
    ok = ok && test_multi_device_cloud_sync();
    ok = ok && test_shift_and_punctuation(trie, decoder);
    ok = ok && test_candidate_window_ui();
    ok = ok && test_tsf_com_dll();
    ok = ok && test_traditional_simplified_toggle(trie, decoder);
    ok = ok && test_binary_dictionary_cache();
    ok = ok && test_mid_sentence_cursor_display(trie, decoder);
    ok = ok && test_polyphone_and_candidate_navigation(trie, decoder);
    ok = ok && test_phonotactics_typo_protection(trie, decoder);
    ok = ok && test_mid_sentence_punctuation(trie, decoder);
    ok = ok && test_candidate_reselection();
    ok = ok && test_aggressive_correction(trie, decoder);
    ok = ok && test_four_column_and_leader_comma(trie, decoder);

    if (ok) {
        std::cout << "\n[ALL TESTS PASSED] E-IME 全數 15 個核心測試模組通過。" << std::endl;
        return 0;
    } else {
        std::cerr << "\n[TEST FAILED] 部分測試未通過！" << std::endl;
        return 1;
    }
}
