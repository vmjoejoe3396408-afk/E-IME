#pragma once
#include "../../engine/include/user_preferences.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <cwctype>

namespace eime {
// A locked read/merge/atomic-replace transaction prevents lost updates between
// host processes. The core lexicon and legacy learning journal are never edited.
class UserPreferencesStore {
public:
    explicit UserPreferencesStore(std::filesystem::path root) : root_(std::move(root)) {}
    static std::filesystem::path default_root() {
        wchar_t path[32768] = {};
        DWORD size = GetEnvironmentVariableW(L"EIME_PRIVATE_DATA_DIR", path, 32768);
        if (size && size < 32768) return path;
        size = GetEnvironmentVariableW(L"LOCALAPPDATA", path, 32768);
        return size && size < 32768 ? std::filesystem::path(path) / L"E-IME" / L"user" : std::filesystem::path{};
    }
    const UserPreferences& data() const { return data_; }
    const std::wstring& error() const { return error_; }
    const std::filesystem::path& root() const { return root_; }
    bool reload(bool* changed = nullptr) {
        Lock lock(root_);
        if (!lock) return fail(L"無法鎖定私人詞庫，請稍後重試。");
        UserPreferences next;
        std::string raw;
        bool unchanged = false;
        if (!read(next, raw, &unchanged)) return false;
        if (unchanged) { if (changed) *changed = false; error_.clear(); return true; }
        if (changed) *changed = raw != raw_;
        data_ = std::move(next); raw_ = std::move(raw); error_.clear();
        return true;
    }
    bool save_word(std::string reading, const std::string& text, const UserWordKey& old = {}) {
        reading = canonical_reading(reading);
        if (!valid_user_word(reading, text)) return fail(L"詞彙與注音須逐字對應；每字一個完整注音，音節以空格分隔（最多 64 字）。");
        if ((!old.first.empty() || !old.second.empty()) && !valid_user_word(old.first, old.second)) return fail(L"原詞彙資料無效，未儲存。");
        return change([&](UserPreferences& next) {
            UserWordKey key{reading, text};
            if (!old.first.empty() && old != key) erase_word(next, old);
            next.words.insert(key);
        });
    }
    bool delete_word(const UserWordKey& key) {
        if (!valid_user_word(key.first, key.second) || canonical_reading(key.first) != key.first) return fail(L"詞彙資料無效，未刪除。");
        return change([&](UserPreferences& next) { erase_word(next, key); });
    }
    bool reorder(const std::string& reading, const std::string& text, RankAction action,
        const std::vector<std::string>& current) {
        const auto normalized = canonical_reading(reading);
        if (normalized.empty() || (action != RankAction::Reset && !valid_user_word(normalized, text)))
            return fail(L"這個候選沒有完整注音，無法固定排序。");
        return change([&](UserPreferences& next) {
            if (action == RankAction::Reset) { next.ranks.erase(normalized); return; }
            auto& order = next.ranks[normalized];
            for (const auto& word : current) {
                if (valid_user_word(normalized, word) && std::find(order.begin(), order.end(), word) == order.end()) order.push_back(word);
            }
            auto found = std::find(order.begin(), order.end(), text);
            if (found == order.end()) { order.push_back(text); found = order.end() - 1; }
            if (action == RankAction::Top) { order.erase(found); order.insert(order.begin(), text); }
            else if (action == RankAction::Up && found != order.begin()) std::iter_swap(found, found - 1);
            else if (action == RankAction::Down && found + 1 != order.end()) std::iter_swap(found, found + 1);
        });
    }
private:
    struct Lock {
        HANDLE handle = nullptr;
        bool acquired = false;
        explicit Lock(const std::filesystem::path& root) {
            if (root.empty()) return;
            auto path = std::filesystem::absolute(root).lexically_normal().wstring();
            unsigned long long hash = 14695981039346656037ULL;
            for (wchar_t c : path) { hash ^= static_cast<unsigned>(std::towlower(c)); hash *= 1099511628211ULL; }
            auto name = L"Local\\EIME-UserPreferences-" + std::to_wstring(hash);
            handle = CreateMutexW(nullptr, FALSE, name.c_str());
            if (handle) { DWORD result = WaitForSingleObject(handle, 3000); acquired = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED; }
        }
        ~Lock() { if (acquired) ReleaseMutex(handle); if (handle) CloseHandle(handle); }
        explicit operator bool() const { return acquired; }
    };
    bool fail(const wchar_t* message) { error_ = message; return false; }
    static void erase_word(UserPreferences& next, const UserWordKey& key) {
        next.words.erase(key);
        auto rank = next.ranks.find(key.first);
        if (rank != next.ranks.end()) {
            std::erase(rank->second, key.second);
            if (rank->second.empty()) next.ranks.erase(rank);
        }
    }
    bool read(UserPreferences& next, std::string& raw, bool* unchanged = nullptr) {
        std::error_code ec;
        const auto file = root_ / L"preferences.tsv";
        bool exists = std::filesystem::exists(file, ec);
        if (ec) return fail(L"無法讀取私人詞庫目錄。");
        if (!exists) return true;
        auto size = std::filesystem::file_size(file, ec);
        if (ec || size > 4 * 1024 * 1024) return fail(L"私人詞庫檔案過大或無法讀取，未覆寫原檔。");
        std::ifstream input(file, std::ios::binary);
        if (!input) return fail(L"無法開啟私人詞庫。");
        raw.assign(std::istreambuf_iterator<char>(input), {});
        if (input.bad() || raw.size() != size) return fail(L"私人詞庫讀取不完整，未覆寫原檔。");
        if (unchanged && !raw_.empty() && raw == raw_) { *unchanged = true; return true; }
        auto parseable = raw;
        if (parseable.starts_with("\xef\xbb\xbf")) parseable.erase(0, 3);
        std::istringstream lines(parseable);
        std::string line;
        if (!std::getline(lines, line)) return fail(L"私人詞庫格式不符，未覆寫原檔。");
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line != "EIME_USER_1") return fail(L"私人詞庫格式不符，未覆寫原檔。");
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            std::vector<std::string> fields;
            std::istringstream row(line);
            std::string field;
            while (std::getline(row, field, '\t')) fields.push_back(field);
            if (fields.size() < 3 || canonical_reading(fields[1]) != fields[1]) return fail(L"私人詞庫內容損壞，未覆寫原檔。");
            for (size_t i = 2; i < fields.size(); ++i)
                if (!valid_user_word(fields[1], fields[i])) return fail(L"私人詞庫的詞彙與注音不符，未覆寫原檔。");
            if (fields[0] == "W" && fields.size() == 3) next.words.emplace(fields[1], fields[2]);
            else if (fields[0] == "R") {
                auto& order = next.ranks[fields[1]];
                for (size_t i = 2; i < fields.size(); ++i) {
                    if (std::find(order.begin(), order.end(), fields[i]) != order.end()) return fail(L"私人順位有重複項目，未覆寫原檔。");
                    order.push_back(fields[i]);
                }
            } else return fail(L"私人詞庫含未知紀錄，未覆寫原檔。");
        }
        return true;
    }
    template<class Operation> bool change(Operation operation) {
        Lock lock(root_);
        if (!lock) return fail(L"無法鎖定私人詞庫，請稍後重試。");
        UserPreferences next;
        std::string ignored;
        if (!read(next, ignored)) return false;
        operation(next);
        std::ostringstream output;
        output << "EIME_USER_1\n";
        for (const auto& key : next.words) output << "W\t" << key.first << '\t' << key.second << '\n';
        for (const auto& [reading, words] : next.ranks) {
            if (words.empty()) continue;
            output << "R\t" << reading;
            for (const auto& word : words) output << '\t' << word;
            output << '\n';
        }
        const auto bytes = output.str();
        if (bytes.size() > 4 * 1024 * 1024) return fail(L"私人詞庫已達容量上限。");
        std::error_code ec;
        std::filesystem::create_directories(root_, ec);
        if (ec) return fail(L"無法建立私人詞庫目錄，未儲存。");
        static std::atomic<unsigned long> counter{0};
        auto temporary = root_ / (L"preferences-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++counter) + L".tmp");
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return fail(L"無法建立暫存詞庫，未儲存。");
        DWORD written = 0;
        bool success = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
            written == bytes.size() && FlushFileBuffers(file);
        CloseHandle(file);
        success = success && MoveFileExW(temporary.c_str(), (root_ / L"preferences.tsv").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!success) { DeleteFileW(temporary.c_str()); return fail(L"私人詞庫儲存失敗，原檔保留。"); }
        data_ = std::move(next); raw_ = bytes; error_.clear(); return true;
    }
    std::filesystem::path root_;
    UserPreferences data_;
    std::string raw_;
    std::wstring error_;
};
}
