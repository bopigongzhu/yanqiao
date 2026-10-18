#include "rime_engine.hpp"
#include <fstream>
#include <iostream>
#include <map>
using namespace es;
int main() {
    int checks = 0;
    auto check = [&](bool ok, const char *message) {
        if (!ok)
            throw std::runtime_error(message);
        ++checks;
    };
    wchar_t temporary[MAX_PATH];
    GetTempPathW(MAX_PATH, temporary);
    auto path =
        std::filesystem::path(temporary) / (L"EShuRu-paging-" + std::to_wstring(GetCurrentProcessId()) +
                                            L"-" + std::to_wstring(now_ms()) + L".dat");
    auto cleanup = [&] {
        for (auto suffix : {L"", L".bak", L".tmp", L".bak.tmp"}) {
            auto file = path;
            file += suffix;
            DeleteFileW(file.c_str());
        }
    };
    int result = 0;
    try {
        FrequencyStore store(path);
        RimeEngine rime;
        ReadingIndex readings;
        InputBuffer buffer;
        check(rime.initialize(module_dir()) && readings.open(module_dir() / L"data/word-readings.idx"),
              "real engine and readings initialized");
        auto type = [&](const std::string &input) {
            rime.clear();
            buffer.clear();
            for (auto c : input)
                rime.key(c);
            rime.sync(buffer);
        };
        auto page = [&](int key) {
            rime.key(key);
            rime.sync(buffer);
        };
        RimeEngine::CacheStats maximum{};
        auto bounded = [&] {
            auto stats = rime.cache_stats();
            check(stats.references <= 77 && stats.pages <= 5 && stats.bookmarks <= 32 &&
                      stats.text_bytes <= 14 * (64 + 128),
                  "candidate references, reconstruction points and cached text stay bounded");
            maximum.references = std::max(maximum.references, stats.references);
            maximum.pages = std::max(maximum.pages, stats.pages);
            maximum.bookmarks = std::max(maximum.bookmarks, stats.bookmarks);
            maximum.text_bytes = std::max(maximum.text_bytes, stats.text_bytes);
        };
        // The unpersonalized Rime selector is an independent oracle for pagination and end-of-list.
        type("s");
        std::vector<std::vector<std::wstring>> expected;
        for (;;) {
            check(!buffer.candidates.empty() && buffer.page == int(expected.size()), "native page exists");
            expected.push_back(buffer.candidates);
            if (buffer.last_page)
                break;
            check(expected.size() < 4096, "native enumeration finishes within test limit");
            page(0xff56);
        }
        check(expected.size() > 40, "many pages exercise eviction and reconstruction");
        rime.set_frequency(&store);
        type("s");
        std::vector<double> times;
        for (size_t i = 0; i < expected.size(); ++i) {
            check(buffer.page == int(i) && buffer.candidates == expected[i], "forward page matches Rime");
            check(buffer.last_page == (i + 1 == expected.size()), "last page agrees with Rime");
            bounded();
            auto start = Clock::now();
            page(0xff56);
            times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        }
        check(buffer.page == int(expected.size() - 1) && buffer.candidates == expected.back(),
              "next on last page stays put");
        for (size_t i = expected.size(); i-- > 0;) {
            check(buffer.page == int(i) && buffer.candidates == expected[i],
                  "evicted backward page reconstructs original sequence");
            bounded();
            auto start = Clock::now();
            page(0xff55);
            times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        }
        check(buffer.page == 0 && buffer.candidates == expected[0], "previous on first page stays put");
        size_t middle = expected.size() / 2;
        for (size_t i = 0; i < middle; ++i)
            page(0xff56);
        auto selected = buffer.candidates.back();
        rime.select(int(buffer.candidates.size() - 1));
        rime.sync(buffer);
        check(buffer.text == selected && !buffer.pending(),
              "selection after eviction inserts displayed text");

        std::map<std::wstring, int> counts;
        for (auto &p : expected)
            for (auto &text : p)
                ++counts[text];
        std::optional<WordKey> learned;
        for (size_t p = 8; p < expected.size() && !learned; ++p)
            for (auto &text : expected[p])
                if (counts[text] == 1 && (learned = readings.identify(utf8(text), "s")))
                    break;
        check(learned.has_value(), "unique word from a distant page found");
        for (int i = 0; i < 30; ++i)
            store.learn({{0, 1, *learned, 3}});
        type("s");
        std::vector<std::vector<std::wstring>> promoted_pages;
        std::map<std::wstring, int> promoted_counts;
        for (;;) {
            promoted_pages.push_back(buffer.candidates);
            for (auto &text : buffer.candidates)
                ++promoted_counts[text];
            bounded();
            if (buffer.last_page)
                break;
            check(promoted_pages.size() < 4096, "promoted enumeration finishes");
            page(0xff56);
        }
        check(promoted_counts == counts, "promoting a distant word loses or duplicates no native candidate");
        check(promoted_pages.size() == expected.size(), "promoted page count remains complete");
        for (size_t i = promoted_pages.size(); i-- > 0;) {
            check(buffer.candidates == promoted_pages[i], "promoted sequence survives backward eviction");
            bounded();
            if (i)
                page(0xff55);
        }
        bool chosen = false;
        for (int p = 0; p < 6 && !chosen; ++p) {
            auto at = std::find(buffer.candidates.begin(), buffer.candidates.end(), wide(learned->text));
            if (at != buffer.candidates.end()) {
                rime.select(int(at - buffer.candidates.begin()));
                rime.sync(buffer);
                check(buffer.text == wide(learned->text) && buffer.learning_spans.size() == 1 &&
                          buffer.learning_spans[0].word == *learned,
                      "injected candidate retains its complete word and reading identity");
                chosen = true;
            } else
                page(0xff56);
        }
        check(chosen, "distant learned word appears in bounded reordered prefix");
        type("s");
        check(store.clear(), "clear during active composition persists");
        rime.set_frequency(&store);
        rime.sync(buffer);
        check(buffer.candidates == expected[0] && buffer.learning_spans.empty(),
              "clear immediately restores base candidates for current composition");
        auto first = buffer.candidates[0];
        rime.select(0);
        rime.sync(buffer);
        check(buffer.text == first, "selection after clearing uses refreshed candidate identity");
        rime.clear();
        auto empty = rime.cache_stats();
        check(empty.references == 0 && empty.text_bytes == 0 && empty.bookmarks == 0,
              "ending composition releases all cached identities and text");
        std::sort(times.begin(), times.end());
        Json report{{"checks", checks},
                    {"pages", expected.size()},
                    {"page_operation_p95_ms", times[(times.size() - 1) * 95 / 100]},
                    {"page_operation_max_ms", times.back()},
                    {"max_cached_references", maximum.references},
                    {"max_cached_pages", maximum.pages},
                    {"max_cached_bookmarks", maximum.bookmarks},
                    {"max_cached_text_bytes", maximum.text_bytes},
                    {"scope",
                     "Real Rime baseline versus bounded personalized pagination; engine timings only, "
                     "not host key-to-paint latency or whole-process memory."}};
        std::ofstream(module_dir() / L"paging-report.json") << report.dump(2);
        std::cout << report.dump() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "FAILED: " << e.what() << " after " << checks << " checks\n";
        result = 1;
    }
    cleanup();
    return result;
}
