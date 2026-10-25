#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "zhuyin.h"
#include "trie.h"
#include "viterbi.h"
#include "hanzi_conv.h"
#include "../../sync/include/sync_journal.h"
#include <string>
#include <vector>
#include <algorithm>

namespace eime {

inline void ring_bell() {
    // 預設靜音：避免打字錯誤或按鍵衝突時持續發出系統蜂鳴聲吵到使用者
}

enum class InputMode {
    Chinese, // 中文注音模式 (中)
    English  // 英數直通模式 (英)
};

enum class EngineState {
    Idle,           // 空閒狀態，無組字
    Composing,      // 正在組字（有注音或暫存漢字未送出）
    CandidateOpen   // 候選字視窗展開中
};

enum class SpecialKey {
    LeftArrow,
    RightArrow,
    UpArrow,
    DownArrow,
    Home,
    End,
    PageUp,
    PageDown,
    ShiftToggle,
    CharsetToggle,        // 繁簡切換 (Ctrl + Shift + F)
    CharsetTraditional,   // 切換為繁體 (右 Shift + 右 Ctrl + T)
    CharsetSimplified,    // 切換為簡體 (右 Shift + 右 Ctrl + S)
    Backspace,
    DeleteKey,
    Enter,
    Escape
};

class InputStateMachine {
public:
    static constexpr size_t CANDIDATES_PER_PAGE = 9;

    InputStateMachine(ZhuyinTrie& trie, const ViterbiDecoder& decoder, SyncJournal* journal = nullptr)
        : trie_(trie),
          decoder_(decoder),
          journal_(journal),
          mode_(InputMode::Chinese),
          charset_(OutputCharset::Traditional),
          state_(EngineState::Idle),
          cursor_idx_(0),
          candidate_target_idx_(0),
          candidate_page_(0),
          selected_in_page_(0) {}

    InputMode mode() const { return mode_; }
    OutputCharset charset() const { return charset_; }
    EngineState state() const { return state_; }
    const std::vector<Syllable>& committed_syllables() const { return committed_syllables_; }
    const Syllable& current_syllable() const { return current_syllable_; }
    std::string composition_text() const {
        return (charset_ == OutputCharset::Simplified)
            ? HanziConverter::to_simplified(preview_text_)
            : preview_text_;
    }
    const std::vector<WordCandidate>& all_candidates() const { return candidates_; }
    const std::vector<PinnedSegment>& pinned_segments() const { return pins_; }
    size_t cursor_idx() const { return cursor_idx_; }
    size_t candidate_target_idx() const { return candidate_target_idx_; }
    size_t candidate_page() const { return candidate_page_; }
    size_t selected_in_page() const { return selected_in_page_; }

    // 計算游標在 UTF-16 寬字元組字字串中的偏移量
    size_t cursor_utf16_offset() const {
        size_t off = utf16_offset_for_slot(cursor_idx_);
        if (!current_syllable_.is_empty()) {
            off += current_syllable_.symbol_count();
        }
        return off;
    }

    size_t utf16_offset_for_slot(size_t slot) const {
        size_t offset = 0;
        for (size_t i = 0; i < std::min(slot, last_viterbi_.per_syllable_chars.size()); ++i) {
            auto text = last_viterbi_.per_syllable_chars[i];
            if (charset_ == OutputCharset::Simplified) text = HanziConverter::to_simplified(text);
            for (unsigned char ch : text) {
                if ((ch & 0xc0) != 0x80) offset += ch >= 0xf0 ? 2 : 1;
            }
        }
        return offset;
    }

    bool is_expanded() const { return is_expanded_; }
    size_t candidates_per_page() const { return is_expanded_ ? 36 : 9; }

    bool is_slot_pinned(size_t slot) const {
        for (const auto& p : pins_) {
            if (slot >= p.start_syllable_idx && slot < p.start_syllable_idx + p.length) return true;
        }
        return false;
    }

    // 取得當前頁面的候選字清單 (未展開 9 個，展開模式 18 個雙欄，若為簡體模式自動轉換輸出顯示)
    std::vector<WordCandidate> current_page_candidates() const {
        std::vector<WordCandidate> page;
        size_t per_page = candidates_per_page();
        size_t start = candidate_page_ * per_page;
        for (size_t i = start; i < std::min(candidates_.size(), start + per_page); ++i) {
            auto cand = candidates_[i];
            if (charset_ == OutputCharset::Simplified) {
                cand.text = HanziConverter::to_simplified(cand.text);
            }
            page.push_back(cand);
        }
        return page;
    }

    size_t total_candidate_pages() const {
        if (candidates_.empty()) return 0;
        size_t per_page = candidates_per_page();
        return (candidates_.size() + per_page - 1) / per_page;
    }

    // 切換繁體/簡體中文輸出模式 (Ctrl + Shift + F)
    void toggle_charset() {
        charset_ = (charset_ == OutputCharset::Traditional)
            ? OutputCharset::Simplified
            : OutputCharset::Traditional;
    }

    void set_charset(OutputCharset cs) {
        charset_ = cs;
    }

    // 切換中/英模式 (對應單擊 Shift)
    void toggle_mode(std::string& out_commit_text) {
        out_commit_text.clear();
        // A mode change keeps resolved text/literals and discards only pending phonetics.
        current_syllable_ = Syllable{};
        component_order_.clear();
        candidates_.clear();
        candidate_starts_.clear();
        state_ = committed_syllables_.empty() ? EngineState::Idle : EngineState::Composing;
        mode_ = (mode_ == InputMode::Chinese) ? InputMode::English : InputMode::Chinese;
    }

    void set_mode(InputMode m) {
        mode_ = m;
    }

    // 處理特殊功能鍵与方向鍵 (MS-IME 游標改字與選字導航)
    bool process_special_key(SpecialKey key, std::string& out_commit_text) {
        out_commit_text.clear();

        if (key == SpecialKey::ShiftToggle) {
            toggle_mode(out_commit_text);
            return true;
        }

        if (key == SpecialKey::CharsetToggle) {
            toggle_charset();
            return true;
        }

        if (key == SpecialKey::CharsetTraditional) {
            set_charset(OutputCharset::Traditional);
            update_viterbi();
            return true;
        }

        if (key == SpecialKey::CharsetSimplified) {
            set_charset(OutputCharset::Simplified);
            update_viterbi();
            return true;
        }

        if (state_ == EngineState::Idle) {
            return false;
        }

        // 若正在輸入半個注音音節，先將其閉合為一聲以便移動游標或展開選字
        auto flush_pending_syllable = [&]() {
            if (!current_syllable_.is_empty()) {
                if (current_syllable_.tone == Tone::None) {
                    current_syllable_.tone = Tone::Tone1;
                }
                insert_completed_syllable(current_syllable_);
                current_syllable_ = Syllable{};
                component_order_.clear();
                update_viterbi();
            }
        };

        if (state_ == EngineState::CandidateOpen) {
            auto page_cands = current_page_candidates();
            switch (key) {
                case SpecialKey::DownArrow: {
                    if (is_expanded_) {
                        size_t col = selected_in_page_ / 9;
                        size_t col_start = col * 9;
                        size_t col_end = std::min(col_start + 9, page_cands.size());
                        if (selected_in_page_ + 1 < col_end) {
                            selected_in_page_++;
                        } else {
                            selected_in_page_ = col_start;
                        }
                    } else {
                        if (selected_in_page_ + 1 < page_cands.size()) {
                            selected_in_page_++;
                        } else {
                            size_t total_pages = total_candidate_pages();
                            if (total_pages > 1) {
                                candidate_page_ = (candidate_page_ + 1) % total_pages;
                            }
                            selected_in_page_ = 0;
                        }
                    }
                    return true;
                }
                case SpecialKey::UpArrow: {
                    if (is_expanded_) {
                        size_t col = selected_in_page_ / 9;
                        size_t col_start = col * 9;
                        size_t col_end = std::min(col_start + 9, page_cands.size());
                        if (selected_in_page_ > col_start) {
                            selected_in_page_--;
                        } else {
                            selected_in_page_ = (col_end > col_start) ? (col_end - 1) : col_start;
                        }
                    } else {
                        if (selected_in_page_ > 0) {
                            selected_in_page_--;
                        } else {
                            size_t total_pages = total_candidate_pages();
                            if (total_pages > 1) {
                                candidate_page_ = (candidate_page_ + total_pages - 1) % total_pages;
                            }
                            auto prev_cands = current_page_candidates();
                            selected_in_page_ = prev_cands.empty() ? 0 : prev_cands.size() - 1;
                        }
                    }
                    return true;
                }
                case SpecialKey::RightArrow: {
                    if (!is_expanded_) {
                        if (candidates_.size() > 9) {
                            is_expanded_ = true;
                            auto exp_cands = current_page_candidates();
                            size_t target = (selected_in_page_ % 9) + 9;
                            if (target < exp_cands.size()) {
                                selected_in_page_ = target;
                            } else if (exp_cands.size() > 9) {
                                selected_in_page_ = exp_cands.size() - 1;
                            }
                        }
                        return true;
                    }
                    // 已展開模式：4 欄橫向切換
                    size_t cur_col = selected_in_page_ / 9;
                    size_t row = selected_in_page_ % 9;
                    if (cur_col < 3) {
                        size_t next_col = cur_col + 1;
                        size_t target = next_col * 9 + row;
                        if (target < page_cands.size()) {
                            selected_in_page_ = target;
                        } else if (next_col * 9 < page_cands.size()) {
                            selected_in_page_ = page_cands.size() - 1;
                        } else {
                            size_t total_pages = total_candidate_pages();
                            if (total_pages > 1) {
                                candidate_page_ = (candidate_page_ + 1) % total_pages;
                                selected_in_page_ = 0;
                            }
                        }
                    } else {
                        size_t total_pages = total_candidate_pages();
                        if (total_pages > 1) {
                            candidate_page_ = (candidate_page_ + 1) % total_pages;
                            auto next_cands = current_page_candidates();
                            selected_in_page_ = std::min(row, next_cands.empty() ? 0 : next_cands.size() - 1);
                        }
                    }
                    return true;
                }
                case SpecialKey::LeftArrow: {
                    if (!is_expanded_) {
                        if (candidate_page_ > 0) {
                            candidate_page_--;
                            selected_in_page_ = 0;
                        }
                        return true;
                    }
                    // 已展開模式：4 欄橫向切換
                    size_t cur_col = selected_in_page_ / 9;
                    size_t row = selected_in_page_ % 9;
                    if (cur_col > 0) {
                        size_t prev_col = cur_col - 1;
                        selected_in_page_ = prev_col * 9 + row;
                    } else {
                        if (candidate_page_ > 0) {
                            candidate_page_--;
                            auto prev_cands = current_page_candidates();
                            size_t last_col = prev_cands.empty() ? 0 : (prev_cands.size() - 1) / 9;
                            size_t target = last_col * 9 + row;
                            selected_in_page_ = std::min(target, prev_cands.empty() ? 0 : prev_cands.size() - 1);
                        } else {
                            is_expanded_ = false;
                            selected_in_page_ = row;
                        }
                    }
                    return true;
                }
                case SpecialKey::PageDown: {
                    size_t total_pages = total_candidate_pages();
                    if (total_pages > 1) {
                        candidate_page_ = (candidate_page_ + 1) % total_pages;
                        selected_in_page_ = 0;
                    }
                    return true;
                }
                case SpecialKey::PageUp: {
                    size_t total_pages = total_candidate_pages();
                    if (total_pages > 1) {
                        candidate_page_ = (candidate_page_ + total_pages - 1) % total_pages;
                        selected_in_page_ = 0;
                    }
                    return true;
                }
                case SpecialKey::Escape:
                    state_ = EngineState::Composing;
                    candidates_.clear();
                    candidate_page_ = 0;
                    selected_in_page_ = 0;
                    return true;
                case SpecialKey::Enter:
                    select_candidate_in_current_page(selected_in_page_);
                    return true;
                default:
                    break;
            }
        }

        // 組字狀態下的游標控制
        switch (key) {
            case SpecialKey::LeftArrow:
                flush_pending_syllable();
                if (cursor_idx_ > 0) cursor_idx_--;
                return true;
            case SpecialKey::RightArrow:
                flush_pending_syllable();
                if (cursor_idx_ < committed_syllables_.size()) cursor_idx_++;
                return true;
            case SpecialKey::Home:
                flush_pending_syllable();
                cursor_idx_ = 0;
                return true;
            case SpecialKey::End:
                flush_pending_syllable();
                cursor_idx_ = committed_syllables_.size();
                return true;
            case SpecialKey::DownArrow:
                flush_pending_syllable();
                open_candidates_at_cursor();
                return true;
            case SpecialKey::Backspace:
                handle_backspace();
                return true;
            case SpecialKey::DeleteKey:
                handle_delete();
                return true;
            case SpecialKey::Enter:
                commit_current_composition(out_commit_text);
                return true;
            case SpecialKey::Escape:
                reset();
                return true;
            default:
                break;
        }
        return false;
    }

    // 處理 Ctrl + 標點符號 (例如 Ctrl+, 輸出全形「，」)
    bool process_ctrl_punctuation(char c, std::string& out_commit_text) {
        out_commit_text.clear();
        if (mode_ != InputMode::Chinese) return false;

        auto punct = ZhuyinParser::map_chinese_punctuation(c);
        if (!punct.has_value()) return false;

        return process_literal(punct.value(), out_commit_text);
    }

    bool process_literal(const std::string& text, std::string& out_commit_text) {
        out_commit_text.clear();
        if (text.empty()) return false;
        if (!current_syllable_.is_empty()) {
            if (current_syllable_.tone == Tone::None) current_syllable_.tone = Tone::Tone1;
            insert_completed_syllable(current_syllable_);
            current_syllable_ = Syllable{};
            component_order_.clear();
        }
        candidates_.clear();
        candidate_starts_.clear();
        for (const auto& ch : ViterbiDecoder::split_utf8_chars(text)) {
            insert_completed_syllable(Syllable{});
            literal_texts_[cursor_idx_ - 1] = ch;
        }
        update_viterbi();
        state_ = EngineState::Composing;
        return true;
    }

    // 處理一般字元按鍵輸入
    bool process_key(char c, std::string& out_commit_text) {
        out_commit_text.clear();

        // 英文模式下直接放行所有按鍵給宿主程式
        if (mode_ == InputMode::English) {
            if (state_ != EngineState::Idle) {
                if (c == '\r' || c == '\n') return process_special_key(SpecialKey::Enter, out_commit_text);
                if (c == 27) return process_special_key(SpecialKey::Escape, out_commit_text);
                if (c == '\b' || c == 127) return process_special_key(SpecialKey::Backspace, out_commit_text);
            }
            return state_ != EngineState::Idle && c >= ' ' ? process_literal(std::string(1, c), out_commit_text) : false;
        }

        // 1. 選字清單展開狀態
        if (state_ == EngineState::CandidateOpen) {
            if (c >= '1' && c <= '9') {
                size_t base = is_expanded_ ? (selected_in_page_ / 9) * 9 : 0;
                size_t idx = base + static_cast<size_t>(c - '1');
                if (select_candidate_in_current_page(idx)) {
                    return true;
                }
            } else if (c == 27) { // ESC
                return process_special_key(SpecialKey::Escape, out_commit_text);
            } else if (c == ' ') {
                return process_literal(" ", out_commit_text);
            } else if (c == '\r' || c == '\n') {
                return select_candidate_in_current_page(selected_in_page_);
            }
        }

        // 2. 控制字元：Enter / Esc / Backspace
        if (c == '\r' || c == '\n') {
            return process_special_key(SpecialKey::Enter, out_commit_text);
        }
        if (c == 27) {
            return process_special_key(SpecialKey::Escape, out_commit_text);
        }
        if (c == '\b' || c == 127) {
            return process_special_key(SpecialKey::Backspace, out_commit_text);
        }

        // 3. 支援前導符號鍵 (`) 與直接引號 [`] / [\]
        if (leader_punct_) {
            leader_punct_ = false;
            if (c == '`' || c == '~') return process_literal("`", out_commit_text);
            auto punct = ZhuyinParser::map_chinese_punctuation(c);
            if (punct.has_value()) {
                return process_literal(punct.value(), out_commit_text);
            }
        } else if (c == '`') {
            leader_punct_ = true;
            return true;
        }

        if (c == '[' || c == ']') {
            auto punct = ZhuyinParser::map_chinese_punctuation(c);
            if (punct.has_value()) {
                return process_literal(punct.value(), out_commit_text);
            }
        }

        // 4. 大千注音鍵處理
        if (ZhuyinParser::is_daqian_key(c)) {
            auto km = ZhuyinParser::map_key(c);

            // 空閒狀態下按空白鍵 -> 放行正常空格
            if (c == ' ' && state_ == EngineState::Idle) {
                return false;
            }

            // 聲調鍵 (6:ˊ, 3:ˇ, 4:ˋ, 7:˙, Space:一聲)
            if (km.category == ZhuyinParser::KeyMapping::Category::Tone) {
                if (!current_syllable_.is_empty()) {
                    current_syllable_.tone = km.tone;
                    insert_completed_syllable(current_syllable_);
                    current_syllable_ = Syllable{};
                    component_order_.clear();
                    update_viterbi();
                    state_ = EngineState::Composing;
                    return true;
                } else if (c == ' ' && !committed_syllables_.empty()) {
                    return process_literal(" ", out_commit_text);
                } else if (c != ' ') {
                    // 若當前音節為空，但游標前有音節：支援重複按聲調或修正聲調！
                    if (cursor_idx_ > 0 && cursor_idx_ <= committed_syllables_.size() &&
                        !is_slot_pinned(cursor_idx_ - 1) && literal_texts_[cursor_idx_ - 1].empty()) {
                        auto& prev = committed_syllables_[cursor_idx_ - 1];
                        if (prev.tone != km.tone) {
                            prev.tone = km.tone;
                            update_viterbi();
                        }
                        state_ = EngineState::Composing;
                        return true;
                    }
                    // 空音節按下聲調鍵 (3:ˇ, 4:ˋ, 6:ˊ, 7:˙) -> 視為無效按鍵，響鈴並攔截 (符合微軟新注音規範)
                    ring_bell();
                    return true;
                }
            }

            // 計算當前音節已輸入的注音符號數 (聲母+介音+韻母)
            size_t current_symbol_count = (!current_syllable_.consonant.empty() ? 1 : 0) +
                                         (!current_syllable_.medial.empty() ? 1 : 0) +
                                         (!current_syllable_.rhyme.empty() ? 1 : 0);

            // 在選字/按聲調出來以前，一個音節最多容納 3 個注音符號，嚴格拒絕第 4 個注音以提早中斷錯誤
            if (current_symbol_count >= 3) {
                ring_bell();
                return true;
            }

            // 聲母 (21)
            if (km.category == ZhuyinParser::KeyMapping::Category::Consonant) {
                // 一個音節僅能有一個聲母；若已存在聲母，直接拒絕並響鈴，不再自動提早斷字，提早中斷錯誤避免大量退格
                if (!current_syllable_.consonant.empty()) {
                    ring_bell();
                    return true;
                }
                current_syllable_.consonant = km.symbol;
                component_order_.push_back(km.category);
                state_ = EngineState::Composing;
                return true;
            }

            // 介音 (ㄧㄨㄩ)
            if (km.category == ZhuyinParser::KeyMapping::Category::Medial) {
                // 激進修正：若音調按早了 (例如 ㄉ + ˋ + ㄧ -> ㄉㄧˋ)，吸收介音至前一音節
                // 注意：只有在前一音節不是有效音節時才允許補全，絕對不可破壞已完成的有效單字（如 ㄘˋ 次）
                if (current_syllable_.is_empty() && cursor_idx_ > 0 && cursor_idx_ <= committed_syllables_.size() &&
                    !is_slot_pinned(cursor_idx_ - 1) && literal_texts_[cursor_idx_ - 1].empty()) {
                    auto& prev = committed_syllables_[cursor_idx_ - 1];
                    if (prev.tone != Tone::None && prev.medial.empty() && prev.rhyme.empty()) {
                        Syllable test_syl = prev;
                        test_syl.medial = km.symbol;
                        if (trie_.lookup_exact({prev}).empty() && !trie_.lookup_exact({test_syl}).empty()) {
                            prev.medial = km.symbol;
                            update_viterbi();
                            state_ = EngineState::Composing;
                            return true;
                        }
                    }
                }
                // 一個音節僅能有一個介音；若已存在介音，直接拒絕並響鈴
                if (!current_syllable_.medial.empty()) {
                    ring_bell();
                    return true;
                }
                current_syllable_.medial = km.symbol;
                component_order_.push_back(km.category);
                state_ = EngineState::Composing;
                return true;
            }

            // 韻母 (13)
            if (km.category == ZhuyinParser::KeyMapping::Category::Rhyme) {
                // 激進修正：若音調按早了 (例如 ㄉ + ˋ + ㄠ -> ㄉㄠˋ)，吸收韻母至前一音節
                // 注意：只有在前一音節不是有效音節時才允許補全，絕對不可破壞已完成的有效單字（如 ㄘˋ 次）
                if (current_syllable_.is_empty() && cursor_idx_ > 0 && cursor_idx_ <= committed_syllables_.size() &&
                    !is_slot_pinned(cursor_idx_ - 1) && literal_texts_[cursor_idx_ - 1].empty()) {
                    auto& prev = committed_syllables_[cursor_idx_ - 1];
                    if (prev.tone != Tone::None && prev.rhyme.empty()) {
                        Syllable test_syl = prev;
                        test_syl.rhyme = km.symbol;
                        if (trie_.lookup_exact({prev}).empty() && !trie_.lookup_exact({test_syl}).empty()) {
                            prev.rhyme = km.symbol;
                            update_viterbi();
                            state_ = EngineState::Composing;
                            return true;
                        }
                    }
                }
                // 一個音節僅能有一個韻母；若已存在韻母，直接拒絕並響鈴
                if (!current_syllable_.rhyme.empty()) {
                    ring_bell();
                    return true;
                }
                current_syllable_.rhyme = km.symbol;
                component_order_.push_back(km.category);
                state_ = EngineState::Composing;
                return true;
            }
        }

        return false;
    }

    // 取得含游標指示器的組字顯示字串 (例如："今天|天氣" 或 "今天[ㄊㄧㄢ]")
    std::string get_display_line_with_cursor() const {
        if (committed_syllables_.empty() && current_syllable_.is_empty()) {
            return "";
        }

        std::string out;
        for (size_t i = 0; i <= last_viterbi_.per_syllable_chars.size(); ++i) {
            if (i == cursor_idx_) out += current_syllable_.is_empty() ? "|" : "[" + current_syllable_.to_string() + "]";
            if (i == last_viterbi_.per_syllable_chars.size()) break;
            std::string ch = last_viterbi_.per_syllable_chars[i];
            if (charset_ == OutputCharset::Simplified) {
                ch = HanziConverter::to_simplified(ch);
            }
            out += ch;
        }
        return out;
    }

    std::string get_display_line() const {
        if (committed_syllables_.empty()) {
            return current_syllable_.to_string();
        }
        std::string res;
        for (size_t i = 0; i < last_viterbi_.per_syllable_chars.size(); ++i) {
            if (i == cursor_idx_ && !current_syllable_.is_empty()) {
                res += current_syllable_.to_string();
            }
            std::string ch = last_viterbi_.per_syllable_chars[i];
            if (charset_ == OutputCharset::Simplified) {
                ch = HanziConverter::to_simplified(ch);
            }
            res += ch;
        }
        if (cursor_idx_ >= last_viterbi_.per_syllable_chars.size() && !current_syllable_.is_empty()) {
            res += current_syllable_.to_string();
        }
        return res;
    }

    void reset() {
        state_ = EngineState::Idle;
        committed_syllables_.clear();
        literal_texts_.clear();
        current_syllable_ = Syllable{};
        component_order_.clear();
        pins_.clear();
        preview_text_.clear();
        last_viterbi_ = ViterbiResult{};
        candidates_.clear();
        candidate_starts_.clear();
        cursor_idx_ = 0;
        candidate_target_idx_ = 0;
        candidate_page_ = 0;
        selected_in_page_ = 0;
        is_expanded_ = false;
    }

private:
    ZhuyinTrie& trie_;
    const ViterbiDecoder& decoder_;
    SyncJournal* journal_;

    InputMode mode_;
    OutputCharset charset_;
    EngineState state_;

    std::vector<Syllable> committed_syllables_;
    // Literal slots share cursor positions with syllables and are pinned during decoding.
    std::vector<std::string> literal_texts_;
    Syllable current_syllable_;
    std::vector<ZhuyinParser::KeyMapping::Category> component_order_;
    std::vector<PinnedSegment> pins_;

    ViterbiResult last_viterbi_;
    std::string preview_text_;
    std::vector<WordCandidate> candidates_;
    std::vector<size_t> candidate_starts_;

    size_t cursor_idx_;
    size_t candidate_target_idx_;
    size_t candidate_page_;
    size_t selected_in_page_;
    bool is_expanded_ = false;
    bool leader_punct_ = false;

    void insert_completed_syllable(const Syllable& s) {
        // 若在鎖定區段內部插入音節/符號，移除該鎖定避免索引錯亂
        pins_.erase(
            std::remove_if(pins_.begin(), pins_.end(), [&](const PinnedSegment& p) {
                return (p.start_syllable_idx < cursor_idx_ && p.start_syllable_idx + p.length > cursor_idx_);
            }),
            pins_.end()
        );
        // 若在句子中間插入音節，把後方鎖定區段的索引往後平移
        for (auto& p : pins_) {
            if (p.start_syllable_idx >= cursor_idx_) {
                p.start_syllable_idx++;
            }
        }
        committed_syllables_.insert(
            committed_syllables_.begin() + static_cast<std::ptrdiff_t>(cursor_idx_),
            s
        );
        literal_texts_.insert(literal_texts_.begin() + static_cast<std::ptrdiff_t>(cursor_idx_), std::string{});
        cursor_idx_++;
    }

    void update_viterbi() {
        if (committed_syllables_.empty()) {
            preview_text_.clear();
            last_viterbi_ = ViterbiResult{};
            return;
        }
        auto all_pins = pins_;
        for (size_t i = 0; i < literal_texts_.size(); ++i) {
            if (!literal_texts_[i].empty()) all_pins.push_back({i, 1, literal_texts_[i]});
        }
        last_viterbi_ = decoder_.decode(committed_syllables_, all_pins);
        preview_text_ = last_viterbi_.full_sentence;
    }

    void open_candidates_at_cursor() {
        if (committed_syllables_.empty()) return;

        // 若游標在句尾 (cursor_idx_ == size)，預設針對最後一個字/詞展開選字；
        // 若使用者已按左方向鍵將游標移至第 k 個字，則針對位置 k 展開選字！
        if (cursor_idx_ < committed_syllables_.size()) {
            candidate_target_idx_ = cursor_idx_;
        } else {
            candidate_target_idx_ = committed_syllables_.size() - 1;
        }

        if (!literal_texts_[candidate_target_idx_].empty()) {
            auto punct_cands = ZhuyinParser::get_punctuation_candidates(literal_texts_[candidate_target_idx_]);
            if (!punct_cands.empty()) {
                candidates_.clear();
                candidate_starts_.clear();
                for (const auto& pc : punct_cands) {
                    WordCandidate wc;
                    wc.text = pc;
                    wc.syllable_length = 1;
                    wc.log_prob = 0.0;
                    candidates_.push_back(wc);
                    candidate_starts_.push_back(candidate_target_idx_);
                }
                candidate_page_ = 0;
                selected_in_page_ = 0;
                state_ = EngineState::CandidateOpen;
            }
            return;
        }

        candidates_ = trie_.lookup_prefixes(committed_syllables_, candidate_target_idx_, true);
        candidate_starts_.assign(candidates_.size(), candidate_target_idx_);

        // 若游標在句尾且句長 > 1，同時把涵蓋到句尾的多字詞也納入前排候選
        if (cursor_idx_ == committed_syllables_.size() && committed_syllables_.size() > 1) {
            size_t segment_start = candidate_target_idx_;
            while (segment_start > 0 && literal_texts_[segment_start - 1].empty()) --segment_start;
            std::vector<Syllable> segment(committed_syllables_.begin() + static_cast<std::ptrdiff_t>(segment_start), committed_syllables_.end());
            auto full_cands = trie_.lookup_exact(segment);
            for (const auto& fc : full_cands) {
                bool exists = false;
                for (const auto& c : candidates_) {
                    if (c.text == fc.text) { exists = true; break; }
                }
                if (!exists) {
                    candidates_.insert(candidates_.begin(), fc);
                    candidate_starts_.insert(candidate_starts_.begin(), segment_start);
                }
            }
        }

        if (!candidates_.empty()) {
            candidate_page_ = 0;
            selected_in_page_ = 0;
            is_expanded_ = false;
            state_ = EngineState::CandidateOpen;
        }
    }

    bool select_candidate_in_current_page(size_t index_in_page) {
        size_t per_page = candidates_per_page();
        size_t global_idx = candidate_page_ * per_page + index_in_page;
        if (global_idx >= candidates_.size()) return false;

        const auto chosen = candidates_[global_idx];

        // 決定此候選詞實際起始音節位置
        size_t pin_start = candidate_starts_[global_idx];
        if (pin_start + chosen.syllable_length > committed_syllables_.size()) return false;

        // 若為標點符號/文字 literal，直接替換 literal 內容並更新
        if (!literal_texts_[pin_start].empty()) {
            literal_texts_[pin_start] = chosen.text;
            update_viterbi();
            candidates_.clear();
            candidate_starts_.clear();
            cursor_idx_ = std::min(committed_syllables_.size(), pin_start + 1);
            state_ = EngineState::Composing;
            return true;
        }

        // 1. 觸發個人詞頻學習與跨裝置增量日誌寫入
        std::vector<Syllable> sub_syllables(
            committed_syllables_.begin() + static_cast<std::ptrdiff_t>(pin_start),
            committed_syllables_.begin() + static_cast<std::ptrdiff_t>(pin_start + chosen.syllable_length)
        );
        if (journal_) {
            journal_->record_learning(trie_, sub_syllables, chosen.text, 1);
        } else {
            trie_.apply_user_delta(sub_syllables, chosen.text, 1);
        }

        // 2. 移除與新鎖定區間重疊的舊鎖定，並加入新的 PinnedSegment
        size_t pin_end = pin_start + chosen.syllable_length;
        pins_.erase(
            std::remove_if(pins_.begin(), pins_.end(), [&](const PinnedSegment& p) {
                size_t p_end = p.start_syllable_idx + p.length;
                return (pin_start < p_end && pin_end > p.start_syllable_idx);
            }),
            pins_.end()
        );
        pins_.push_back(PinnedSegment{pin_start, chosen.syllable_length, chosen.text});

        // 3. 重新計算整句 Viterbi
        update_viterbi();
        candidates_.clear();
        candidate_starts_.clear();

        // Choosing only pins the word; keep the composition editable for reselection.
        // Enter in Composing is the separate, explicit document commit.
        is_expanded_ = false;
        cursor_idx_ = std::min(committed_syllables_.size(), pin_end);
        state_ = EngineState::Composing;
        return true;
    }

    void commit_current_composition(std::string& out_commit_text) {
        if (!current_syllable_.is_empty()) {
            if (current_syllable_.tone == Tone::None) current_syllable_.tone = Tone::Tone1;
            insert_completed_syllable(current_syllable_);
            current_syllable_ = Syllable{};
            component_order_.clear();
            update_viterbi();
        }
        out_commit_text = (charset_ == OutputCharset::Simplified)
            ? HanziConverter::to_simplified(preview_text_)
            : preview_text_;
        reset();
    }

    void handle_backspace() {
        if (!current_syllable_.is_empty()) {
            if (!component_order_.empty()) {
                auto cat = component_order_.back();
                component_order_.pop_back();
                if (cat == ZhuyinParser::KeyMapping::Category::Rhyme) current_syllable_.rhyme.clear();
                else if (cat == ZhuyinParser::KeyMapping::Category::Medial) current_syllable_.medial.clear();
                else if (cat == ZhuyinParser::KeyMapping::Category::Consonant) current_syllable_.consonant.clear();
            } else {
                if (!current_syllable_.rhyme.empty()) current_syllable_.rhyme.clear();
                else if (!current_syllable_.medial.empty()) current_syllable_.medial.clear();
                else if (!current_syllable_.consonant.empty()) current_syllable_.consonant.clear();
            }
        } else if (cursor_idx_ > 0 && !committed_syllables_.empty()) {
            size_t del_idx = cursor_idx_ - 1;
            // 移除涵蓋 del_idx 的任何鎖定，並將後方的鎖定索引減 1
            pins_.erase(
                std::remove_if(pins_.begin(), pins_.end(), [&](const PinnedSegment& p) {
                    return del_idx >= p.start_syllable_idx && del_idx < p.start_syllable_idx + p.length;
                }),
                pins_.end()
            );
            for (auto& p : pins_) {
                if (p.start_syllable_idx > del_idx) p.start_syllable_idx--;
            }
            committed_syllables_.erase(committed_syllables_.begin() + static_cast<std::ptrdiff_t>(del_idx));
            literal_texts_.erase(literal_texts_.begin() + static_cast<std::ptrdiff_t>(del_idx));
            cursor_idx_--;
            update_viterbi();
        }

        if (committed_syllables_.empty() && current_syllable_.is_empty()) {
            reset();
        } else {
            state_ = EngineState::Composing;
        }
    }

    void handle_delete() {
        if (cursor_idx_ < committed_syllables_.size()) {
            size_t del_idx = cursor_idx_;
            pins_.erase(
                std::remove_if(pins_.begin(), pins_.end(), [&](const PinnedSegment& p) {
                    return del_idx >= p.start_syllable_idx && del_idx < p.start_syllable_idx + p.length;
                }),
                pins_.end()
            );
            for (auto& p : pins_) {
                if (p.start_syllable_idx > del_idx) p.start_syllable_idx--;
            }
            committed_syllables_.erase(committed_syllables_.begin() + static_cast<std::ptrdiff_t>(del_idx));
            literal_texts_.erase(literal_texts_.begin() + static_cast<std::ptrdiff_t>(del_idx));
            update_viterbi();
        }
        if (committed_syllables_.empty() && current_syllable_.is_empty()) {
            reset();
        }
    }
};

} // namespace eime
