#include "../sync/include/sync_journal.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    "line " + std::to_string(__LINE__) + ": " #condition); } while (0)

using eime::SyncJournal;
using eime::ZhuyinTrie;
namespace fs = std::filesystem;

static const auto you = eime::ZhuyinParser::parse_multi_syllables("ㄋㄧˇ");
static const auto good = eime::ZhuyinParser::parse_multi_syllables("ㄏㄠˇ");

static uint32_t boost(const ZhuyinTrie& trie, const std::vector<eime::Syllable>& syllables) {
    const auto words = trie.lookup_exact(syllables);
    return words.empty() ? 0 : words.front().user_boost;
}

static void same_device(const fs::path& root) {
    ZhuyinTrie a, b;
    SyncJournal first(root, "same_pc"), second(root, "same_pc");
    CHECK(first.own_delta_path() != second.own_delta_path());
    CHECK(first.sync_all_devices(a) == 0 && second.sync_all_devices(b) == 0);
    CHECK(first.record_learning(a, you, "你"));
    CHECK(second.record_learning(b, good, "好"));
    first.sync_all_devices(a);
    second.sync_all_devices(b);
    CHECK(boost(a, you) == 1 && boost(a, good) == 1);
    CHECK(boost(b, you) == 1 && boost(b, good) == 1);
    CHECK(first.sync_all_devices(a) == 0 && second.sync_all_devices(b) == 0);
}

static void compaction(const fs::path& root) {
    ZhuyinTrie a, b;
    SyncJournal writer(root, "writer"), reader(root, "reader");
    CHECK(writer.record_learning(a, you, "你"));
    CHECK(reader.sync_all_devices(b) == 1 && boost(b, you) == 1);
    CHECK(writer.compact_own_journal());
    CHECK(reader.sync_all_devices(b) == 0 && boost(b, you) == 1);
    for (int i = 0; i < 4; ++i) CHECK(writer.record_learning(a, you, "你"));
    reader.sync_all_devices(b);
    CHECK(boost(b, you) == 5);
    CHECK(writer.compact_own_journal());
    CHECK(writer.record_learning(a, you, "你", 2));
    CHECK(reader.sync_all_devices(b) == 1 && boost(b, you) == 7);
    CHECK(writer.sync_all_devices(a) == 0 && boost(a, you) == 7);
    ZhuyinTrie restarted;
    SyncJournal next(root, "reader");
    CHECK(next.sync_all_devices(restarted) == 1 && boost(restarted, you) == 7);
}

static void legacy_and_partial_records(const fs::path& root) {
    ZhuyinTrie trie;
    SyncJournal journal(root);
    const auto legacy = root / "device_old_pc.delta";
    {
        std::ofstream out(legacy, std::ios::binary);
        out << "# legacy per-device log\r\n1\t你\tㄋㄧˇ\t1\r\n"
               "bad\t你\tㄋㄧˇ\t8\n2\t你\tㄋㄧˇ\t-1\n"
               "3\t你\tㄋㄧˇ\t9oops\n4\t你\tㄋㄧˇ\t2";
    }
    CHECK(journal.sync_all_devices(trie) == 1 && boost(trie, you) == 1);
    CHECK(journal.sync_all_devices(trie) == 0);
    { std::ofstream out(legacy, std::ios::app | std::ios::binary); out << '\n'; }
    CHECK(journal.sync_all_devices(trie) == 1 && boost(trie, you) == 3);
    // A cloud client may briefly expose an older/truncated snapshot.
    { std::ofstream out(legacy, std::ios::binary); out << "1\t你\tㄋㄧˇ\t1\n"; }
    CHECK(journal.sync_all_devices(trie) == 0 && boost(trie, you) == 3);
    { std::ofstream out(legacy, std::ios::binary); out << "5\t你\tㄋㄧˇ\t5\n"; }
    CHECK(journal.sync_all_devices(trie) == 1 && boost(trie, you) == 5);
    CHECK(journal.sync_all_devices(trie) == 0);
}

static void large_counters(const fs::path& root) {
    ZhuyinTrie trie;
    SyncJournal journal(root);
    {
        std::ofstream out(root / "device_large.delta", std::ios::binary);
        out << "1\t你\tㄋㄧˇ\t18446744073709551615\n";
    }
    CHECK(journal.sync_all_devices(trie) == 1);
    CHECK(boost(trie, you) == std::numeric_limits<uint32_t>::max());
    CHECK(journal.sync_all_devices(trie) == 0);
    CHECK(journal.record_learning(trie, you, "你"));
    CHECK(boost(trie, you) == std::numeric_limits<uint32_t>::max());
}

static void failed_write(const fs::path& root) {
    ZhuyinTrie trie;
    SyncJournal journal(root);
    fs::create_directory(journal.own_delta_path()); // Cannot open a directory as a file.
    CHECK(!journal.record_learning(trie, you, "你"));
    CHECK(boost(trie, you) == 0);
    CHECK(!journal.record_learning(trie, you, "你\nextra"));
}

static void concurrent_hosts(const fs::path& root) {
    fs::create_directories(root);
    constexpr int hosts = 8, writes = 100;
    std::atomic<bool> failed{false}, finished{false};
    ZhuyinTrie observer_trie;
    SyncJournal observer(root, "observer");
    std::thread scanner([&] {
        while (!finished.load()) {
            observer.sync_all_devices(observer_trie);
            std::this_thread::yield();
        }
    });
    std::vector<std::thread> threads;
    for (int i = 0; i < hosts; ++i) {
        threads.emplace_back([&] {
            ZhuyinTrie local;
            SyncJournal writer(root, "same_pc");
            for (int n = 0; n < writes; ++n) {
                if (!writer.record_learning(local, you, "你")) failed = true;
                if (n % 20 == 0) writer.compact_own_journal();
            }
        });
    }
    for (auto& thread : threads) thread.join();
    finished = true;
    scanner.join();
    observer.sync_all_devices(observer_trie);
    CHECK(!failed);
    CHECK(boost(observer_trie, you) == hosts * writes);
    CHECK(observer.sync_all_devices(observer_trie) == 0);
    ZhuyinTrie fresh;
    SyncJournal restarted(root);
    restarted.sync_all_devices(fresh);
    CHECK(boost(fresh, you) == hosts * writes);
}

int main() {
    const auto root = fs::absolute(fs::path("build") / ("sync_regression_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count())));
    try {
        same_device(root / "same_device");
        compaction(root / "compaction");
        legacy_and_partial_records(root / "legacy");
        large_counters(root / "large");
        failed_write(root / "failed_write");
        concurrent_hosts(root / "concurrent");
        fs::remove_all(root); // Only the unique test directory created above.
        std::cout << "PASS: same-device hosts, compaction, restart, partial/legacy logs, "
                     "failed writes, 800 concurrent learning events\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << " (evidence: " << root << ")\n";
        return 1;
    }
}
