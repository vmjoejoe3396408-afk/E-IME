#include "../ui/user_dictionary_dialog.h"
#include <iostream>
namespace {
bool successful = false;
bool visited = false;
std::shared_ptr<eime::UserPreferencesStore> store;
bool text_equals(HWND dialog, int id, const wchar_t* expected) {
    wchar_t value[512] = {}; GetDlgItemTextW(dialog, id, value, 512);
    return std::wstring(value) == expected;
}
BOOL CALLBACK exercise(HWND dialog, LPARAM) {
    wchar_t title[128] = {}; GetWindowTextW(dialog, title, 128);
    if (std::wstring(title) != L"E-IME 私人詞庫與固定順位") return TRUE;
    visited = true;
    RECT client = {}; GetClientRect(dialog, &client);
    bool layout_valid = true;
    for (int id : {100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, IDCANCEL}) {
        HWND child = GetDlgItem(dialog, id);
        RECT bounds = {}; GetWindowRect(child, &bounds);
        MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&bounds), 2);
        layout_valid = layout_valid && child && bounds.left >= 0 && bounds.top >= 0 &&
            bounds.right <= client.right && bounds.bottom <= client.bottom;
    }
    // Read-only preview contains a supplementary Unicode character. Selecting
    // UTF-16 offsets 2..4 must derive exactly 今天 and its two readings.
    SendDlgItemMessageW(dialog, 100, EM_SETSEL, 2, 4);
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(101, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(dialog, 101)));
    bool derived = text_equals(dialog, 103, L"今天") && text_equals(dialog, 104, L"ㄐㄧㄣ ㄊㄧㄢ");
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(105, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(dialog, 105)));
    bool persisted = store->reload() && store->data().words.contains({"ㄐㄧㄣ ㄊㄧㄢ", "今天"});
    SendDlgItemMessageW(dialog, 102, LB_SETCURSEL, 0, 0);
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(102, LBN_SELCHANGE), reinterpret_cast<LPARAM>(GetDlgItem(dialog, 102)));
    bool loaded = text_equals(dialog, 103, L"今天") && text_equals(dialog, 104, L"ㄐㄧㄣ ㄊㄧㄢ");
    successful = layout_valid && derived && persisted && loaded;
    std::cout << "layout=" << layout_valid << " derived=" << derived << " persisted=" << persisted << " loaded=" << loaded << '\n';
    SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    return FALSE;
}
void CALLBACK run_test(HWND, UINT, UINT_PTR timer, DWORD) {
    KillTimer(nullptr, timer); EnumThreadWindows(GetCurrentThreadId(), exercise, 0);
}
}
int main() {
    wchar_t private_root[32768] = {};
    if (!GetEnvironmentVariableW(L"EIME_PRIVATE_DATA_DIR", private_root, 32768)) return 2;
    store = std::make_shared<eime::UserPreferencesStore>(std::filesystem::path(private_root) / L"dialog-test");
    if (!SetTimer(nullptr, 0, 50, run_test)) return 3;
    eime::show_user_dictionary_dialog(GetModuleHandleW(nullptr), nullptr, store,
        {{"😀", ""}, {"今", "ㄐㄧㄣ"}, {"天", "ㄊㄧㄢ"}});
    return visited && successful ? 0 : 1;
}
