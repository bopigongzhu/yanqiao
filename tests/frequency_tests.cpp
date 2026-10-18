#include "rime_engine.hpp"
#include <iostream>
#include <fstream>
#include <psapi.h>
using namespace es;
int main() {
    int checks = 0;
    auto check = [&](bool ok, const char *message) {
        if (!ok)
            throw std::runtime_error(message);
        ++checks;
    };
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    auto path =
        std::filesystem::path(tmp) / (L"EShuRu-frequency-test-" + std::to_wstring(GetCurrentProcessId()) +
                                      L"-" + std::to_wstring(now_ms()) + L".dat");
    auto cleanup = [&] {
        for (auto suffix : {L"", L".bak", L".tmp", L".bak.tmp"}) {
            auto file = path;
            file += suffix;
            DeleteFileW(file.c_str());
        }
    };
    try {
        for (auto input : {"mingzi", "mz", "mingz", "mzi", "ming'zi"})
            check(matches_reading("ming zi", input), input);
        check(matches_reading("zhong guo", "zhg"), "digraph abbreviation");
        check(!matches_reading("ming zi", "ming"), "partial reading not learned");
        check(!matches_reading("xing", "hang"), "pronunciation remains distinct");
        for (auto [reading, input] : {std::pair{"lue", "lve"},
                                      {"nue", "nve"},
                                      {"ju", "jv"},
                                      {"juan", "jvan"},
                                      {"que", "qve"},
                                      {"xue", "xve"},
                                      {"yuan", "yvan"}})
            check(match_reading(reading, input) == ReadingMatch::Full, "schema alias is a complete syllable");
        check(match_reading("lue ming", "lvem") == ReadingMatch::Abbreviated, "alias with mixed initials");
        for (auto [reading, input] : {std::pair{"lu", "lv"},
                                      {"lv", "lu"},
                                      {"nu", "nv"},
                                      {"nv", "nu"},
                                      {"juan", "jv"},
                                      {"gu", "gv"},
                                      {"ming zi", "ming''zi"},
                                      {"ming zi", "'mingzi"},
                                      {"ming zi", "mingzi'"},
                                      {"nue", "nv"}})
            check(!matches_reading(reading, input), "unsupported spelling cannot borrow a pronunciation");
        ReadingIndex index;
        check(index.open(module_dir() / L"data/word-readings.idx"), "mapped lexicon index");
        auto name = index.identify(utf8(L"名字"), "mz");
        check(name && name->reading == "ming zi", "canonical reading from abbreviation");
        check(!index.identify(utf8(L"不存在的测试句子"), "bczdcsjz"), "generated sentence not in lexicon");
        check(!index.identify(utf8(L"绿"), "l"), "ambiguous lu/lv initial is not assigned a reading");
        check(index.identify(utf8(L"行"), "hang")->reading == "hang" &&
                  index.identify(utf8(L"行"), "xing")->reading == "xing",
              "actual polyphonic dictionary readings remain separate");
        {
            FrequencyStore store(path);
            RimeEngine r;
            check(r.initialize(module_dir()), "Rime initializes");
            r.set_frequency(&store);
            InputBuffer b;
            auto type = [&](std::string input) {
                r.clear();
                b.clear();
                for (char c : input)
                    r.key(c);
                r.sync(b);
            };
            type("ta");
            check(b.candidates.size() == 7, "personalized page retains seven candidates");
            auto original = b.candidates;
            for (int iteration = 0; iteration < 3; ++iteration) {
                type("ta");
                auto found = std::find(b.candidates.begin(), b.candidates.end(), L"她");
                check(found != b.candidates.end(), "find target word");
                r.select(int(found - b.candidates.begin()));
                r.sync(b);
                check(b.text == L"她" && b.learning_spans.size() == 1,
                      "selected identity creates one pending span");
                check(store.size() == (iteration ? 1 : 0), "selection itself never persists frequency");
                store.learn(b.learning_spans); // same operation as a successful host acknowledgement
                if (!iteration) {
                    type("ta");
                    check(b.candidates == original, "one accidental choice does not reorder");
                }
            }
            type("ta");
            check(b.candidates[0] == L"她", "frequent non-default candidate promoted");
            r.key(' ');
            r.sync(b);
            check(b.text == L"她", "space chooses displayed promoted candidate");
            type("ta");
            r.commit();
            r.sync(b);
            check(b.text == L"她", "Enter-style commit uses displayed candidate");
            type("ta");
            r.key(0xff56);
            r.sync(b);
            check(b.page == 1, "personalized paging advances");
            auto later = b.candidates.back();
            r.select(int(b.candidates.size() - 1));
            r.sync(b);
            check(b.text == later, "later page candidate identity preserved");
            auto pending = b.learning_spans;
            check(!pending.empty(), "later page word has a known reading");
            for (int i = 0; i < 20; ++i)
                store.learn(pending);
            type("ta");
            check(std::find(b.candidates.begin(), b.candidates.end(), later) != b.candidates.end(),
                  "later page word promoted to first page");
            auto at = std::find(b.candidates.begin(), b.candidates.end(), later);
            r.select(int(at - b.candidates.begin()));
            r.sync(b);
            check(b.text == later, "promoted word inserts exact text");
            b.backspace();
            check(b.learning_spans.empty(), "deleted word loses pending learning");
            type("mingzi");
            auto it = std::find(b.candidates.begin(), b.candidates.end(), L"名字");
            check(it != b.candidates.end(), "find phrase");
            r.select(int(it - b.candidates.begin()));
            r.sync(b);
            check(b.learning_spans.size() == 1, "phrase span created");
            b.move(-1);
            b.insert(L"的");
            check(b.learning_spans.empty(), "editing inside a word invalidates learning");
            type("mz");
            r.select(0);
            r.sync(b);
            check(!b.learning_spans.empty(), "abbreviated selection identified");
            auto learned = b.learning_spans;
            store.learn(learned);
            store.learn(learned);
            check(!store.matching("mingzi").empty() && !store.matching("mingz").empty(),
                  "full and mixed input share frequency");
            auto find_word = [&](const wchar_t *word) {
                for (int page = 0; page < 1000; ++page) {
                    auto at = std::find(b.candidates.begin(), b.candidates.end(), word);
                    if (at != b.candidates.end())
                        return int(at - b.candidates.begin());
                    if (b.last_page)
                        break;
                    r.key(0xff56);
                    r.sync(b);
                }
                return -1;
            };
            for (auto [input, text, canonical] : {std::tuple{"lve", L"略", "lue"},
                                                  {"nve", L"虐", "nue"},
                                                  {"jv", L"居", "ju"},
                                                  {"jvan", L"卷", "juan"},
                                                  {"xve", L"学", "xue"},
                                                  {"yv", L"鱼", "yu"},
                                                  {"nv", L"女", "nv"},
                                                  {"lv", L"绿", "lv"}}) {
                type(input);
                auto at = find_word(text);
                check(at >= 0, "real Rime accepts schema alternative spelling");
                r.select(at);
                r.sync(b);
                check(b.text == text && b.learning_spans.size() == 1 &&
                          b.learning_spans[0].word.reading == canonical,
                      "alternative spelling selection preserves canonical reading");
                store.learn(b.learning_spans);
                store.learn(b.learning_spans);
                auto has_word = [&](const std::string &spelling) {
                    auto matches = store.matching(spelling);
                    return std::any_of(matches.begin(), matches.end(), [&](auto &entry) {
                        return entry.first.text == utf8(text) && entry.first.reading == canonical;
                    });
                };
                check(has_word(input) && has_word(canonical),
                      "alias and canonical input share learned weight");
            }
            store.learn({{0, 1, {utf8(L"绿"), "lu"}, 3}});
            store.learn({{0, 1, {utf8(L"绿"), "lu"}, 3}});
            type("l");
            auto green = find_word(L"绿");
            check(green >= 0, "polyphonic word remains available through initial");
            r.select(green);
            r.sync(b);
            check(b.text == L"绿" && b.learning_spans.empty(),
                  "known full-reading weights do not cause ambiguous initials to learn a guessed reading");
            b.clear();
            check(b.learning_spans.empty(), "cancel clears all pending spans");
            check(store.flush(), "encrypted file flushed");
            std::ifstream f(path, std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(f)), {});
            check(bytes.find(utf8(L"名字")) == std::string::npos &&
                      bytes.find("ming zi") == std::string::npos,
                  "no plaintext words in stored file");
            r.set_frequency(nullptr);
            type("ta");
            check(b.candidates == original, "disabling learning restores base ranking");
        }
        {
            FrequencyStore restored(path);
            check(restored.size() >= 2, "frequency survives restart");
            check(!restored.matching("ta").empty(), "restored ranking weights usable");
            check(restored.clear() && restored.size() == 0, "clear removes personal weights");
        }
        {
            FrequencyStore empty(path);
            check(empty.size() == 0, "clear survives restart");
        }
        {
            std::ofstream broken(path, std::ios::binary | std::ios::trunc);
            broken << "invalid encrypted file";
        }
        {
            FrequencyStore broken(path);
            check(broken.failed() && broken.size() == 0,
                  "corrupt frequency fails closed without blocking input");
            check(broken.clear(), "corrupt frequency can be reset");
        }
        {
            FrequencyStore capacity(path);
            PROCESS_MEMORY_COUNTERS_EX before{}, after{};
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&before),
                                 sizeof(before));
            std::vector<LearningSpan> sample;
            for (int i = 0; i < 10000; ++i)
                sample.push_back({0, 1, {"synthetic-" + std::to_string(i), "ming zi"}, 3});
            capacity.learn(sample);
            capacity.learn(sample);
            sample.clear();
            sample.shrink_to_fit();
            check(capacity.size() == 10000 && capacity.flush(), "capacity snapshot saves");
            std::vector<double> times;
            for (int i = 0; i < 100; ++i) {
                auto start = Clock::now();
                auto found = capacity.matching(i % 2 ? "mz" : "mingzi");
                times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
                check(found.size() == 14, "capacity lookup remains bounded");
            }
            std::sort(times.begin(), times.end());
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&after),
                                 sizeof(after));
            std::cout << Json{{"capacity_entries", capacity.size()},
                              {"lookup_p95_ms", times[94]},
                              {"private_delta_MiB",
                               (double(after.PrivateUsage) - double(before.PrivateUsage)) / 1048576.0},
                              {"scope", "Synthetic worst-case same-reading table, includes save allocations; "
                                        "not host key latency"}}
                             .dump()
                      << std::endl;
            capacity.learn({{0, 1, {"overflow", "ta"}, 3}});
            check(capacity.size() == 10000, "overflow evicts an entry");
        }
        cleanup();
        std::cout << Json{{"checks", checks},
                          {"result", "passed"},
                          {"scope", "real Rime identity and paging, pending span edits, DPAPI persistence, "
                                    "clear, restart and corruption"}}
                         .dump()
                  << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAILED: " << e.what() << " after " << checks << " checks\n";
        cleanup();
        return 1;
    }
}
