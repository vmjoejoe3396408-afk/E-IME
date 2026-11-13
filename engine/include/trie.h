#pragma once

#include "zhuyin.h"
#include "user_preferences.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <limits>

namespace eime {

struct WordCandidate {
    std::string text;
    uint32_t base_frequency = 0;
    uint32_t user_boost = 0;      // 來自使用者個人學習 / 跨裝置同步的增量詞頻
    double log_prob = -100.0;     // log((base + user_boost * K) / total_freq)
    size_t syllable_length = 0;   // 此詞佔用的音節數
    int manual_rank = -1; // Per-reading candidate order, independent of frequency.

    uint64_t effective_frequency() const {
        // 使用者主動選過的詞給予顯著加權 (1 次選字相當於基礎詞頻 +250,000)
        return static_cast<uint64_t>(base_frequency) + static_cast<uint64_t>(user_boost) * 250000ULL;
    }

    bool operator<(const WordCandidate& other) const {
        if (manual_rank != other.manual_rank && (manual_rank >= 0 || other.manual_rank >= 0)) {
            return manual_rank >= 0 && (other.manual_rank < 0 || manual_rank < other.manual_rank);
        }
        if (effective_frequency() != other.effective_frequency()) {
            return effective_frequency() > other.effective_frequency();
        }
        return text < other.text;
    }
};

class ZhuyinTrie {
public:
    struct Node {
        std::unordered_map<std::string, std::unique_ptr<Node>> children;
        std::vector<WordCandidate> words;
        bool is_word = false;
    };

    ZhuyinTrie() : root_(std::make_unique<Node>()), total_frequency_(0), word_count_(0) {}

    void set_user_preferences(const UserPreferences& preferences) {
        UserPreferences next_preferences = preferences;
        auto next_words = std::make_unique<ZhuyinTrie>();
        for (const auto& [reading, text] : preferences.words) {
            if (valid_user_word(reading, text))
                next_words->insert(ZhuyinParser::parse_multi_syllables(reading), text, 1000000);
        }
        next_words->finalize_probabilities();
        std::swap(preferences_, next_preferences);
        user_words_ = std::move(next_words);
    }
    bool is_private_word(const std::vector<Syllable>& syllables, const std::string& text) const {
        return preferences_.words.contains({reading_for(syllables, 0, syllables.size()), text});
    }

    // 插入系統基礎詞彙
    void insert(const std::vector<Syllable>& syllables, const std::string& text, uint32_t freq) {
        if (syllables.empty() || text.empty()) return;

        Node* current = get_or_create_node(syllables);
        current->is_word = true;

        for (auto& w : current->words) {
            if (w.text == text) {
                if (freq > w.base_frequency) {
                    total_frequency_ += (freq - w.base_frequency);
                    w.base_frequency = freq;
                }
                update_single_prob(w);
                return;
            }
        }

        WordCandidate cand;
        cand.text = text;
        cand.base_frequency = freq;
        cand.user_boost = 0;
        cand.syllable_length = syllables.size();
        total_frequency_ += freq;
        word_count_++;
        update_single_prob(cand);
        current->words.push_back(cand);
    }

    // 動態增加使用者個人詞頻或新增自訂詞 (供即時選字學習與雲端 Delta 合併呼叫，O(K) 極速更新)
    void apply_user_delta(const std::vector<Syllable>& syllables, const std::string& text, uint32_t delta_count) {
        if (syllables.empty() || text.empty() || delta_count == 0) return;

        Node* current = get_or_create_node(syllables);
        current->is_word = true;

        for (auto& w : current->words) {
            if (w.text == text) {
                w.user_boost = static_cast<uint32_t>(std::min<uint64_t>(
                    static_cast<uint64_t>(w.user_boost) + delta_count,
                    std::numeric_limits<uint32_t>::max()));
                update_single_prob(w);
                std::sort(current->words.begin(), current->words.end());
                return;
            }
        }

        // 若為系統詞庫未收錄之全新自造詞，直接建立並賦予初始基礎分
        WordCandidate cand;
        cand.text = text;
        cand.base_frequency = 300000;
        cand.user_boost = delta_count;
        cand.syllable_length = syllables.size();
        total_frequency_ += cand.base_frequency;
        word_count_++;
        update_single_prob(cand);
        current->words.push_back(cand);
        std::sort(current->words.begin(), current->words.end());
    }

    // 批次載入完成後統一刷新全局對數機率並排序各節點
    void finalize_probabilities() {
        if (total_frequency_ == 0) total_frequency_ = 1;
        recompute_node_probs(root_.get());
    }

    // 精確查詢：給定完整注音序列，回傳所有精確匹配的候選詞（已按權重由高到低排序）
    std::vector<WordCandidate> lookup_exact(const std::vector<Syllable>& syllables) const {
        Node* node = find_node(syllables);
        std::vector<WordCandidate> result;
        if (node && node->is_word) result = node->words;
        if (user_words_) merge_personal(result, user_words_->lookup_exact(syllables));
        apply_preferences(result, syllables, 0);
        std::sort(result.begin(), result.end());
        return result;
    }

    // 前綴查詢：給定音節陣列與起點 start_idx，找出從 start_idx 出發的所有長度 (1, 2, 3...) 匹配詞彙
    // 若 strict_Order 為 true，可按「長詞優先、同長按頻率」排序供候選字窗顯示
    std::vector<WordCandidate> lookup_prefixes(
        const std::vector<Syllable>& syllables,
        size_t start_idx,
        bool prefer_longer_for_candidates = false
    ) const {
        std::vector<WordCandidate> results;
        Node* current = root_.get();

        for (size_t i = start_idx; i < syllables.size(); ++i) {
            std::string key = syllables[i].to_string();
            auto it = current->children.find(key);
            if (it == current->children.end()) {
                break;
            }
            current = it->second.get();
            if (current->is_word) {
                for (const auto& w : current->words) {
                    results.push_back(w);
                }
            }
        }

        if (user_words_) merge_personal(results, user_words_->lookup_prefixes(syllables, start_idx, false));
        apply_preferences(results, syllables, start_idx);
        if (prefer_longer_for_candidates) {
            std::sort(results.begin(), results.end(), [](const WordCandidate& a, const WordCandidate& b) {
                if (a.syllable_length != b.syllable_length) {
                    return a.syllable_length > b.syllable_length;
                }
                return a < b;
            });
        } else {
            std::sort(results.begin(), results.end());
        }
        return results;
    }

    uint64_t total_frequency() const { return total_frequency_; }
    size_t word_count() const { return word_count_; }

private:
    UserPreferences preferences_;
    std::unique_ptr<ZhuyinTrie> user_words_;
    void merge_personal(std::vector<WordCandidate>& target, const std::vector<WordCandidate>& words) const {
        for (auto word : words) {
            auto found = std::find_if(target.begin(), target.end(), [&](const auto& current) {
                return current.text == word.text && current.syllable_length == word.syllable_length;
            });
            if (found != target.end()) {
                found->base_frequency = std::max(found->base_frequency, word.base_frequency);
                found->log_prob = std::log(static_cast<double>(found->effective_frequency()) /
                    static_cast<double>(std::max<uint64_t>(1, total_frequency_)) + 1e-12);
            } else {
                word.log_prob = std::log(static_cast<double>(word.effective_frequency()) /
                    static_cast<double>(std::max<uint64_t>({1, total_frequency_, user_words_->total_frequency()})) + 1e-12);
                target.push_back(std::move(word));
            }
        }
    }
    void apply_preferences(std::vector<WordCandidate>& words, const std::vector<Syllable>& syllables, size_t start) const {
        for (auto& word : words) {
            const auto reading = reading_for(syllables, start, word.syllable_length);
            word.manual_rank = -1;
            auto group = preferences_.ranks.find(reading);
            if (group != preferences_.ranks.end()) {
                auto found = std::find(group->second.begin(), group->second.end(), word.text);
                if (found != group->second.end()) word.manual_rank = static_cast<int>(found - group->second.begin());
            }
        }
    }
    std::unique_ptr<Node> root_;
    uint64_t total_frequency_;
    size_t word_count_;

    Node* get_or_create_node(const std::vector<Syllable>& syllables) {
        Node* current = root_.get();
        for (const auto& s : syllables) {
            std::string key = s.to_string();
            auto& child = current->children[key];
            if (!child) {
                child = std::make_unique<Node>();
            }
            current = child.get();
        }
        return current;
    }

    Node* find_node(const std::vector<Syllable>& syllables) const {
        Node* current = root_.get();
        for (const auto& s : syllables) {
            std::string key = s.to_string();
            auto it = current->children.find(key);
            if (it == current->children.end()) {
                return nullptr;
            }
            current = it->second.get();
        }
        return current;
    }

    void update_single_prob(WordCandidate& w) const {
        double denom = static_cast<double>(std::max<uint64_t>(1, total_frequency_));
        double p = static_cast<double>(w.effective_frequency()) / denom;
        w.log_prob = std::log(p + 1e-12);
    }

    void recompute_node_probs(Node* node) {
        if (!node) return;
        for (auto& w : node->words) {
            update_single_prob(w);
        }
        std::sort(node->words.begin(), node->words.end());
        for (auto& [k, child] : node->children) {
            recompute_node_probs(child.get());
        }
    }
};

} // namespace eime
