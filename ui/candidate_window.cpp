#include "candidate_window.h"
#include <algorithm>

namespace eime {

#ifdef _WIN32

static const wchar_t* kCandidateWndClass = L"EIME_CandidateWindow_Class";

CandidateWindow::CandidateWindow() = default;

CandidateWindow::~CandidateWindow() {
    destroy();
}

std::wstring CandidateWindow::utf8_to_wide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring wstr(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), wstr.data(), len);
    return wstr;
}

bool CandidateWindow::create(HINSTANCE hInstance) {
    if (hwnd_) return true;
    if (!hInstance) {
        hInstance = GetModuleHandleW(nullptr);
    }
    instance_ = hInstance;
    class_name_ = std::wstring(kCandidateWndClass) + L"_" + std::to_wstring(reinterpret_cast<UINT_PTR>(this));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = CandidateWindow::wnd_proc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = class_name_.c_str();
    if (!RegisterClassExW(&wc)) return false;

    // 關鍵設計：WS_EX_NOACTIVATE 確保彈出時絕對不搶走宿主程式或全螢幕遊戲的輸入焦點
    DWORD ex_style = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    hwnd_ = CreateWindowExW(
        ex_style,
        class_name_.c_str(),
        L"E-IME Candidate",
        WS_POPUP,
        100, 100, width_, height_,
        nullptr, nullptr, hInstance, this
    );

    if (!hwnd_) {
        UnregisterClassW(class_name_.c_str(), instance_);
        class_name_.clear();
        return false;
    }

    // Keep a flat, square border without a DWM shadow.
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto fn = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(hDwm, "DwmSetWindowAttribute"));
        if (fn) {
            DWORD preference = 1; // DWMWCP_DONOTROUND (強制直角)
            fn(hwnd_, 33, &preference, sizeof(preference));
            DWORD rendering = 1; // DWMNCRP_DISABLED
            fn(hwnd_, 2, &rendering, sizeof(rendering));
        }
        FreeLibrary(hDwm);
    }

    font_comp_ = CreateFontW(
        -20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei UI"
    );
    font_cand_ = CreateFontW(
        -20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei UI"
    );
    font_num_ = CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    font_toast_ = CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei UI"
    );

    return true;
}

void CandidateWindow::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (font_comp_) { DeleteObject(font_comp_); font_comp_ = nullptr; }
    if (font_cand_) { DeleteObject(font_cand_); font_cand_ = nullptr; }
    if (font_num_)  { DeleteObject(font_num_);  font_num_  = nullptr; }
    if (font_toast_) { DeleteObject(font_toast_); font_toast_ = nullptr; }
    if (!class_name_.empty()) {
        UnregisterClassW(class_name_.c_str(), instance_);
        class_name_.clear();
    }
}

void CandidateWindow::show_toast(const std::wstring& message, int screen_x, int screen_y) {
    if (!hwnd_) create();
    if (!hwnd_) return;

    is_toast_ = true;
    toast_msg_ = message;
    HDC dc = GetDC(hwnd_);
    auto previous = SelectObject(dc, font_toast_);
    SIZE size = {};
    GetTextExtentPoint32W(dc, message.c_str(), static_cast<int>(message.size()), &size);
    SelectObject(dc, previous);
    ReleaseDC(hwnd_, dc);
    width_ = size.cx + 12;
    height_ = std::max(20, static_cast<int>(size.cy) + 6);

    show_at(screen_x, screen_y - height_ - 8);
    SetTimer(hwnd_, 999, 1200, nullptr);
}

void CandidateWindow::update_content(
    const std::string& composition_line,
    const std::vector<WordCandidate>& page_candidates,
    size_t selected_index,
    size_t current_page,
    size_t total_pages,
    bool is_simplified
) {
    if (hwnd_) KillTimer(hwnd_, 999);
    is_toast_ = false;
    comp_text_ = utf8_to_wide(composition_line);
    cand_texts_.clear();
    for (const auto& c : page_candidates) {
        cand_texts_.push_back(utf8_to_wide(c.text));
    }
    selected_idx_ = selected_index;
    current_page_ = current_page;
    total_pages_ = total_pages;
    is_simplified_ = is_simplified;

    recalculate_layout();
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void CandidateWindow::recalculate_layout() {
    if (!hwnd_) return;
    HDC hdc = GetDC(hwnd_);
    HGDIOBJ old_font = SelectObject(hdc, font_cand_);

    int candidate_width = 0;
    for (const auto& ct : cand_texts_) {
        SIZE csz = {0, 0};
        GetTextExtentPoint32W(hdc, ct.c_str(), static_cast<int>(ct.size()), &csz);
        candidate_width = std::max(candidate_width, static_cast<int>(csz.cx));
    }

    SelectObject(hdc, old_font);
    ReleaseDC(hwnd_, hdc);

    const int num_cols = std::max(1, static_cast<int>((cand_texts_.size() + 8) / 9));
    const int col_width = std::max(96, candidate_width + 16);
    const int rows = std::min(9, static_cast<int>(cand_texts_.size()));
    // One shared row-number gutter; all candidate columns use the same width.
    width_ = 2 + 24 + num_cols * col_width;
    height_ = 4 + rows * 30 + (total_pages_ > 1 ? 20 : 2);
}

void CandidateWindow::show_at(int screen_x, int screen_y) {
    if (!hwnd_) create();
    if (!hwnd_) return;

    // 螢幕邊界防溢出保護 (Multi-Monitor WorkArea Check)
    POINT pt = {screen_x, screen_y};
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(MONITORINFO);
    if (GetMonitorInfoW(hMon, &mi)) {
        if (screen_x + width_ > mi.rcWork.right) {
            screen_x = mi.rcWork.right - width_ - 8;
        }
        if (screen_x < mi.rcWork.left) {
            screen_x = mi.rcWork.left + 8;
        }
        if (screen_y + height_ > mi.rcWork.bottom) {
            // 若下方空間不足，自動向上彈出於游標上方
            screen_y = screen_y - height_ - 28;
        }
        screen_y = std::max(screen_y, static_cast<int>(mi.rcWork.top) + 4);
    }

    SetWindowPos(
        hwnd_, HWND_TOPMOST,
        screen_x, screen_y, width_, height_,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void CandidateWindow::hide() {
    if (hwnd_ && IsWindowVisible(hwnd_)) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

bool CandidateWindow::is_visible() const {
    return hwnd_ && IsWindowVisible(hwnd_);
}

void CandidateWindow::on_paint(HDC hdc, const RECT& rc) {
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    // 雙緩衝繪圖防止閃爍
    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

    // 1. 微軟經典淺色風格背景 (#FFFFFF) 與細緻邊框 (#DADCE0)
    HBRUSH bg_brush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(mem_dc, &rc, bg_brush);
    DeleteObject(bg_brush);

    HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
    HGDIOBJ old_pen = SelectObject(mem_dc, border_pen);
    HGDIOBJ old_brush = SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
    Rectangle(mem_dc, 0, 0, w, h);

    SetBkMode(mem_dc, TRANSPARENT);

    if (is_toast_) {
        // Compact, non-activating confirmation above the mouse pointer.
        SelectObject(mem_dc, font_toast_);
        SetTextColor(mem_dc, RGB(80, 80, 80));
        RECT t_rc = {6, 1, w - 6, h - 1};
        DrawTextW(mem_dc, toast_msg_.c_str(), static_cast<int>(toast_msg_.size()), &t_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);
        SelectObject(mem_dc, old_pen);
        SelectObject(mem_dc, old_brush);
        DeleteObject(border_pen);
        SelectObject(mem_dc, old_bmp);
        DeleteObject(mem_bmp);
        DeleteDC(mem_dc);
        return;
    }

    const int item_h = 30;
    const int gutter = 24;
    const int num_cols = std::max(1, static_cast<int>((cand_texts_.size() + 8) / 9));
    const int col_width = (w - 2 - gutter) / num_cols;
    const size_t selected_row = selected_idx_ % 9;
    const int rows = std::min(9, static_cast<int>(cand_texts_.size()));

    // The row numbers apply to the active column, without repeating in each cell.
    SelectObject(mem_dc, font_num_);
    SetTextColor(mem_dc, RGB(32, 32, 32));
    for (int row = 0; row < rows; ++row) {
        const int y = 2 + row * item_h;
        RECT number_rc = {1, y, 1 + gutter, y + item_h};
        if (static_cast<size_t>(row) == selected_row && selected_idx_ < cand_texts_.size()) {
            HBRUSH brush = CreateSolidBrush(RGB(153, 204, 238));
            FillRect(mem_dc, &number_rc, brush);
            DeleteObject(brush);
        }
        wchar_t number[] = {static_cast<wchar_t>(L'1' + row), L'\0'};
        DrawTextW(mem_dc, number, 1, &number_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    for (size_t i = 0; i < cand_texts_.size(); ++i) {
        const int col = static_cast<int>(i / 9);
        const int row = static_cast<int>(i % 9);
        const int x = 1 + gutter + col * col_width;
        const int y = 2 + row * item_h;
        RECT cell = {x, y, x + col_width, y + item_h};
        if (i == selected_idx_) {
            HBRUSH brush = CreateSolidBrush(RGB(153, 204, 238));
            FillRect(mem_dc, &cell, brush);
            DeleteObject(brush);
        }
        SelectObject(mem_dc, font_cand_);
        SetTextColor(mem_dc, RGB(32, 32, 32));
        RECT text_rc = {x + 8, y, x + col_width - 8, y + item_h};
        DrawTextW(mem_dc, cand_texts_[i].c_str(), static_cast<int>(cand_texts_[i].size()),
            &text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    if (total_pages_ > 1) {
        RECT footer = {1, h - 21, w - 1, h - 1};
        HBRUSH brush = CreateSolidBrush(RGB(242, 242, 242));
        FillRect(mem_dc, &footer, brush);
        DeleteObject(brush);
        MoveToEx(mem_dc, 1, footer.top, nullptr);
        LineTo(mem_dc, w - 1, footer.top);
        const std::wstring page = std::to_wstring(current_page_ + 1) + L"/" + std::to_wstring(total_pages_);
        SelectObject(mem_dc, font_num_);
        SetTextColor(mem_dc, RGB(90, 90, 90));
        RECT page_rc = {6, footer.top + 1, w - 6, h - 2};
        DrawTextW(mem_dc, page.c_str(), static_cast<int>(page.size()), &page_rc,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);

    SelectObject(mem_dc, old_pen);
    SelectObject(mem_dc, old_brush);
    DeleteObject(border_pen);
    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
}

LRESULT CALLBACK CandidateWindow::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    CandidateWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<CandidateWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return DefWindowProcW(hwnd, msg, wp, lp);
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE; // 滑鼠點擊時絕不搶焦點
        case WM_TIMER:
            if (wp == 999) {
                KillTimer(hwnd, 999);
                if (self) self->hide();
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (self) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                self->on_paint(hdc, rc);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

#endif // _WIN32

} // namespace eime
