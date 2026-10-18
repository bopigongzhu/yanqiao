#pragma once
#include "core.hpp"
#include "rime_api.h"
#include <array>
namespace es {
class RimeEngine {
    HMODULE library_ = nullptr;
    RimeApi *api_ = nullptr;
    RimeSessionId session_ = 0;
    std::string shared_, user_, prebuilt_;
    ReadingIndex readings_;
    FrequencyStore *frequency_ = nullptr;
    struct Candidate {
        std::string text;
        int index = -1;
        std::optional<WordKey> word;
        double score = 0;
        int match_class = 0;
    };
    struct Reference {
        int native = -1, promoted = -1;
    };
    struct Page {
        size_t number = 0, count = 0;
        std::array<int, 7> native{};
        int next = 0;
        bool exhausted = false;
    };
    struct Bookmark {
        size_t offset = 0;
        int native = 0;
    };
    // Fixed-size native references, never a growing cache of candidate strings.
    std::vector<Reference> prefix_;
    std::vector<WordKey> promoted_;
    std::deque<Page> pages_;
    std::deque<Bookmark> bookmarks_;
    std::string raw_, injected_commit_;
    std::optional<WordKey> chosen_;
    unsigned chosen_weight_ = 1;
    int page_ = 0, selected_ = 0, tail_start_ = 0;
    bool dirty_ = true, prefix_exhausted_ = false, personal_active_ = false, index_ready_ = false;
    void reset_cache();
    void rebuild();
    Page &tail_page(size_t number);
    void bookmark(size_t offset, int native);
    std::optional<Reference> reference(size_t position);
    std::optional<Candidate> read(Reference ref);
    void append_commit(InputBuffer &, const std::wstring &);

  public:
    ~RimeEngine();
    bool initialize(const std::filesystem::path &root, bool deploy = false);
    bool ready() const {
        return session_ != 0;
    }
    std::wstring error;
    bool key(int code, int modifiers = 0);
    void clear();
    void commit();
    void select(int index, bool explicit_choice = true);
    void set_frequency(FrequencyStore *store) {
        frequency_ = store;
        reset_cache();
        dirty_ = true;
        page_ = selected_ = 0;
    }
    bool learning_ready() const {
        return index_ready_;
    }
    struct CacheStats {
        size_t references, pages, bookmarks, text_bytes;
    };
    CacheStats cache_stats() const;
    void sync(InputBuffer &buffer);
};
} // namespace es
