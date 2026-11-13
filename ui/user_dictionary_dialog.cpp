#include "user_dictionary_dialog.h"
#include "candidate_window.h"

namespace eime {
namespace {
enum { Preview = 100, UseSelection, Words, Word, Reading, Save, NewWord, DeleteWord, Ranks, ResetRank, Status };
std::string utf8(const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size > 0 ? size : 0, '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
class DictionaryDialog {
public:
    std::shared_ptr<UserPreferencesStore> store;
    std::vector<WordReading> composition;
    WordReading preset;
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    int dpi = 96;
    std::vector<UserWordKey> keys;
    std::vector<std::string> groups;
    UserWordKey editing;
    ~DictionaryDialog() { if (font) DeleteObject(font); }
    int px(int value) const { return MulDiv(value, dpi, 96); }
    HWND control(const wchar_t* type, const wchar_t* title, int id, int x, int y, int w, int h, DWORD style = 0) {
        HWND child = CreateWindowExW(std::wstring(type) == L"EDIT" || std::wstring(type) == L"LISTBOX" ? WS_EX_CLIENTEDGE : 0,
            type, title, WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w), px(h), hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return child;
    }
    void label(const wchar_t* title, int x, int y, int w) { control(L"STATIC", title, -1, x, y, w, 22); }
    void set(int id, const std::string& text) { SetDlgItemTextW(hwnd, id, CandidateWindow::utf8_to_wide(text).c_str()); }
    std::string get(int id) {
        HWND child = GetDlgItem(hwnd, id);
        int size = GetWindowTextLengthW(child);
        if (size > 4096) return {};
        std::wstring text(static_cast<size_t>(size) + 1, L'\0');
        GetWindowTextW(child, text.data(), size + 1); text.resize(size);
        return utf8(text);
    }
    void error(const std::wstring& message) { MessageBoxW(hwnd, message.c_str(), L"E-IME", MB_OK | MB_ICONWARNING); }
    void refresh() {
        if (!store->reload()) { error(store->error()); return; }
        keys.assign(store->data().words.begin(), store->data().words.end());
        HWND list = GetDlgItem(hwnd, Words);
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        for (const auto& [reading, text] : keys) {
            auto label = CandidateWindow::utf8_to_wide(text + "  —  " + reading);
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        groups.clear(); list = GetDlgItem(hwnd, Ranks); SendMessageW(list, LB_RESETCONTENT, 0, 0);
        for (const auto& [reading, order] : store->data().ranks) {
            groups.push_back(reading);
            std::string line = reading + "  —  ";
            bool first = true;
            for (const auto& text : order) {
                if (line.size() > 500) { line += "…"; break; }
                if (!first) line += " → ";
                line += text; first = false;
            }
            auto label = CandidateWindow::utf8_to_wide(line);
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
    }
    void select_composition() {
        DWORD begin = 0, end = 0;
        SendDlgItemMessageW(hwnd, Preview, EM_GETSEL, reinterpret_cast<WPARAM>(&begin), reinterpret_cast<LPARAM>(&end));
        std::string text, reading;
        size_t offset = 0;
        bool aligned_begin = begin == 0, aligned_end = false;
        for (const auto& slot : composition) {
            size_t next = offset + CandidateWindow::utf8_to_wide(slot.text).size();
            if (offset == begin) aligned_begin = true;
            if (next == end) aligned_end = true;
            if (offset >= begin && next <= end) {
                if (slot.reading.empty()) { error(L"選取範圍包含英文或標點，請只選已改好的注音字詞。"); return; }
                text += slot.text;
                if (!reading.empty()) reading += ' ';
                reading += slot.reading;
            }
            offset = next;
        }
        if (begin == end || !aligned_begin || !aligned_end || !valid_user_word(reading, text)) {
            error(L"請先在組字預覽反白完整中文字詞；未完成注音不能造詞。"); return;
        }
        editing = {}; set(Word, text); set(Reading, reading);
    }
    void init() {
        HDC dc = GetDC(hwnd); dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(hwnd, dc);
        font = CreateFontW(-px(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft JhengHei UI");
        SetWindowTextW(hwnd, L"E-IME 私人詞庫與固定順位");
        RECT work = {}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        SetWindowPos(hwnd, nullptr, work.left + std::max<LONG>(0, (work.right - work.left - px(660)) / 2),
            work.top + std::max<LONG>(0, (work.bottom - work.top - px(675)) / 2), px(660), px(675), SWP_NOZORDER);
        label(L"在組字預覽反白詞語，再按「帶入選取範圍」。", 14, 12, 610);
        control(L"EDIT", L"", Preview, 14, 38, 610, 30, ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP);
        std::string preview; for (const auto& slot : composition) preview += slot.text;
        set(Preview, preview);
        SendDlgItemMessageW(hwnd, Preview, EM_SETSEL, 0, -1);
        control(L"BUTTON", L"帶入選取範圍", UseSelection, 14, 74, 160, 30, WS_TABSTOP);
        EnableWindow(GetDlgItem(hwnd, UseSelection), !composition.empty());
        label(L"私人詞彙（點選後可改詞彙或讀音）", 14, 114, 610);
        control(L"LISTBOX", L"", Words, 14, 140, 610, 125, LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP);
        SendDlgItemMessageW(hwnd, Words, LB_SETHORIZONTALEXTENT, px(1500), 0);
        label(L"詞彙", 14, 278, 70);
        control(L"EDIT", L"", Word, 90, 274, 534, 30, ES_AUTOHSCROLL | WS_TABSTOP);
        label(L"注音", 14, 314, 70);
        control(L"EDIT", L"", Reading, 90, 310, 534, 30, ES_AUTOHSCROLL | WS_TABSTOP);
        SendDlgItemMessageW(hwnd, Word, EM_SETLIMITTEXT, 256, 0);
        SendDlgItemMessageW(hwnd, Reading, EM_SETLIMITTEXT, 2048, 0);
        label(L"注音每字一音節，空格分隔；第一聲不加符號。", 90, 344, 534);
        control(L"BUTTON", L"儲存／更新", Save, 14, 372, 150, 32, WS_TABSTOP);
        control(L"BUTTON", L"另建新詞", NewWord, 174, 372, 140, 32, WS_TABSTOP);
        control(L"BUTTON", L"刪除所選詞", DeleteWord, 324, 372, 140, 32, WS_TABSTOP);
        label(L"固定順位（在候選右鍵調整；此處可恢復自動排序）", 14, 416, 610);
        control(L"LISTBOX", L"", Ranks, 14, 442, 610, 80, LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP);
        SendDlgItemMessageW(hwnd, Ranks, LB_SETHORIZONTALEXTENT, px(2000), 0);
        control(L"BUTTON", L"恢復所選組的自動排序", ResetRank, 14, 532, 230, 32, WS_TABSTOP);
        control(L"STATIC", L"", Status, 14, 574, 610, 36);
        control(L"BUTTON", L"關閉", IDCANCEL, 504, 532, 120, 32, WS_TABSTOP);
        refresh(); set(Word, preset.text); set(Reading, preset.reading);
        SetDlgItemTextW(hwnd, Status, (L"私人資料：" + store->root().wstring()).c_str());
    }
    void command(int id, int notification) {
        if (id == IDCANCEL) { EndDialog(hwnd, 0); return; }
        if (id == UseSelection) { select_composition(); return; }
        if (id == Words && notification == LBN_SELCHANGE) {
            auto index = SendDlgItemMessageW(hwnd, Words, LB_GETCURSEL, 0, 0);
            if (index >= 0 && static_cast<size_t>(index) < keys.size()) { editing = keys[index]; set(Word, editing.second); set(Reading, editing.first); }
        } else if (id == NewWord) { editing = {}; set(Word, ""); set(Reading, ""); SetFocus(GetDlgItem(hwnd, Word)); }
        else if (id == Save) {
            if (!store->save_word(get(Reading), get(Word), editing)) { error(store->error()); return; }
            editing = {canonical_reading(get(Reading)), get(Word)}; refresh();
            SetDlgItemTextW(hwnd, Status, L"已儲存。回到輸入程式後，下次輸入或開候選時生效。");
        } else if (id == DeleteWord && !editing.first.empty()) {
            if (MessageBoxW(hwnd, L"刪除此私人加詞？原本詞庫已有的同音字詞仍會保留。", L"E-IME", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
            if (!store->delete_word(editing)) { error(store->error()); return; }
            editing = {}; set(Word, ""); set(Reading, ""); refresh();
        } else if (id == ResetRank) {
            auto index = SendDlgItemMessageW(hwnd, Ranks, LB_GETCURSEL, 0, 0);
            if (index >= 0 && static_cast<size_t>(index) < groups.size()) {
                if (!store->reorder(groups[index], "", RankAction::Reset, {})) { error(store->error()); return; }
                refresh();
            }
        }
    }
    static INT_PTR CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        try {
        auto self = reinterpret_cast<DictionaryDialog*>(GetWindowLongPtrW(hwnd, DWLP_USER));
        if (message == WM_INITDIALOG) {
            self = reinterpret_cast<DictionaryDialog*>(lp); self->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, DWLP_USER, lp); self->init(); return TRUE;
        }
        if (!self) return FALSE;
        if (message == WM_CLOSE) { EndDialog(hwnd, 0); return TRUE; }
        if (message == WM_COMMAND) { self->command(LOWORD(wp), HIWORD(wp)); return TRUE; }
        return FALSE;
        } catch (...) {
            MessageBoxW(hwnd, L"私人詞庫操作失敗，請關閉視窗後重試。", L"E-IME", MB_OK | MB_ICONWARNING);
            return TRUE;
        }
    }
};
}
void show_user_dictionary_dialog(HINSTANCE instance, HWND owner,
    const std::shared_ptr<UserPreferencesStore>& store,
    const std::vector<WordReading>& composition, const WordReading& preset) {
    DictionaryDialog model;
    model.store = store; model.composition = composition; model.preset = preset;
    std::vector<WORD> buffer((sizeof(DLGTEMPLATE) + 6 + sizeof(WORD) - 1) / sizeof(WORD), 0);
    auto layout = reinterpret_cast<DLGTEMPLATE*>(buffer.data());
    layout->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    layout->cx = 330; layout->cy = 330;
    if (DialogBoxIndirectParamW(instance, layout, owner, DictionaryDialog::procedure, reinterpret_cast<LPARAM>(&model)) == -1)
        MessageBoxW(owner, L"無法開啟私人詞庫視窗。", L"E-IME", MB_OK | MB_ICONWARNING);
}
}
