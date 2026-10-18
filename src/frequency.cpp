#include "frequency.hpp"
#include <wincrypt.h>
#include <fstream>
#include <cmath>
#include <cstring>
#include <array>
namespace es {
ReadingMatch match_reading(std::string_view reading, std::string_view input) {
    if (input.empty() || input.size() > 128 || reading.empty() || reading.size() > 128)
        return ReadingMatch::None;
    // Bit 1: all syllables complete; bit 2: at least one abbreviated syllable.
    // Fixed storage avoids per-word allocations when searching 10,000 learned entries.
    std::array<unsigned char, 129> positions{};
    positions[0] = 1;
    size_t syllables = 0;
    for (size_t start = 0; start < reading.size();) {
        auto end = reading.find(' ', start);
        if (end == std::string_view::npos)
            end = reading.size();
        if (end == start || ++syllables > 16 || (end < reading.size() && end + 1 == reading.size()))
            return ReadingMatch::None;
        auto syllable = reading.substr(start, end - start);
        if (syllable.find_first_not_of("abcdefghijklmnopqrstuvwxyz") != std::string_view::npos)
            return ReadingMatch::None;
        std::array<unsigned char, 129> next{};
        for (size_t p = 0; p < input.size(); ++p)
            if (positions[p]) {
                size_t q = p;
                if (q && input[q] == '\'')
                    ++q;
                auto spelling = input.substr(q, syllable.size());
                bool full = spelling == syllable;
                // Exactly the derive rules in e_shuru.schema.yaml; never conflate lu/lv or nu/nv.
                if (!full && spelling.size() == syllable.size() && syllable.size() >= 2 &&
                    spelling[0] == syllable[0] && spelling[1] == 'v' && syllable[1] == 'u' &&
                    spelling.substr(2) == syllable.substr(2)) {
                    full = syllable == "nue" || syllable == "lue" ||
                           std::string_view("jqxy").find(syllable[0]) != std::string_view::npos;
                }
                if (full)
                    next[q + syllable.size()] |= positions[p];
                if (syllable.size() > 1 && q < input.size() && input[q] == syllable[0])
                    next[q + 1] |= 2;
                if (syllable.size() > 2 &&
                    (syllable.starts_with("zh") || syllable.starts_with("ch") ||
                     syllable.starts_with("sh")) &&
                    input.substr(q, 2) == syllable.substr(0, 2))
                    next[q + 2] |= 2;
            }
        positions = next;
        start = end + 1;
    }
    return positions[input.size()] & 1 ? ReadingMatch::Full
           : positions[input.size()]   ? ReadingMatch::Abbreviated
                                       : ReadingMatch::None;
}
bool matches_reading(std::string_view reading, std::string_view input) {
    return match_reading(reading, input) != ReadingMatch::None;
}
ReadingIndex::~ReadingIndex() {
    if (data_)
        UnmapViewOfFile(data_);
}
std::string_view ReadingIndex::row(uint32_t i) const {
    if (!data_ || i >= count_)
        return {};
    uint32_t start, end;
    std::memcpy(&start, data_ + 12 + i * 4, 4);
    std::memcpy(&end, data_ + 12 + (i + 1) * 4, 4);
    if (start < 12 + (size_t(count_) + 1) * 4 || start > end || end > size_)
        return {};
    return {data_ + start, end - start};
}
bool ReadingIndex::open(const std::filesystem::path &path) {
    file_.reset(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    LARGE_INTEGER n{};
    if (!file_ || !GetFileSizeEx(file_.value, &n) || n.QuadPart < 16 || n.QuadPart > 32 * 1024 * 1024)
        return false;
    size_ = size_t(n.QuadPart);
    mapping_.reset(CreateFileMappingW(file_.value, nullptr, PAGE_READONLY, 0, 0, nullptr));
    if (!mapping_)
        return false;
    data_ = static_cast<const char *>(MapViewOfFile(mapping_.value, FILE_MAP_READ, 0, 0, 0));
    if (!data_ || std::memcmp(data_, "ESREAD01", 8))
        return false;
    std::memcpy(&count_, data_ + 8, 4);
    if (count_ > 300000 || 12 + (size_t(count_) + 1) * 4 > size_) {
        count_ = 0;
        return false;
    }
    std::string_view previous;
    for (uint32_t i = 0; i < count_; ++i) {
        auto r = row(i);
        if (r.empty() || r.size() > 512 || r.find('\t') == std::string_view::npos || r < previous) {
            count_ = 0;
            return false;
        }
        previous = r;
    }
    return true;
}
std::optional<WordKey> ReadingIndex::identify(const std::string &text, const std::string &input) const {
    if (!count_ || text.empty())
        return {};
    auto prefix = text + "\t";
    uint32_t low = 0, high = count_;
    while (low < high) {
        auto mid = low + (high - low) / 2;
        if (row(mid) < prefix)
            low = mid + 1;
        else
            high = mid;
    }
    std::optional<WordKey> result;
    for (; low < count_; ++low) {
        auto r = row(low);
        if (!r.starts_with(prefix))
            break;
        auto reading = r.substr(prefix.size());
        if (matches_reading(reading, input)) {
            if (result && result->reading != reading)
                return {}; // ambiguous pronunciation: don't learn
            result = WordKey{text, std::string(reading)};
        }
    }
    return result;
}
bool ReadingIndex::contains(const WordKey &key) const {
    auto target = key.text + "\t" + key.reading;
    uint32_t low = 0, high = count_;
    while (low < high) {
        auto mid = low + (high - low) / 2;
        if (row(mid) < target)
            low = mid + 1;
        else
            high = mid;
    }
    return low < count_ && row(low) == target;
}
int FrequencyStore::today() {
    return int(
        std::chrono::duration_cast<std::chrono::hours>(std::chrono::system_clock::now().time_since_epoch())
            .count() /
        24);
}
double FrequencyStore::score(const Entry &e, int day) {
    return e.weight * std::exp2(-double(std::max(0, day - e.day)) / 90.0);
}
std::optional<FrequencyStore::Entries> FrequencyStore::load(const std::filesystem::path &path) {
    try {
        if (std::filesystem::exists(path)) {
            auto size = std::filesystem::file_size(path);
            if (size > 8 * 1024 * 1024)
                throw std::runtime_error("size");
            std::ifstream f(path, std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(f)), {});
            if (!f || bytes.size() != size)
                throw std::runtime_error("read");
            DATA_BLOB input{DWORD(bytes.size()), reinterpret_cast<BYTE *>(bytes.data())}, output{};
            if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                                    &output))
                throw std::runtime_error("decrypt");
            Json j;
            try {
                j = Json::parse(output.pbData, output.pbData + output.cbData);
            } catch (...) {
                SecureZeroMemory(output.pbData, output.cbData);
                LocalFree(output.pbData);
                throw;
            }
            SecureZeroMemory(output.pbData, output.cbData);
            LocalFree(output.pbData);
            if (j.at("v") != 1 || !j.at("words").is_array() || j["words"].size() > 10000)
                throw std::runtime_error("format");
            Entries entries;
            for (auto &w : j["words"]) {
                WordKey k{w.at(0).get<std::string>(), w.at(1).get<std::string>()};
                Entry e{w.at(2).get<double>(), w.at(3).get<int>()};
                if (k.text.empty() || k.text.size() > 64 || k.reading.empty() || k.reading.size() > 128 ||
                    !std::isfinite(e.weight) || e.weight < 0 || e.weight > 10000 || e.day < 0 ||
                    e.day > today() + 1)
                    throw std::runtime_error("entry");
                if (!entries.emplace(std::move(k), e).second)
                    throw std::runtime_error("duplicate");
            }
            return entries;
        }
    } catch (...) {
    }
    return {};
}
namespace {
std::filesystem::path suffixed(std::filesystem::path path, const wchar_t *suffix) {
    path += suffix;
    return path;
}
bool exists_or_inaccessible(const std::filesystem::path &path) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        return true;
    auto error = GetLastError();
    return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
}
bool remove_file(const std::filesystem::path &path) {
    if (DeleteFileW(path.c_str()))
        return true;
    auto error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}
std::optional<std::string> encrypt_snapshot(const Json &snapshot) {
    auto plain = snapshot.dump();
    DATA_BLOB in{DWORD(plain.size()), reinterpret_cast<BYTE *>(plain.data())}, out{};
    bool ok = CryptProtectData(&in, L"EShuRu word frequency", nullptr, nullptr, nullptr,
                               CRYPTPROTECT_UI_FORBIDDEN, &out) != FALSE;
    SecureZeroMemory(plain.data(), plain.size());
    if (!ok)
        return {};
    std::string result(reinterpret_cast<char *>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return result;
}
bool write_flushed(const std::filesystem::path &path, const std::string &bytes) {
    Handle file(
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        return false;
    DWORD written = 0;
    return WriteFile(file.value, bytes.data(), DWORD(bytes.size()), &written, nullptr) &&
           written == bytes.size() && FlushFileBuffers(file.value);
}
bool replace_file(const std::filesystem::path &from, const std::filesystem::path &to) {
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}
} // namespace
FrequencyStore::FrequencyStore(std::filesystem::path path) : path_(std::move(path)) {
    auto primary = load(path_);
    if (primary)
        entries_ = std::move(*primary);
    else if (auto backup = load(suffixed(path_, L".bak"))) {
        entries_ = std::move(*backup);
        recovery_notice_ = true;
        version_ = 1; // Repair using validated data, never promote an unfinished temporary file.
    } else
        failed_ = exists_or_inaccessible(path_) || exists_or_inaccessible(suffixed(path_, L".bak"));
    worker_ = std::thread([this] { run(); });
}
FrequencyStore::~FrequencyStore() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable())
        worker_.join();
}
void FrequencyStore::learn(const std::vector<LearningSpan> &spans) {
    if (spans.empty())
        return;
    std::lock_guard lock(mutex_);
    int day = today();
    for (auto &s : spans) {
        auto found = entries_.find(s.word);
        if (found == entries_.end() && entries_.size() >= 10000) {
            auto worst = std::min_element(entries_.begin(), entries_.end(), [day](auto &a, auto &b) {
                return score(a.second, day) < score(b.second, day);
            });
            entries_.erase(worst);
        }
        auto &e = entries_[s.word];
        e = {std::min(10000.0, score(e, day) + std::clamp(s.weight, 1u, 3u)), day};
    }
    ++version_;
    cv_.notify_all();
}
std::vector<std::pair<WordKey, double>> FrequencyStore::matching(const std::string &input) const {
    std::vector<std::pair<WordKey, double>> result;
    result.reserve(14);
    std::lock_guard lock(mutex_);
    auto day = today();
    for (auto &[k, e] : entries_) {
        auto value = score(e, day);
        if (value >= 5.5 && !input.empty() && k.reading.front() == input.front() &&
            matches_reading(k.reading, input)) {
            auto at =
                std::find_if(result.begin(), result.end(), [value](auto &v) { return v.second < value; });
            if (result.size() < 14)
                result.insert(at, {k, value});
            else if (at != result.end()) {
                auto offset = at - result.begin();
                result.pop_back();
                result.insert(result.begin() + offset, {k, value});
            }
        }
    }
    return result;
}
bool FrequencyStore::save(const Json &snapshot, uint64_t version) {
    auto bytes = encrypt_snapshot(snapshot);
    if (!bytes)
        return false;
    std::lock_guard files(file_mutex_);
    {
        std::lock_guard lock(mutex_);
        if (version <= cleared_through_)
            return true;
    }
    auto temporary = suffixed(path_, L".tmp");
    auto backup = suffixed(path_, L".bak");
    auto backup_temporary = suffixed(path_, L".bak.tmp");
    bool ok = write_flushed(temporary, *bytes) && write_flushed(backup_temporary, *bytes);
    if (ok)
        ok = replace_file(temporary, path_);
    if (ok)
        ok = replace_file(backup_temporary, backup);
    bool cleaned = remove_file(temporary);
    if (!remove_file(backup_temporary))
        cleaned = false;
    ok = ok && cleaned;
    return ok;
}
void FrequencyStore::run() {
    std::unique_lock lock(mutex_);
    for (;;) {
        cv_.wait(lock, [&] { return stopping_ || version_ > attempted_; });
        if (stopping_ && version_ <= attempted_)
            break;
        // One bounded debounce; constant typing cannot postpone saving indefinitely.
        if (!stopping_)
            cv_.wait_for(lock, std::chrono::milliseconds(300));
        auto version = version_;
        Json words = Json::array();
        for (auto &[k, e] : entries_)
            words.push_back(Json::array({k.text, k.reading, e.weight, e.day}));
        writing_ = true;
        lock.unlock();
        bool ok = false;
        try {
            ok = save(Json{{"v", 1}, {"words", std::move(words)}}, version);
        } catch (...) {
        }
        lock.lock();
        writing_ = false;
        attempted_ = std::max(attempted_, version);
        if (version > cleared_through_) {
            if (ok)
                saved_ = std::max(saved_, version);
            failed_ = !ok;
        }
        cv_.notify_all();
    }
}
bool FrequencyStore::flush() {
    std::unique_lock lock(mutex_);
    auto target = version_;
    cv_.notify_all();
    cv_.wait_for(lock, std::chrono::seconds(5), [&] { return attempted_ >= target && !writing_; });
    return saved_ >= target;
}
bool FrequencyStore::clear() {
    // Same lock order as save(): files, then state. No older writer can finish after this operation.
    std::lock_guard files(file_mutex_);
    uint64_t target;
    {
        std::lock_guard lock(mutex_);
        entries_.clear();
        target = ++version_;
        cleared_through_ = target;
        recovery_notice_ = false;
    }
    bool ok = true;
    for (auto suffix : {L".bak", L".bak.tmp", L".tmp"})
        if (!remove_file(suffixed(path_, suffix)))
            ok = false;
    // Keep a valid empty primary so older versions also see the cleared state.
    auto temporary = suffixed(path_, L".tmp");
    try {
        auto bytes = encrypt_snapshot(Json{{"v", 1}, {"words", Json::array()}});
        if (!bytes || !write_flushed(temporary, *bytes) || !replace_file(temporary, path_))
            ok = false;
    } catch (...) {
        ok = false;
    }
    if (!remove_file(temporary))
        ok = false;
    {
        std::lock_guard lock(mutex_);
        attempted_ = std::max(attempted_, target);
        if (ok)
            saved_ = std::max(saved_, target);
        failed_ = !ok;
    }
    cv_.notify_all();
    return ok;
}
size_t FrequencyStore::size() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}
bool FrequencyStore::failed() const {
    std::lock_guard lock(mutex_);
    return failed_;
}
bool FrequencyStore::take_recovery_notice() {
    std::lock_guard lock(mutex_);
    bool result = recovery_notice_;
    recovery_notice_ = false;
    return result;
}
} // namespace es
