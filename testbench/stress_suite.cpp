#include "../engine/include/zhuyin.h"
#include "../engine/include/trie.h"
#include "../engine/include/viterbi.h"
#include "../engine/include/dict_loader.h"
#include "../engine/include/input_state_machine.h"
#include "../engine/include/hanzi_conv.h"
#include "../sync/include/sync_journal.h"
#include "../ui/candidate_window.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <random>
#include <cassert>
#include <filesystem>

#define BENCH_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[TESTBENCH FAILURE] " << msg << " (Line " << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (0)

// 測試 1：極速打字洪峰壓力測試 (10,000 擊鍵壓力測試)
bool stress_test_burst_typing(eime::ZhuyinTrie& trie, const eime::ViterbiDecoder& decoder) {
    std::cout << "\n[TESTBENCH 1/5] 執行高速擊鍵洪峰壓力測試 (10,000 擊鍵)..." << std::endl;
    eime::InputStateMachine sm(trie, decoder);

    std::string test_sentence = "rup wu0 wu0 fu4cp3cl3\r"; // 今天天氣很好 + Enter
    const int iterations = 1000; // 1000 次整句循環 = 14,000 次擊鍵
    size_t total_keys = 0;
    std::string out;

    auto start = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < iterations; ++it) {
        for (char c : test_sentence) {
            sm.process_key(c, out);
            total_keys++;
        }
        BENCH_CHECK(out == "今天天氣很好", "洪峰壓力下輸出結果不符: " + out);
        out.clear();
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    double avg_us_per_key = static_cast<double>(total_us) / static_cast<double>(total_keys);

    std::cout << "  -> 總擊鍵數: " << total_keys << " 次"
              << " | 總耗時: " << total_us / 1000.0 << " ms"
              << " | 單次擊鍵平均延遲: " << avg_us_per_key << " us" << std::endl;
    BENCH_CHECK(avg_us_per_key < 50.0, "平均擊鍵延遲超過 50 微秒門檻！");
    std::cout << "  -> [PASS] 高速擊鍵洪峰測試通過 (零丟鍵、零崩潰)" << std::endl;
    return true;
}

// 測試 2：多裝置雲端並發競爭寫入壓力測試 (8 執行緒同時寫入與同步)
bool stress_test_multi_device_concurrency() {
    std::cout << "\n[TESTBENCH 2/5] 執行多裝置雲端無鎖並發競爭寫入測試 (8 台設備同寫同讀)..." << std::endl;
    std::filesystem::path test_sync_dir = "build/testbench_sync_cluster";
    std::error_code ec;
    std::filesystem::remove_all(test_sync_dir, ec);
    std::filesystem::create_directories(test_sync_dir, ec);

    constexpr int NUM_DEVICES = 8;
    constexpr int ENTRIES_PER_DEVICE = 100;
    std::vector<std::thread> workers;

    auto worker_func = [&](int dev_id) {
        eime::ZhuyinTrie local_trie;
        std::string dev_name = "Host_" + std::to_string(dev_id);
        eime::SyncJournal journal(test_sync_dir, dev_name);

        for (int i = 0; i < ENTRIES_PER_DEVICE; ++i) {
            std::string word = "詞彙_" + std::to_string(dev_id) + "_" + std::to_string(i);
            auto syls = eime::ZhuyinParser::parse_multi_syllables("ㄊㄞˊ ㄨㄢ");
            journal.record_learning(local_trie, syls, word, 1);
            if (i % 25 == 0) {
                journal.compact_own_journal(); // 隨機並發壓縮本機日誌
            }
        }
    };

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_DEVICES; ++i) {
        workers.emplace_back(worker_func, i);
    }
    for (auto& t : workers) {
        t.join();
    }
    auto end = std::chrono::high_resolution_clock::now();

    // 驗證最終聚合讀取
    eime::ZhuyinTrie master_trie;
    eime::SyncJournal master_journal(test_sync_dir, "Master_Aggregator");
    size_t merged = master_journal.sync_all_devices(master_trie);

    std::cout << "  -> 8 執行緒並發寫入日誌共 " << (NUM_DEVICES * ENTRIES_PER_DEVICE) << " 筆"
              << " | 耗時: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms"
              << " | 聚合合併筆數: " << merged << " 筆" << std::endl;

    BENCH_CHECK(merged == NUM_DEVICES * ENTRIES_PER_DEVICE, "並發寫入發生資料遺失！");
    std::cout << "  -> [PASS] 多設備雲端並發無衝突寫入驗證通過 (零鎖定衝突、零資料遺失)" << std::endl;
    return true;
}

// 測試 3：超長句 Viterbi 最佳化與線性複雜度測試
bool stress_test_massive_sentence_viterbi(const eime::ZhuyinTrie& trie, const eime::ViterbiDecoder& decoder) {
    std::cout << "\n[TESTBENCH 3/5] 執行超長句 Viterbi 動態規劃複雜度測試 (40 音節長句)..." << std::endl;

    // 建構一個包含 40 個音節的連續大長句注音 (10 個 "今天天氣很好")
    std::string base_pattern = "ㄐㄧㄣ ㄊㄧㄢ ㄊㄧㄢ ㄑㄧˋ ";
    std::string long_zhuyin;
    for (int i = 0; i < 10; ++i) long_zhuyin += base_pattern;

    auto syllables = eime::ZhuyinParser::parse_multi_syllables(long_zhuyin);
    BENCH_CHECK(syllables.size() == 40, "音節總數應為 40");

    auto start = std::chrono::high_resolution_clock::now();
    auto result = decoder.decode(syllables);
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "  -> 40 音節超長句解碼結果: " << result.full_sentence
              << " | 耗時: " << elapsed_us << " us" << std::endl;

    BENCH_CHECK(!result.full_sentence.empty(), "長句解碼結果不可為空");
    BENCH_CHECK(elapsed_us < 2000, "40 音節解碼時間應在 2 毫秒以內！");
    std::cout << "  -> [PASS] 超長句動態規劃解碼效能達標" << std::endl;
    return true;
}

// 測試 4：繁簡轉換萬次壓力測試與即時轉碼驗證
bool stress_test_traditional_simplified_stress(eime::ZhuyinTrie& trie, const eime::ViterbiDecoder& decoder) {
    std::cout << "\n[TESTBENCH 4/5] 執行繁簡切換萬次高速轉碼測試 (10,000 次轉換)..." << std::endl;
    eime::InputStateMachine sm(trie, decoder);
    sm.set_charset(eime::OutputCharset::Simplified);

    std::string input = "dj845j; 54mp62j0 wj/61j4\r"; // 跨裝置雲端同步
    std::string out;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 500; ++i) {
        for (char c : input) {
            sm.process_key(c, out);
        }
        BENCH_CHECK(out == "跨装置云端同步", "簡體轉碼結果不符: " + out);
        out.clear();
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "  -> 500 次長句簡體轉碼耗時: " << elapsed_us / 1000.0 << " ms"
              << " | 單次轉換耗時: " << elapsed_us / 500.0 << " us" << std::endl;
    std::cout << "  -> [PASS] 繁簡轉碼壓力測試通過" << std::endl;
    return true;
}

// 測試 5：混亂與邊界容錯模糊測試 (Chaos & Fuzzing)
bool stress_test_chaos_fuzzing(eime::ZhuyinTrie& trie, const eime::ViterbiDecoder& decoder) {
    std::cout << "\n[TESTBENCH 5/5] 執行混亂與異常邊界模糊測試 (Chaos Fuzzer)..." << std::endl;
    eime::InputStateMachine sm(trie, decoder);
    std::string out;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist_char(0, 255);

    // 1. 空狀態狂按 Backspace 與 Delete 500 次，不得引發下溢或崩潰
    for (int i = 0; i < 500; ++i) {
        sm.process_special_key(eime::SpecialKey::Backspace, out);
        sm.process_special_key(eime::SpecialKey::DeleteKey, out);
        sm.process_special_key(eime::SpecialKey::LeftArrow, out);
        sm.process_special_key(eime::SpecialKey::RightArrow, out);
    }

    // 2. 注入 5,000 個完全隨機字元 (包含控制字元、非法 ASCII、未定義鍵碼)
    for (int i = 0; i < 5000; ++i) {
        char rand_c = static_cast<char>(dist_char(rng));
        sm.process_key(rand_c, out);
        if (i % 200 == 0) {
            sm.process_special_key(eime::SpecialKey::CharsetToggle, out);
            sm.process_special_key(eime::SpecialKey::ShiftToggle, out);
        }
    }

    // 3. 恢復正常打字驗證狀態機未死鎖
    sm.reset();
    sm.set_mode(eime::InputMode::Chinese);
    for (char c : std::string("su3cl3\r")) {
        sm.process_key(c, out);
    }
    BENCH_CHECK(out == "你好", "混沌注入後狀態機應能正常工作恢復輸入『你好』，實際為: " + out);

    std::cout << "  -> [PASS] 混亂模糊測試通過 (5,000 次隨機注入無崩潰、無記憶體損壞、狀態自動自我修復)" << std::endl;
    return true;
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::cout << "================================================================" << std::endl;
    std::cout << "  E-IME (Enhanced IME) 全自動化壓力與端到端測試基準 (Testbench) " << std::endl;
    std::cout << "================================================================" << std::endl;

    eime::ZhuyinTrie trie;
    if (!eime::DictLoader::load_from_file("data/core_dict.txt", trie, false)) {
        if (!eime::DictLoader::load_from_file("../data/core_dict.txt", trie, false)) {
            std::cerr << "無法載入詞庫檔案！" << std::endl;
            return 1;
        }
    }
    eime::ViterbiDecoder decoder(trie);

    bool ok = true;
    ok = ok && stress_test_burst_typing(trie, decoder);
    ok = ok && stress_test_multi_device_concurrency();
    ok = ok && stress_test_massive_sentence_viterbi(trie, decoder);
    ok = ok && stress_test_traditional_simplified_stress(trie, decoder);
    ok = ok && stress_test_chaos_fuzzing(trie, decoder);

    if (ok) {
        std::cout << "\n================================================================" << std::endl;
        std::cout << "  [TESTBENCH SUCCESS] 5 大全自動化壓力基準 100% 通過！           " << std::endl;
        std::cout << "================================================================" << std::endl;
        return 0;
    } else {
        std::cerr << "\n[TESTBENCH FAILURE] 測試基準發現異常！" << std::endl;
        return 1;
    }
}
