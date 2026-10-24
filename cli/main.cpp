#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#endif

#include "engine/include/zhuyin.h"
#include "engine/include/trie.h"
#include "engine/include/viterbi.h"
#include "engine/include/dict_loader.h"
#include "engine/include/input_state_machine.h"
#include "sync/include/sync_journal.h"

void run_automated_benchmark(eime::ZhuyinTrie& trie, const eime::ViterbiDecoder& decoder, eime::SyncJournal* journal) {
    std::cout << "\n=== [E-IME (Enhanced IME)] 執行自動化基準與選詞測試 ===" << std::endl;

    struct TestCase {
        std::string name;
        std::string raw_keys;
        std::string expected;
    };

    std::vector<TestCase> cases = {
        {"日常問候 (你好)", "su3cl3\r", "你好"},
        {"單字變換 (變)", "1u04\r", "變"},
        {"台灣地名 (台灣)", "w96j0 \r", "台灣"},
        {"科技名詞 (台積電)", "w96ru 2u04\r", "台積電"},
        {"完整長句 (今天天氣很好)", "rup wu0 wu0 fu4cp3cl3\r", "今天天氣很好"},
        {"工程語句 (跨裝置雲端同步)", "dj845j; 54mp62j0 wj/61j4\r", "跨裝置雲端同步"},
        {"開發語句 (系統架構最佳化)", "vu4wj/3ru84e.4yjo4ru8 cj84\r", "系統架構最佳化"}
    };

    for (const auto& tc : cases) {
        eime::InputStateMachine sm(trie, decoder, journal);
        std::string committed;

        auto start = std::chrono::high_resolution_clock::now();
        for (char c : tc.raw_keys) {
            std::string out;
            sm.process_key(c, out);
            if (!out.empty()) {
                committed += out;
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::string clean_keys = tc.raw_keys.substr(0, tc.raw_keys.size() - 1);
        std::cout << "  * [" << tc.name << "] 輸入: \"" << clean_keys << "\""
                  << " -> 輸出: \"" << committed << "\""
                  << " | 耗時: " << elapsed_us << " us"
                  << (committed == tc.expected ? " [PASS]" : " [DIFF]") << std::endl;
    }
    std::cout << "=== 基準測試完成 ===\n" << std::endl;
}

void run_interactive_cli(eime::ZhuyinTrie& trie, const eime::ViterbiDecoder& decoder, eime::SyncJournal* journal) {
    std::cout << "=================================================================" << std::endl;
    std::cout << "  E-IME (改進型注音輸入法) - 終端互動模擬器         " << std::endl;
    std::cout << "  - 直接鍵入大千注音 (如 w96ru 2u04 -> 台積電)                   " << std::endl;
    std::cout << "  - 方向鍵 [←]/[→] 移動組字游標，[↓] 或 [空白鍵] 展開候選字窗     " << std::endl;
    std::cout << "  - 數字鍵 [1]~[9] 局部鎖定選字並寫入跨裝置學習日誌              " << std::endl;
    std::cout << "  - 按 [`] 模擬 Shift 切換中/英；按 [Ctrl+F] 切換繁/簡；Ctrl+C 結束" << std::endl;
    std::cout << "=================================================================\n" << std::endl;

    eime::InputStateMachine sm(trie, decoder, journal);

#ifdef _WIN32
    while (true) {
        int ch = _getch();
        if (ch == 3) {
            std::cout << "\n結束互動測試。" << std::endl;
            break;
        }

        std::string committed;
        auto start = std::chrono::high_resolution_clock::now();

        if (ch == 0 || ch == 224) {
            int ext = _getch();
            switch (ext) {
                case 75: sm.process_special_key(eime::SpecialKey::LeftArrow, committed); break;
                case 77: sm.process_special_key(eime::SpecialKey::RightArrow, committed); break;
                case 72: sm.process_special_key(eime::SpecialKey::UpArrow, committed); break;
                case 80: sm.process_special_key(eime::SpecialKey::DownArrow, committed); break;
                case 71: sm.process_special_key(eime::SpecialKey::Home, committed); break;
                case 79: sm.process_special_key(eime::SpecialKey::End, committed); break;
                case 83: sm.process_special_key(eime::SpecialKey::DeleteKey, committed); break;
                default: break;
            }
        } else if (ch == '`') {
            sm.process_special_key(eime::SpecialKey::ShiftToggle, committed);
        } else if (ch == 6) { // Ctrl + F: 繁簡切換
            sm.process_special_key(eime::SpecialKey::CharsetToggle, committed);
        } else {
            bool eaten = sm.process_key(static_cast<char>(ch), committed);
            if (!eaten && sm.mode() == eime::InputMode::English && ch >= 32 && ch < 127) {
                committed.push_back(static_cast<char>(ch));
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        if (!committed.empty()) {
            std::cout << "\n[上屏輸出]: " << committed << std::endl;
        }

        std::string mode_tag = (sm.mode() == eime::InputMode::Chinese) ? "[中]" : "[英]";
        std::string charset_tag = (sm.charset() == eime::OutputCharset::Simplified) ? "[簡]" : "[繁]";
        std::cout << "\r\033[K" << mode_tag << charset_tag << " 組字區: " << sm.get_display_line_with_cursor();
        if (sm.state() == eime::EngineState::CandidateOpen) {
            std::cout << " | 候選(" << (sm.candidate_page() + 1) << "/" << sm.total_candidate_pages() << "): ";
            auto cands = sm.current_page_candidates();
            for (size_t i = 0; i < cands.size(); ++i) {
                std::cout << (i + 1) << "." << cands[i].text << " ";
            }
        }
        std::cout << " (" << elapsed_us << " us)" << std::flush;
    }
#endif
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::cout << "[E-IME] 初始化核心語言引擎與跨裝置同步層..." << std::endl;

    eime::ZhuyinTrie trie;
    std::string dict_path = "data/core_dict.txt";
    bool non_interactive = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--bench" || arg == "--test" || arg == "--no-interactive") {
            non_interactive = true;
        } else if (!arg.empty() && arg[0] != '-') {
            dict_path = arg;
        }
    }

    if (!eime::DictLoader::load_from_file(dict_path, trie)) {
        if (!eime::DictLoader::load_from_file("../data/core_dict.txt", trie)) {
            std::cerr << "[E-IME] 致命錯誤：找不到系統詞庫檔案！" << std::endl;
            return 1;
        }
        eime::DictLoader::load_from_file("../data/custom_words.txt", trie, false);
    } else {
        eime::DictLoader::load_from_file("data/custom_words.txt", trie, false);
    }

    eime::SyncJournal journal("data/sync");
    size_t synced = journal.sync_all_devices(trie);
    if (synced > 0) {
        std::cout << "[eime::SyncJournal] 已從跨裝置增量日誌載入 " << synced << " 筆個人詞頻紀錄" << std::endl;
    }

    eime::ViterbiDecoder decoder(trie);

    run_automated_benchmark(trie, decoder, &journal);

    if (!non_interactive) {
        run_interactive_cli(trie, decoder, &journal);
    }

    return 0;
}
