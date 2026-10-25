#pragma once

#include "../engine/include/trie.h"
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace eime {

class CandidateWindow {
public:
    CandidateWindow();
    ~CandidateWindow();
    CandidateWindow(const CandidateWindow&) = delete;
    CandidateWindow& operator=(const CandidateWindow&) = delete;

    bool create(HINSTANCE hInstance = nullptr);
    void destroy();

    // 更新顯示內容：組字預覽行 + 當前頁面候選字 + 頁碼資訊 + 繁簡狀態
    void update_content(
        const std::string& composition_line,
        const std::vector<WordCandidate>& page_candidates,
        size_t selected_index,
        size_t current_page,
        size_t total_pages,
        bool is_simplified = false
    );

    // 移動至游標座標下方 (含螢幕邊界防溢出自動翻轉保護)
    void show_at(int screen_x, int screen_y);
    void hide();
    void show_toast(const std::wstring& message, int screen_x, int screen_y);
    bool is_visible() const;

#ifdef _WIN32
    HWND hwnd() const { return hwnd_; }
#endif

    static std::wstring utf8_to_wide(const std::string& str);

private:
#ifdef _WIN32
    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    std::wstring class_name_;
    HFONT font_comp_ = nullptr;
    HFONT font_cand_ = nullptr;
    HFONT font_num_ = nullptr;
    HFONT font_toast_ = nullptr;

    std::wstring comp_text_;
    std::vector<std::wstring> cand_texts_;
    size_t selected_idx_ = 0;
    size_t current_page_ = 0;
    size_t total_pages_ = 0;
    bool is_simplified_ = false;
    bool is_toast_ = false;
    std::wstring toast_msg_;

    int width_ = 360;
    int height_ = 110;

    void recalculate_layout();
    void on_paint(HDC hdc, const RECT& rc);
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
#endif
};

} // namespace eime
