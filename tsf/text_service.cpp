#include "text_service.h"
#include "guids.h"
#include "display_attribute.h"
#include <filesystem>

namespace eime {

static bool is_keypad_text_key(WPARAM vk) {
    return vk >= VK_NUMPAD0 && vk <= VK_DIVIDE;
}

// TSF EditSession：Windows TSF 規定所有對宿主文字緩衝區 (ITfContext) 的讀寫與游標座標查詢
// 必須在 RequestEditSession 授予的 EditCookie 回呼內執行
class EimeEditSession : public ITfEditSession {
public:
    EimeEditSession(EimeTextService* service, ITfContext* context)
        : ref_count_(1),
          service_(service),
          context_(context) {
        generation_ = service_->activation_generation();
        if (service_) service_->AddRef();
        if (context_) context_->AddRef();
    }

    virtual ~EimeEditSession() {
        if (context_) context_->Release();
        if (service_) service_->Release();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
            *ppvObj = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return ++ref_count_;
    }

    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --ref_count_;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (service_ && context_ && generation_ == service_->activation_generation()) {
            service_->drain_edit_sessions(ec, context_, generation_);
        }
        return S_OK;
    }

private:
    ULONG ref_count_;
    EimeTextService* service_;
    ITfContext* context_;
    unsigned long generation_;
};

EimeTextService::EimeTextService()
    : ref_count_(1),
      thread_mgr_(nullptr),
      client_id_(TF_CLIENTID_NULL),
      composition_(nullptr),
      shift_pressed_alone_(false) {
    initialize_engine_resources();
    ++g_dllRefCount;
}

EimeTextService::~EimeTextService() {
    if (edit_context_) edit_context_->Release();
    candidate_window_.destroy();
    if (composition_) {
        composition_->Release();
        composition_ = nullptr;
    }
    if (thread_mgr_) {
        thread_mgr_->Release();
        thread_mgr_ = nullptr;
    }
    --g_dllRefCount;
}

void EimeTextService::initialize_engine_resources() {
    wchar_t dll_path_buf[MAX_PATH] = {0};
    GetModuleFileNameW(g_hInst, dll_path_buf, MAX_PATH);
    std::filesystem::path base_dir = std::filesystem::path(dll_path_buf).parent_path();

    // 支援 build/ 目錄或專案根目錄尋找 data/
    std::filesystem::path data_dir = base_dir / "data";
    std::ifstream root_file(base_dir / "data-root.txt", std::ios::binary);
    std::string root;
    if (root_file && std::getline(root_file, root) && !root.empty()) {
        if (root.back() == '\r') root.pop_back();
        data_dir = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(root.data()), root.size()));
    } else if (!std::filesystem::exists(data_dir / "core_dict.txt")) {
        data_dir = base_dir.parent_path() / "data";
    }

    if (!DictLoader::load_from_file(data_dir / "core_dict.txt", trie_, false)) {
        throw std::runtime_error("E-IME dictionary is unavailable");
    }
    if (std::filesystem::exists(data_dir / "custom_words.txt")) {
        DictLoader::load_from_file(data_dir / "custom_words.txt", trie_, false);
    }

    journal_ = std::make_unique<SyncJournal>(data_dir / "sync");
    journal_->sync_all_devices(trie_);

    decoder_ = std::make_unique<ViterbiDecoder>(trie_);
    state_machine_ = std::make_unique<InputStateMachine>(trie_, *decoder_, journal_.get());
}

STDMETHODIMP EimeTextService::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor)) {
        *ppvObj = static_cast<ITfTextInputProcessor*>(this);
    } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
        *ppvObj = static_cast<ITfKeyEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
        *ppvObj = static_cast<ITfCompositionSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider)) {
        *ppvObj = static_cast<ITfDisplayAttributeProvider*>(this);
    }

    if (*ppvObj) {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) EimeTextService::AddRef() {
    return ++ref_count_;
}

STDMETHODIMP_(ULONG) EimeTextService::Release() {
    ULONG r = --ref_count_;
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP EimeTextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    if (!ptim) return E_INVALIDARG;
    if (thread_mgr_) return E_UNEXPECTED;
    ++activation_generation_;
    thread_mgr_ = ptim;
    thread_mgr_->AddRef();
    client_id_ = tid;

    ITfKeystrokeMgr* key_mgr = nullptr;
    if (SUCCEEDED(thread_mgr_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&key_mgr)))) {
        key_mgr->AdviseKeyEventSink(client_id_, static_cast<ITfKeyEventSink*>(this), TRUE);
        key_mgr->Release();
    }

    candidate_window_.create(g_hInst);
    if (journal_) {
        journal_->sync_all_devices(trie_);
    }

    ITfCategoryMgr* cat_mgr = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, reinterpret_cast<void**>(&cat_mgr))) && cat_mgr) {
        cat_mgr->RegisterGUID(GUID_EIME_DisplayAttr_Input, &atom_input_);
        cat_mgr->RegisterGUID(GUID_EIME_DisplayAttr_Target, &atom_target_);
        cat_mgr->Release();
    }

    return S_OK;
}

STDMETHODIMP EimeTextService::Deactivate() {
    tested_key_.reset();
    ctrl_keys_ = 0;
    ctrl_state_known_ = false;
    ++activation_generation_;
    pending_edits_.clear();
    edit_requested_ = false;
    if (edit_context_) { edit_context_->Release(); edit_context_ = nullptr; }
    shift_pressed_alone_ = false;
    if (state_machine_) state_machine_->reset();
    if (composition_) {
        auto* old = composition_;
        composition_ = nullptr;
        old->Release();
    }
    candidate_window_.hide();
    candidate_window_.destroy();

    if (thread_mgr_) {
        ITfKeystrokeMgr* key_mgr = nullptr;
        if (SUCCEEDED(thread_mgr_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&key_mgr)))) {
            key_mgr->UnadviseKeyEventSink(client_id_);
            key_mgr->Release();
        }
        thread_mgr_->Release();
        thread_mgr_ = nullptr;
    }
    client_id_ = TF_CLIENTID_NULL;
    return S_OK;
}

STDMETHODIMP EimeTextService::OnSetFocus(BOOL fForeground) {
    if (!fForeground) {
        tested_key_.reset();
        ctrl_keys_ = 0;
        ctrl_state_known_ = false;
        shift_keys_ = 0;
        shift_state_known_ = false;
        candidate_window_.hide();
    } else if (journal_) {
        // 當切換視窗重新取得焦點時，順便增量同步雲端目錄的新詞頻
        journal_->sync_all_devices(trie_);
    }
    return S_OK;
}

char EimeTextService::vk_to_daqian_char(WPARAM vk) {
    if (vk >= '0' && vk <= '9') return static_cast<char>(vk);
    if (vk >= 'A' && vk <= 'Z') return static_cast<char>(vk - 'A' + 'a');
    switch (vk) {
        case VK_SPACE:      return ' ';
        case VK_OEM_MINUS:  return '-';
        case VK_OEM_1:      return ';';
        case VK_OEM_COMMA:  return ',';
        case VK_OEM_PERIOD: return '.';
        case VK_OEM_2:      return '/';
        case VK_OEM_4:      return '[';
        case VK_OEM_6:      return ']';
        case VK_OEM_5:      return '\\';
        case VK_OEM_7:      return '\'';
        default:            return '\0';
    }
}

void EimeTextService::observe_ctrl_key(WPARAM vk, LPARAM flags, bool down) {
    if (vk != VK_CONTROL && vk != VK_LCONTROL && vk != VK_RCONTROL) return;
    if (!ctrl_state_known_) {
        ctrl_keys_ = ((GetKeyState(VK_LCONTROL) & 0x8000) ? 1u : 0u)
                   | ((GetKeyState(VK_RCONTROL) & 0x8000) ? 2u : 0u);
        ctrl_state_known_ = true;
    }
    const unsigned side = (vk == VK_RCONTROL || (vk == VK_CONTROL && (flags & (1LL << 24))) || ((GetKeyState(VK_RCONTROL) & 0x8000) != 0)) ? 2u : 1u;
    if (down) ctrl_keys_ |= side;
    else ctrl_keys_ &= ~side;
}

void EimeTextService::observe_shift_key(WPARAM vk, LPARAM flags, bool down) {
    if (vk != VK_SHIFT && vk != VK_LSHIFT && vk != VK_RSHIFT) return;
    UINT sc = static_cast<UINT>((flags >> 16) & 0xff);
    const bool is_right = (vk == VK_RSHIFT || sc == 0x36 || ((GetKeyState(VK_RSHIFT) & 0x8000) != 0));
    if (!shift_state_known_) {
        shift_keys_ = ((GetKeyState(VK_LSHIFT) & 0x8000) ? 1u : 0u)
                    | ((GetKeyState(VK_RSHIFT) & 0x8000) ? 2u : 0u);
        shift_state_known_ = true;
    }
    const unsigned side = is_right ? 2u : 1u;
    if (down) shift_keys_ |= side;
    else shift_keys_ &= ~side;
}

EimeTextService::KeyInput EimeTextService::capture_key(WPARAM vk, LPARAM flags) const {
    const auto down = [](int key) { return (GetKeyState(key) & 0x8000) != 0; };
    bool ctrl_down  = (ctrl_state_known_ && ctrl_keys_ != 0) || down(VK_CONTROL) || down(VK_LCONTROL) || down(VK_RCONTROL);
    bool shift_down = (shift_state_known_ && shift_keys_ != 0) || down(VK_SHIFT) || down(VK_LSHIFT) || down(VK_RSHIFT);
    bool is_rctrl   = (ctrl_state_known_ && (ctrl_keys_ & 2u)) || down(VK_RCONTROL);
    bool is_rshift  = (shift_state_known_ && (shift_keys_ & 2u)) || down(VK_RSHIFT);
    return {vk, flags,
        ctrl_down,
        shift_down,
        down(VK_MENU) || down(VK_LMENU) || down(VK_RMENU),
        printable_key(vk, flags),
        is_rctrl,
        is_rshift};
}

bool EimeTextService::would_eat_key(const KeyInput& input) const {
    const auto wParam = input.vk;
    if (!thread_mgr_ || !state_machine_) return false;
    if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) return false;

    bool ctrl_down  = input.ctrl;
    bool shift_down = input.shift;
    bool alt_down   = input.alt;
    if (alt_down) return false;

    // Touch/remote input can arrive as resolved Unicode, including Ctrl+comma.
    // Letting such a packet pass inserts it outside the outstanding composition.
    if (wParam == VK_PACKET) {
        const auto& text = input.text;
        if (text.empty()) return false;
        if (state_machine_->state() != EngineState::Idle || edit_requested_ || draining_edits_ || !pending_edits_.empty()) return true;
        if (state_machine_->mode() != InputMode::Chinese) return false;
        // Start a new composition for punctuation; leave already-resolved ordinary
        // Unicode text (for example from accessibility input) on its normal path.
        wchar_t wide[8] = {};
        WORD types[8] = {};
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide, 8);
        return count > 0 && GetStringTypeW(CT_CTYPE1, wide, count, types) &&
               std::all_of(types, types + count, [](WORD type) { return (type & C1_PUNCT) != 0; });
    }

    // 繁簡切換快捷鍵:
    // 1. 右 Shift + 右 Ctrl + T (繁體) / S (簡體)
    if (input.rctrl && input.rshift && (wParam == 'T' || wParam == 't' || wParam == 'S' || wParam == 's')) {
        return true;
    }
    // 2. Ctrl + Shift + F (繁簡切換)
    if (ctrl_down && shift_down && (wParam == 'F' || wParam == 'f')) {
        return true;
    }

    if (ctrl_down) {
        if (state_machine_->mode() == InputMode::Chinese && shift_down && wParam == '1') return true;
        if (state_machine_->mode() == InputMode::Chinese &&
            (wParam == VK_OEM_COMMA || wParam == VK_OEM_PERIOD || wParam == VK_OEM_1 || wParam == VK_OEM_2 ||
             wParam == VK_OEM_4 || wParam == VK_OEM_5 || wParam == VK_OEM_6 || wParam == VK_OEM_7)) {
            return true;
        }
        return false;
    }

    // Printable keypad keys are literals, independent of the Zhuyin number row.
    // Include queued commits so a host-inserted digit cannot overtake them.
    if (is_keypad_text_key(wParam) && !input.text.empty()) {
        return state_machine_->state() != EngineState::Idle || edit_requested_ ||
               draining_edits_ || !pending_edits_.empty();
    }

    if (state_machine_->state() != EngineState::Idle) {
        if (wParam == VK_BACK || wParam == VK_DELETE || wParam == VK_RETURN ||
            wParam == VK_ESCAPE || wParam == VK_LEFT || wParam == VK_RIGHT ||
            wParam == VK_UP || wParam == VK_DOWN || wParam == VK_HOME ||
            wParam == VK_END || wParam == VK_PRIOR || wParam == VK_NEXT || wParam == VK_SPACE) {
            return true;
        }
        // 組字狀態下攔截可列印字元或符號（如 =、+、~ 等），絕不外洩到組字區外
        if (!input.text.empty() || wParam == VK_OEM_PLUS || wParam == VK_OEM_MINUS ||
            wParam == VK_OEM_1 || wParam == VK_OEM_2 || wParam == VK_OEM_3 ||
            wParam == VK_OEM_4 || wParam == VK_OEM_5 || wParam == VK_OEM_6 || wParam == VK_OEM_7) {
            return true;
        }
    }

    if (state_machine_->mode() == InputMode::English) {
        return (state_machine_->state() != EngineState::Idle || edit_requested_ || draining_edits_ || !pending_edits_.empty()) && !input.text.empty();
    }
    if (shift_down && !input.text.empty()) return true;

    char ch = vk_to_daqian_char(wParam);
    if (ch != '\0' && ch != ' ') {
        return true;
    }
    return false;
}

std::string EimeTextService::printable_key(WPARAM vk, LPARAM lParam) {
    // Do not turn controls such as Return into literal text inside a composition.
    if (vk < VK_SPACE || (vk > VK_DIVIDE && vk < VK_OEM_1) || vk == VK_DELETE) return {};
    BYTE keys[256] = {};
    if (!GetKeyboardState(keys)) return {};
    const auto layout = GetKeyboardLayout(0);
    wchar_t text[8] = {};
    int count = 0;
    if (vk == VK_PACKET) {
        // A packet's character is not in the TSF lParam scan code. Windows exposes
        // it through ToUnicode(VK_PACKET, 0, ...); bit 2 makes repeated tests safe.
        count = ToUnicode(VK_PACKET, 0, keys, text, 8, 4);
    } else {
        UINT scan = static_cast<UINT>((lParam >> 16) & 0xff);
        if (!scan) scan = MapVirtualKeyExW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC, layout);
        count = ToUnicodeEx(static_cast<UINT>(vk), scan, keys, text, 8, 4, layout);
    }
    if (count <= 0 || text[0] < L' ') return {};
    // A lone surrogate packet must remain with Windows, which can combine the pair.
    // Converting either half independently would turn it into a replacement character.
    if (count == 1 && text[0] >= 0xd800 && text[0] <= 0xdfff) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, count, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, count, result.data(), bytes, nullptr, nullptr);
    return result;
}

void EimeTextService::prepare_context(ITfContext* context) {
    if (edit_context_ == context) return;
    ++activation_generation_;
    pending_edits_.clear();
    edit_requested_ = false;
    if (edit_context_) {
        edit_context_->Release();
        state_machine_->reset();
    }
    if (composition_) {
        auto* old = composition_;
        composition_ = nullptr;
        old->Release();
    }
    edit_context_ = context;
    context->AddRef();
    candidate_window_.hide();
}

HRESULT EimeTextService::queue_edit(ITfContext* context, const std::string& committed) {
    EditSnapshot snapshot;
    snapshot.committed = committed;
    snapshot.preview = state_machine_->get_display_line();
    snapshot.display_with_cursor = state_machine_->get_display_line_with_cursor();
    snapshot.caret_offset = state_machine_->cursor_utf16_offset();
    snapshot.target_offset = state_machine_->utf16_offset_for_slot(state_machine_->candidate_target_idx());
    snapshot.candidates_open = state_machine_->state() == EngineState::CandidateOpen;
    snapshot.candidates = state_machine_->current_page_candidates();
    snapshot.selected = state_machine_->selected_in_page();
    snapshot.page = state_machine_->candidate_page();
    snapshot.pages = state_machine_->total_candidate_pages();
    snapshot.simplified = state_machine_->charset() == OutputCharset::Simplified;
    pending_edits_.push_back(std::move(snapshot));
    if (edit_requested_ || draining_edits_) return S_OK;

    edit_requested_ = true;
    auto* session = new EimeEditSession(this, context);
    HRESULT result = E_FAIL;
    HRESULT hr = context->RequestEditSession(client_id_, session, TF_ES_SYNC | TF_ES_READWRITE, &result);
    if (hr == TF_E_SYNCHRONOUS || (SUCCEEDED(hr) && result == TF_E_SYNCHRONOUS)) {
        hr = context->RequestEditSession(client_id_, session, TF_ES_ASYNC | TF_ES_READWRITE, &result);
    }
    session->Release();
    if (FAILED(hr) || FAILED(result)) {
        edit_requested_ = false;
        pending_edits_.clear();
        state_machine_->reset();
        return FAILED(hr) ? hr : result;
    }
    return S_OK;
}

void EimeTextService::drain_edit_sessions(TfEditCookie ec, ITfContext* context, unsigned long generation) {
    if (generation != activation_generation_ || context != edit_context_ || draining_edits_) return;
    edit_requested_ = false;
    draining_edits_ = true;
    while (!pending_edits_.empty() && generation == activation_generation_) {
        auto snapshot = std::move(pending_edits_.front());
        pending_edits_.pop_front();
        execute_edit_session(ec, context, snapshot);
    }
    draining_edits_ = false;
}

STDMETHODIMP EimeTextService::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    (void)pic; (void)lParam;
    if (!pfEaten) return E_INVALIDARG;
    observe_ctrl_key(wParam, lParam, true);
    observe_shift_key(wParam, lParam, true);
    const auto input = capture_key(wParam, lParam);
    tested_key_.reset();

    if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
        if (!(lParam & (1LL << 30))) shift_pressed_alone_ = !input.ctrl && !input.alt;
        *pfEaten = FALSE;
        return S_OK;
    }

    shift_pressed_alone_ = false;
    *pfEaten = would_eat_key(input) ? TRUE : FALSE;
    if (*pfEaten) tested_key_ = input;
    return S_OK;
}

STDMETHODIMP EimeTextService::OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    (void)pic; (void)lParam;
    if (!pfEaten) return E_INVALIDARG;
    observe_ctrl_key(wParam, lParam, false);
    observe_shift_key(wParam, lParam, false);
    if (tested_key_ && tested_key_->vk == wParam) tested_key_.reset();
    if ((wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) && shift_pressed_alone_) {
        *pfEaten = TRUE;
        return S_OK;
    }
    *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP EimeTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten || !pic) return E_INVALIDARG;
    observe_ctrl_key(wParam, lParam, true);
    observe_shift_key(wParam, lParam, true);
    // Use the modifier/text snapshot accepted by OnTestKeyDown, even if the host
    // has advanced its keyboard state before delivering OnKeyDown.
    const auto input = tested_key_ && tested_key_->vk == wParam && tested_key_->flags == lParam
        ? *tested_key_ : capture_key(wParam, lParam);
    tested_key_.reset();
    prepare_context(pic);

    if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
        if (!(lParam & (1LL << 30))) shift_pressed_alone_ = !input.ctrl && !input.alt;
        *pfEaten = FALSE;
        return S_OK;
    }
    shift_pressed_alone_ = false;

    if (!would_eat_key(input)) {
        *pfEaten = FALSE;
        return S_OK;
    }

    std::string committed;
    bool handled = false;
    bool ctrl_down  = input.ctrl;
    bool shift_down = input.shift;

    // 繁簡切換快捷鍵:
    if (input.rctrl && input.rshift && (wParam == 'T' || wParam == 't')) {
        handled = state_machine_->process_special_key(SpecialKey::CharsetTraditional, committed);
    } else if (input.rctrl && input.rshift && (wParam == 'S' || wParam == 's')) {
        handled = state_machine_->process_special_key(SpecialKey::CharsetSimplified, committed);
    } else if (wParam == VK_PACKET) {
        auto text = input.text;
        if (state_machine_->mode() == InputMode::Chinese && ctrl_down && text.size() == 1) {
            if (auto punctuation = ZhuyinParser::map_chinese_punctuation(text[0])) text = *punctuation;
        }
        if (state_machine_->mode() == InputMode::English && state_machine_->state() == EngineState::Idle) {
            committed = text;
            handled = !text.empty();
        } else {
            handled = state_machine_->process_literal(text, committed);
        }
    } else if (ctrl_down && shift_down && (wParam == 'F' || wParam == 'f')) {
        handled = state_machine_->process_special_key(SpecialKey::CharsetToggle, committed);
    } else if (ctrl_down) {
        // Number-row 1 is a Zhuyin key without modifiers; Ctrl+Shift+1 is ！.
        char ch = shift_down && wParam == '1' ? '!' : vk_to_daqian_char(wParam);
        handled = state_machine_->process_ctrl_punctuation(ch, committed);
    } else if (auto text = input.text;
               !text.empty() && (is_keypad_text_key(wParam) || shift_down || state_machine_->mode() == InputMode::English)) {
        if ((is_keypad_text_key(wParam) || state_machine_->mode() == InputMode::English) && state_machine_->state() == EngineState::Idle) {
            // This literal follows an outstanding commit. Queue it behind that commit.
            committed = text;
            handled = true;
        } else {
            handled = state_machine_->process_literal(text, committed);
        }
    } else {
        switch (wParam) {
            case VK_LEFT:   handled = state_machine_->process_special_key(SpecialKey::LeftArrow, committed); break;
            case VK_RIGHT:  handled = state_machine_->process_special_key(SpecialKey::RightArrow, committed); break;
            case VK_UP:     handled = state_machine_->process_special_key(SpecialKey::UpArrow, committed); break;
            case VK_DOWN:   handled = state_machine_->process_special_key(SpecialKey::DownArrow, committed); break;
            case VK_HOME:   handled = state_machine_->process_special_key(SpecialKey::Home, committed); break;
            case VK_END:    handled = state_machine_->process_special_key(SpecialKey::End, committed); break;
            case VK_PRIOR:  handled = state_machine_->process_special_key(SpecialKey::PageUp, committed); break;
            case VK_NEXT:   handled = state_machine_->process_special_key(SpecialKey::PageDown, committed); break;
            case VK_BACK:   handled = state_machine_->process_special_key(SpecialKey::Backspace, committed); break;
            case VK_DELETE: handled = state_machine_->process_special_key(SpecialKey::DeleteKey, committed); break;
            case VK_RETURN: handled = state_machine_->process_special_key(SpecialKey::Enter, committed); break;
            case VK_ESCAPE: handled = state_machine_->process_special_key(SpecialKey::Escape, committed); break;
            default: {
                char ch = vk_to_daqian_char(wParam);
                if (ch != '\0') {
                    handled = state_machine_->process_key(ch, committed);
                } else if (state_machine_->state() != EngineState::Idle) {
                    std::string text = input.text;
                    if (text.empty()) {
                        if (wParam == VK_OEM_PLUS) text = shift_down ? "+" : "=";
                    }
                    if (!text.empty()) {
                        handled = state_machine_->process_literal(text, committed);
                    }
                }
                break;
            }
        }
    }

    *pfEaten = handled ? TRUE : FALSE;
    if (handled) {
        return queue_edit(pic, committed);
    }
    return S_OK;
}

STDMETHODIMP EimeTextService::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    (void)lParam;
    if (!pfEaten) return E_INVALIDARG;
    observe_ctrl_key(wParam, lParam, false);
    observe_shift_key(wParam, lParam, false);
    if (tested_key_ && tested_key_->vk == wParam) tested_key_.reset();

    if ((wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) && shift_pressed_alone_) {
        shift_pressed_alone_ = false;
        std::string committed;
        state_machine_->toggle_mode(committed);
        *pfEaten = TRUE;

        if (pic) {
            prepare_context(pic);
            return queue_edit(pic, committed);
        }
        return S_OK;
    }

    *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP EimeTextService::OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) {
    (void)pic; (void)rguid;
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP EimeTextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo **ppEnum) {
    if (!ppEnum) return E_INVALIDARG;
    *ppEnum = nullptr;

    TF_DISPLAYATTRIBUTE da_input = {};
    da_input.crText.type = TF_CT_NONE;
    da_input.crBk.type = TF_CT_NONE;
    da_input.lsStyle = TF_LS_SOLID;
    da_input.fBoldLine = FALSE;
    da_input.crLine.type = TF_CT_SYSCOLOR;
    da_input.crLine.nIndex = COLOR_WINDOWTEXT;
    da_input.bAttr = TF_ATTR_INPUT;

    TF_DISPLAYATTRIBUTE da_target = {};
    da_target.crText.type = TF_CT_NONE;
    da_target.crBk.type = TF_CT_NONE;
    da_target.lsStyle = TF_LS_SOLID;
    da_target.fBoldLine = TRUE;
    da_target.crLine.type = TF_CT_SYSCOLOR;
    da_target.crLine.nIndex = COLOR_WINDOWTEXT;
    da_target.bAttr = TF_ATTR_TARGET_CONVERTED;

    std::vector<ITfDisplayAttributeInfo*> list = {
        new EimeDisplayAttributeInfo(GUID_EIME_DisplayAttr_Input, L"E-IME 組字實線底線", da_input),
        new EimeDisplayAttributeInfo(GUID_EIME_DisplayAttr_Target, L"E-IME 選字粗實線底線", da_target)
    };

    *ppEnum = new EimeEnumDisplayAttributeInfo(list);
    for (auto* item : list) item->Release();
    return S_OK;
}

STDMETHODIMP EimeTextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo **ppInfo) {
    if (!ppInfo) return E_INVALIDARG;
    *ppInfo = nullptr;

    if (IsEqualGUID(guid, GUID_EIME_DisplayAttr_Input)) {
        TF_DISPLAYATTRIBUTE da_input = {};
        da_input.crText.type = TF_CT_NONE;
        da_input.crBk.type = TF_CT_NONE;
        da_input.lsStyle = TF_LS_SOLID;
        da_input.fBoldLine = FALSE;
        da_input.crLine.type = TF_CT_SYSCOLOR;
        da_input.crLine.nIndex = COLOR_WINDOWTEXT;
        da_input.bAttr = TF_ATTR_INPUT;
        *ppInfo = new EimeDisplayAttributeInfo(GUID_EIME_DisplayAttr_Input, L"E-IME 組字實線底線", da_input);
        return S_OK;
    }

    if (IsEqualGUID(guid, GUID_EIME_DisplayAttr_Target)) {
        TF_DISPLAYATTRIBUTE da_target = {};
        da_target.crText.type = TF_CT_NONE;
        da_target.crBk.type = TF_CT_NONE;
        da_target.lsStyle = TF_LS_SOLID;
        da_target.fBoldLine = TRUE;
        da_target.crLine.type = TF_CT_SYSCOLOR;
        da_target.crLine.nIndex = COLOR_WINDOWTEXT;
        da_target.bAttr = TF_ATTR_TARGET_CONVERTED;
        *ppInfo = new EimeDisplayAttributeInfo(GUID_EIME_DisplayAttr_Target, L"E-IME 選字粗實線底線", da_target);
        return S_OK;
    }

    return E_INVALIDARG;
}

STDMETHODIMP EimeTextService::OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) {
    (void)ecWrite;
    if (!pComposition || pComposition != composition_) return S_OK;
    auto* ended = composition_;
    composition_ = nullptr;
    ended->Release();
    ++activation_generation_;
    pending_edits_.clear();
    edit_requested_ = false;
    if (state_machine_) {
        state_machine_->reset();
    }
    candidate_window_.hide();
    return S_OK;
}

void EimeTextService::execute_edit_session(TfEditCookie ec, ITfContext* context, const EditSnapshot& snapshot) {
    const auto& committed_text = snapshot.committed;
    // 1. 若有已確認輸出的文字 (例如按下 Enter 或選字完成整句)，先寫入宿主並結束舊組字
    if (!committed_text.empty()) {
        std::wstring wcommit = CandidateWindow::utf8_to_wide(committed_text);
        if (composition_) {
            ITfRange* comp_range = nullptr;
            if (SUCCEEDED(composition_->GetRange(&comp_range)) && comp_range) {
                comp_range->SetText(ec, 0, wcommit.c_str(), static_cast<LONG>(wcommit.size()));
                comp_range->Collapse(ec, TF_ANCHOR_END);
                TF_SELECTION sel = {};
                sel.range = comp_range;
                sel.style.ase = TF_AE_NONE;
                sel.style.fInterimChar = FALSE;
                context->SetSelection(ec, 1, &sel);
                comp_range->Release();
            }
            end_composition(ec);
        } else {
            TF_SELECTION sel = {};
            ULONG fetched = 0;
            if (SUCCEEDED(context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0 && sel.range) {
                sel.range->SetText(ec, 0, wcommit.c_str(), static_cast<LONG>(wcommit.size()));
                sel.range->Collapse(ec, TF_ANCHOR_END);
                context->SetSelection(ec, 1, &sel);
                sel.range->Release();
            }
        }
    }

    // 2. 處理當前組字緩衝區 (Inline Composition)
    const auto& inline_preview = snapshot.preview;
    if (inline_preview.empty()) {
        if (composition_) {
            ITfRange* comp_range = nullptr;
            if (SUCCEEDED(composition_->GetRange(&comp_range)) && comp_range) {
                comp_range->SetText(ec, 0, L"", 0);
                comp_range->Release();
            }
            end_composition(ec);
        }
        candidate_window_.hide();
        return;
    }

    // 若尚未建立 TSF Composition，啟動之
    if (!composition_) {
        ITfContextComposition* ctx_comp = nullptr;
        if (SUCCEEDED(context->QueryInterface(IID_ITfContextComposition, reinterpret_cast<void**>(&ctx_comp))) && ctx_comp) {
            TF_SELECTION sel = {};
            ULONG fetched = 0;
            if (SUCCEEDED(context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0 && sel.range) {
                ctx_comp->StartComposition(ec, sel.range, static_cast<ITfCompositionSink*>(this), &composition_);
                sel.range->Release();
            }
            ctx_comp->Release();
        }
    }

    POINT caret_pt = get_fallback_caret_position();
    if (composition_) {
        ITfRange* comp_range = nullptr;
        if (SUCCEEDED(composition_->GetRange(&comp_range)) && comp_range) {
            std::wstring winline = CandidateWindow::utf8_to_wide(inline_preview);
            comp_range->SetText(ec, 0, winline.c_str(), static_cast<LONG>(winline.size()));

            // 1. 設置 TSF 虛線顯示屬性 (GUID_PROP_ATTRIBUTE)
            ITfProperty* prop = nullptr;
            if (atom_input_ != 0 && SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &prop)) && prop) {
                VARIANT var;
                VariantInit(&var);
                var.vt = VT_I4;
                var.lVal = static_cast<LONG>(atom_input_);
                prop->SetValue(ec, comp_range, &var);

                // 若處於選字狀態，為目標字/詞加上粗實線底線
                if (atom_target_ != 0 && snapshot.candidates_open) {
                    ITfRange* target_range = nullptr;
                    if (SUCCEEDED(comp_range->Clone(&target_range)) && target_range) {
                        target_range->Collapse(ec, TF_ANCHOR_START);
                        LONG shifted1 = 0, shifted2 = 0;
                        target_range->ShiftEnd(ec, static_cast<LONG>(snapshot.target_offset), &shifted1, nullptr);
                        target_range->Collapse(ec, TF_ANCHOR_END);
                        target_range->ShiftEnd(ec, 1, &shifted2, nullptr);
                        var.lVal = static_cast<LONG>(atom_target_);
                        prop->SetValue(ec, target_range, &var);
                        target_range->Release();
                    }
                }
                prop->Release();
            }

            // 2. 移動宿主應用程式游標至當前組字/選字焦點位置並追蹤座標
            ITfRange* sel_range = nullptr;
            if (SUCCEEDED(comp_range->Clone(&sel_range)) && sel_range) {
                sel_range->Collapse(ec, TF_ANCHOR_START);
                LONG shifted = 0;
                size_t target_offset = snapshot.candidates_open ? snapshot.target_offset : snapshot.caret_offset;
                sel_range->ShiftEnd(ec, static_cast<LONG>(target_offset), &shifted, nullptr);
                sel_range->Collapse(ec, TF_ANCHOR_END);

                TF_SELECTION sel = {};
                sel.range = sel_range;
                sel.style.ase = TF_AE_NONE;
                sel.style.fInterimChar = FALSE;
                context->SetSelection(ec, 1, &sel);

                // 嘗試透過 ITfContextView::GetTextExt 取得精確游標矩形 (Tier-1 Caret Tracking)
                ITfContextView* view = nullptr;
                if (SUCCEEDED(context->GetActiveView(&view)) && view) {
                    RECT rc_ext = {0, 0, 0, 0};
                    BOOL clipped = FALSE;
                    if (SUCCEEDED(view->GetTextExt(ec, sel_range, &rc_ext, &clipped))) {
                        if (rc_ext.left != 0 || rc_ext.bottom != 0) {
                            caret_pt.x = rc_ext.left;
                            caret_pt.y = rc_ext.bottom + 4;
                        }
                    }
                    view->Release();
                }
                sel_range->Release();
            }

            comp_range->Release();
        }
    }

    update_ui(caret_pt, snapshot);
}

void EimeTextService::end_composition(TfEditCookie ec) {
    // EndComposition can reenter the sink. Detach before calling the host.
    auto* ending = composition_;
    composition_ = nullptr;
    if (ending) {
        ending->EndComposition(ec);
        ending->Release();
    }
}

POINT EimeTextService::get_fallback_caret_position() const {
    // Tier-2 Fallback: Win32 GetGUIThreadInfo
    GUITHREADINFO gti = {};
    gti.cbSize = sizeof(GUITHREADINFO);
    HWND fg_wnd = GetForegroundWindow();
    DWORD fg_tid = fg_wnd ? GetWindowThreadProcessId(fg_wnd, nullptr) : 0;
    if (GetGUIThreadInfo(fg_tid, &gti) && gti.hwndCaret) {
        POINT pt = {gti.rcCaret.left, gti.rcCaret.bottom + 4};
        ClientToScreen(gti.hwndCaret, &pt);
        if (pt.x != 0 || pt.y != 0) return pt;
    }

    // Tier-3 Fallback: 吸附於前景視窗內側左下方
    if (fg_wnd) {
        RECT wrc = {};
        if (GetWindowRect(fg_wnd, &wrc)) {
            return {wrc.left + 60, wrc.bottom - 140};
        }
    }
    return {200, 200};
}

void EimeTextService::update_ui(const POINT& caret_pt, const EditSnapshot& snapshot) {
    if (!snapshot.candidates_open) {
        candidate_window_.hide();
        return;
    }

    candidate_window_.update_content(
        snapshot.display_with_cursor,
        snapshot.candidates,
        snapshot.selected,
        snapshot.page,
        snapshot.pages,
        snapshot.simplified
    );
    candidate_window_.show_at(caret_pt.x, caret_pt.y);
}

} // namespace eime
