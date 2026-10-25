#include "candidate_window.h"
#include <algorithm>
#include <sstream>

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
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
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

    // 啟用 Windows 11 現代圓角邊框 (DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2)
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto fn = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(hDwm, "DwmSetWindowAttribute"));
        if (fn) {
            DWORD preference = 1; // DWMWCP_DONOTROUND (強制直角)
            fn(hwnd_, 33, &preference, sizeof(preference));
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
        -16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
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
    width_ = 140;
    height_ = 44;

    show_at(screen_x, screen_y);
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

    SelectObject(hdc, font_num_);
    std::wstring page_tag;
    if (total_pages_ > 1) {
        page_tag = std::to_wstring(current_page_ + 1) + L"/" + std::to_wstring(total_pages_);
    }
    SIZE tag_sz = {0, 0};
    if (!page_tag.empty()) {
        GetTextExtentPoint32W(hdc, page_tag.c_str(), static_cast<int>(page_tag.size()), &tag_sz);
    }

    SelectObject(hdc, old_font);
    ReleaseDC(hwnd_, hdc);

    if (is_toast_) {
        width_ = 140;
        height_ = 44;
        return;
    }
    const int item_h = 30;
    int num_cols = 1;
    if (cand_texts_.size() > 27) num_cols = 4;
    else if (cand_texts_.size() > 18) num_cols = 3;
    else if (cand_texts_.size() > 9) num_cols = 2;

    int footer_h = (total_pages_ > 1) ? 24 : 8;

    if (num_cols > 1) {
        int col_width = std::max(120, candidate_width + 48);
        width_ = col_width * num_cols + 24;
        int max_rows = 9;
        height_ = 12 + max_rows * item_h + footer_h;
    } else {
        int min_width = 160;
        width_ = std::max({min_width, candidate_width + 80, static_cast<int>(tag_sz.cx) + 36});
        height_ = 12 + static_cast<int>(cand_texts_.size()) * item_h + footer_h;
    }
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

    HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(218, 220, 224));
    HGDIOBJ old_pen = SelectObject(mem_dc, border_pen);
    HGDIOBJ old_brush = SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
    Rectangle(mem_dc, 0, 0, w, h);

    SetBkMode(mem_dc, TRANSPARENT);

    if (is_toast_) {
        // 直角 Toast 提示視窗 (藍色指示條 + 置中標題)
        RECT ind_rc = {0, 0, 4, h};
        HBRUSH ind_brush = CreateSolidBrush(RGB(0, 103, 192));
        FillRect(mem_dc, &ind_rc, ind_brush);
        DeleteObject(ind_brush);

        SelectObject(mem_dc, font_comp_);
        SetTextColor(mem_dc, RGB(20, 20, 20));
        RECT t_rc = {8, 0, w, h};
        DrawTextW(mem_dc, toast_msg_.c_str(), static_cast<int>(toast_msg_.size()), &t_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

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
    int num_cols = 1;
    if (cand_texts_.size() > 27) num_cols = 4;
    else if (cand_texts_.size() > 18) num_cols = 3;
    else if (cand_texts_.size() > 9) num_cols = 2;

    int col_width = (num_cols > 1) ? ((w - 24) / num_cols) : (w - 24);

    if (num_cols > 1) {
        HPEN div_pen = CreatePen(PS_SOLID, 1, RGB(230, 230, 230));
        HGDIOBJ old_p = SelectObject(mem_dc, div_pen);
        for (int c = 1; c < num_cols; ++c) {
            int div_x = 12 + c * col_width;
            MoveToEx(mem_dc, div_x, 10, nullptr);
            LineTo(mem_dc, div_x, h - 16);
        }
        SelectObject(mem_dc, old_p);
        DeleteObject(div_pen);
    }

    for (size_t i = 0; i < cand_texts_.size(); ++i) {
        int col = static_cast<int>(i / 9);
        int row = static_cast<int>(i % 9);
        int col_left = 12 + col * col_width;
        int item_w = col_width - 6;
        const int row_y = 6 + row * item_h;
        int cur_x = col_left + 8;

        if (i == selected_idx_) {
            RECT hi_rc = {col_left + 2, row_y, col_left + 2 + item_w, row_y + item_h};
            HBRUSH hi_brush = CreateSolidBrush(RGB(235, 243, 252));
            FillRect(mem_dc, &hi_rc, hi_brush);
            DeleteObject(hi_brush);

            RECT ind_rc = {col_left + 2, row_y + 4, col_left + 6, row_y + item_h - 4};
            HBRUSH ind_brush = CreateSolidBrush(RGB(0, 103, 192));
            FillRect(mem_dc, &ind_rc, ind_brush);
            DeleteObject(ind_brush);
        }

        SelectObject(mem_dc, font_num_);
        SetTextColor(mem_dc, (i == selected_idx_) ? RGB(0, 103, 192) : RGB(115, 115, 115));
        int num_idx = static_cast<int>(row);
        wchar_t num_buf[4] = {static_cast<wchar_t>(L'1' + num_idx), L'.', L' ', L'\0'};
        RECT num_rc = {cur_x, row_y, cur_x + 22, row_y + item_h};
        DrawTextW(mem_dc, num_buf, -1, &num_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(mem_dc, font_cand_);
        SetTextColor(mem_dc, (i == selected_idx_) ? RGB(0, 0, 0) : RGB(32, 32, 32));
        RECT txt_rc = {cur_x + 24, row_y, col_left + item_w - 2, row_y + item_h};
        DrawTextW(mem_dc, cand_texts_[i].c_str(), static_cast<int>(cand_texts_[i].size()), &txt_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // 只有在多於 1 頁時才顯示頁碼（例如 1/2），不顯示 [繁] 或 [簡]
    if (total_pages_ > 1) {
        std::wstringstream pss;
        pss << (current_page_ + 1) << L"/" << total_pages_;
        std::wstring page_str = pss.str();

        SelectObject(mem_dc, font_num_);
        SetTextColor(mem_dc, RGB(130, 130, 130));
        RECT page_rc = {14, h - 22, w - 14, h - 4};
        DrawTextW(mem_dc, page_str.c_str(), static_cast<int>(page_str.size()), &page_rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
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
