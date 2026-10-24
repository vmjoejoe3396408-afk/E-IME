#pragma once

#include "trie.h"
#include <vector>
#include <string>
#include <limits>
#include <algorithm>

namespace eime {

// 使用者在組字區內透過游標手動選字鎖定的區段 (MS-IME 核心行為)
struct PinnedSegment {
    size_t start_syllable_idx = 0;
    size_t length = 1;
    std::string text;
};

struct SegmentedWord {
    std::string text;
    size_t start_syllable_idx = 0;
    size_t length = 1; // 涵蓋的音節數
    double score = 0.0;
    bool is_pinned = false;
};

struct ViterbiResult {
    std::string full_sentence;
    std::vector<SegmentedWord> words;
    // 每個音節位置對應到的輸出字詞片段 (方便組字區游標精準定位顯示)
    std::vector<std::string> per_syllable_chars;
    double total_score = -1e9;
};

class ViterbiDecoder {
public:
    explicit ViterbiDecoder(const ZhuyinTrie& trie) : trie_(trie) {}

    // 支援無鎖定或含局部鎖定 (Pinned Overrides) 的全局最佳語句解碼
    ViterbiResult decode(
        const std::vector<Syllable>& syllables,
        const std::vector<PinnedSegment>& pins = {}
    ) const {
        if (syllables.empty()) return {};

        const size_t N = syllables.size();
        const double NEG_INF = -1e18;

        std::vector<double> dp(N + 1, NEG_INF);
        struct Backlink {
            size_t prev_idx = 0;
            WordCandidate word;
            bool is_pinned = false;
            bool valid = false;
        };
        std::vector<Backlink> backpointer(N + 1);

        dp[0] = 0.0;

        // 輔助函式：檢查區間 [start, end) 是否與任何鎖定區間衝突
        auto overlaps_any_pin = [&](size_t start, size_t end) -> bool {
            for (const auto& p : pins) {
                size_t p_start = p.start_syllable_idx;
                size_t p_end = p.start_syllable_idx + p.length;
                if (start < p_end && end > p_start) {
                    return true;
                }
            }
            return false;
        };

        for (size_t i = 0; i < N; ++i) {
            if (dp[i] <= NEG_INF / 2.0) continue;

            // 1. 檢查位置 i 是否正好是某個使用者手動鎖定區段的起點
            const PinnedSegment* active_pin = nullptr;
            for (const auto& p : pins) {
                if (p.start_syllable_idx == i && i + p.length <= N) {
                    active_pin = &p;
                    break;
                }
            }

            if (active_pin) {
                size_t j = i + active_pin->length;
                double score = dp[i] + 50.0; // 絕對優先權
                if (score > dp[j]) {
                    dp[j] = score;
                    WordCandidate pinned_cand;
                    pinned_cand.text = active_pin->text;
                    pinned_cand.syllable_length = active_pin->length;
                    pinned_cand.log_prob = 0.0;
                    backpointer[j] = {i, pinned_cand, true, true};
                }
                continue;
            }

            // 若位置 i 落在某個鎖定區間內部，則不允許從此處發起新詞轉移
            if (overlaps_any_pin(i, i + 1)) {
                continue;
            }

            // 2. 一般字典樹前綴搜尋
            auto candidates = trie_.lookup_prefixes(syllables, i, false);

            bool any_valid_transition = false;
            for (const auto& w : candidates) {
                size_t j = i + w.syllable_length;
                if (j > N) continue;

                // 不可跨越任何已鎖定區段
                if (overlaps_any_pin(i, j)) continue;

                any_valid_transition = true;

                // 評分公式：對數機率 + 多音節長詞凝聚力加成
                double length_bonus = 0.0;
                if (w.syllable_length >= 2) {
                    length_bonus = 2.2 * static_cast<double>(w.syllable_length - 1);
                }

                double score = dp[i] + w.log_prob + length_bonus;
                if (score > dp[j]) {
                    dp[j] = score;
                    backpointer[j] = {i, w, false, true};
                }
            }

            // 3. 若無任何匹配詞彙，提供單音節未知字元保底，確保整句永不中斷
            if (!any_valid_transition) {
                size_t j = i + 1;
                double penalty_score = dp[i] - 25.0;
                if (penalty_score > dp[j]) {
                    dp[j] = penalty_score;
                    WordCandidate fallback;
                    fallback.text = syllables[i].to_string(); // 保底顯示原注音符號，如同 MS-IME 行為
                    fallback.syllable_length = 1;
                    fallback.log_prob = -25.0;
                    backpointer[j] = {i, fallback, false, true};
                }
            }
        }

        if (dp[N] <= NEG_INF / 2.0) {
            return {};
        }

        std::vector<SegmentedWord> reversed_words;
        size_t curr = N;
        while (curr > 0 && backpointer[curr].valid) {
            const auto& link = backpointer[curr];
            SegmentedWord seg;
            seg.text = link.word.text;
            seg.start_syllable_idx = link.prev_idx;
            seg.length = link.word.syllable_length;
            seg.score = link.word.log_prob;
            seg.is_pinned = link.is_pinned;
            reversed_words.push_back(seg);
            curr = link.prev_idx;
        }

        std::reverse(reversed_words.begin(), reversed_words.end());

        ViterbiResult result;
        result.total_score = dp[N];
        result.words = reversed_words;
        result.per_syllable_chars.resize(N);

        for (const auto& w : result.words) {
            result.full_sentence += w.text;
            // 將 UTF-8 中文字串按字元切分對應到各音節索引
            auto utf8_chars = split_utf8_chars(w.text);
            if (utf8_chars.size() == w.length) {
                for (size_t k = 0; k < w.length; ++k) {
                    result.per_syllable_chars[w.start_syllable_idx + k] = utf8_chars[k];
                }
            } else {
                // 若為注音符號保底等非等長情況，首音節放全文
                result.per_syllable_chars[w.start_syllable_idx] = w.text;
            }
        }

        return result;
    }

    static std::vector<std::string> split_utf8_chars(std::string_view str) {
        std::vector<std::string> chars;
        size_t i = 0;
        while (i < str.size()) {
            size_t len = 1;
            unsigned char lead = static_cast<unsigned char>(str[i]);
            if ((lead & 0xF8) == 0xF0) len = 4;
            else if ((lead & 0xF0) == 0xE0) len = 3;
            else if ((lead & 0xC0) == 0xC0) len = 2;
            chars.emplace_back(str.substr(i, len));
            i += len;
        }
        return chars;
    }

private:
    const ZhuyinTrie& trie_;
};

} // namespace eime
