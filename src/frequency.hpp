#pragma once
#include "common.hpp"
#include <map>
#include <tuple>
namespace es {
struct WordKey {
    std::string text, reading;
    bool operator==(const WordKey &) const = default;
    bool operator<(const WordKey &b) const {
        return std::tie(text, reading) < std::tie(b.text, b.reading);
    }
};
struct LearningSpan {
    size_t start = 0, length = 0;
    WordKey word;
    unsigned weight = 1;
};
// Exact full/initial/mixed spelling only. No completion guesses or tone inference.
enum class ReadingMatch { None, Abbreviated, Full };
ReadingMatch match_reading(std::string_view reading, std::string_view input);
bool matches_reading(std::string_view reading, std::string_view input);
class ReadingIndex {
    Handle file_, mapping_;
    const char *data_ = nullptr;
    size_t size_ = 0;
    uint32_t count_ = 0;
    std::string_view row(uint32_t i) const;

  public:
    ~ReadingIndex();
    bool open(const std::filesystem::path &path);
    std::optional<WordKey> identify(const std::string &text, const std::string &input) const;
    bool contains(const WordKey &key) const;
};
class FrequencyStore {
    struct Entry {
        double weight = 0;
        int day = 0;
    };
    using Entries = std::map<WordKey, Entry>;
    Entries entries_;
    std::filesystem::path path_;
    mutable std::mutex mutex_;
    std::mutex file_mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    bool stopping_ = false, writing_ = false, failed_ = false, recovery_notice_ = false;
    uint64_t version_ = 0, saved_ = 0, attempted_ = 0, cleared_through_ = 0;
    static int today();
    static double score(const Entry &, int day);
    static std::optional<Entries> load(const std::filesystem::path &path);
    bool save(const Json &snapshot, uint64_t version);
    void run();

  public:
    explicit FrequencyStore(std::filesystem::path path);
    ~FrequencyStore();
    void learn(const std::vector<LearningSpan> &spans);
    std::vector<std::pair<WordKey, double>> matching(const std::string &input) const;
    bool clear();
    bool flush();
    size_t size() const;
    bool failed() const;
    bool take_recovery_notice();
};
} // namespace es
