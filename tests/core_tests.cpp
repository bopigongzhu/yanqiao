#include "core.hpp"
#include "rime_engine.hpp"
#include <iostream>
#include <psapi.h>
using namespace es;
int main() {
    int passed = 0;
    auto check = [&](bool ok, const char *name) {
        if (!ok)
            throw std::runtime_error(name);
        ++passed;
    };
    try {
        InputBuffer b;
        check(b.insert(L"你好"), "insert");
        check(b.text == L"你好" && b.caret == 2, "Chinese buffer");
        b.move(-1);
        b.insert(L"真的");
        check(b.text == L"你真的好", "cursor insertion");
        b.backspace();
        check(b.text == L"你真好", "backspace");
        b.erase();
        check(b.text == L"你真", "delete");
        b.clear();
        b.insert(L"A\U0001F600B");
        check(character_count(b.text) == 3, "Unicode count");
        b.move(-1);
        b.backspace();
        check(b.text == L"AB" && b.caret == 1, "surrogate-safe delete");
        b.clear();
        b.insert(std::wstring(300, L'中'));
        check(!b.insert(L"文") && b.text.size() == 300, "source bound");
        b.clear();
        b.insert(L"我累了");
        auto old = b.revision;
        b.accept_result(old, L"I'm tired.", true);
        check(b.can_english(), "current translation commits");
        b.insert(L"。");
        check(!b.can_english(), "source edit invalidates result");
        b.accept_result(old, L"I'm tired.", true);
        check(!b.can_english(), "stale result never commits");
        b.accept_result(b.revision, L"I'm tired.", true);
        b.accept_result(b.revision, L"I'm", false);
        check(!b.can_english(), "partial output cannot inherit prior commit permission");
        b.accept_result(b.revision, L"I'm tired.", true);
        check(!b.accept_result(old, L"old", true) && b.english == L"I'm tired.",
              "late older output cannot overwrite a newer complete result");
        b.set_preedit(L"ni", L"你", {L"你"}, 0);
        check(!b.can_english(), "unconfirmed candidate blocks English");
        b.clear();
        check(b.text.empty() && b.english.empty() && b.explanation.is_null(), "session clearing");
        SseParser s;
        std::string wire = "data: {\"choices\":[{\"delta\":{\"content\":\"I'm "
                           "tired.\"},\"finish_reason\":null}]}\r\n\r\ndata: "
                           "{\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n";
        for (char c : wire)
            check(s.push(std::string_view(&c, 1), [](auto &) {}), "fragmented SSE");
        check(s.text == "I'm tired." && s.stopped && s.done, "complete SSE");
        SseParser truncated;
        check(truncated.push("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"length\"}]}\n",
                             [](auto &) {}),
              "truncation parse");
        check(truncated.truncated && !truncated.stopped, "truncation rejected");
        SseParser malformed;
        check(!malformed.push("data: not JSON\n", [](auto &) {}), "malformed SSE");
        check(!valid_translation("```x```"), "code output rejected");
        check(!valid_translation("中文翻译"), "wrong-language result rejected");
        check(valid_translation("Can we take a rain check?"), "plain English accepted");
        Json explanation = {{"items", Json::array({Json{{"expression", "I'm beat"},
                                                        {"kind", "phrase"},
                                                        {"meaning_zh", "累坏了"},
                                                        {"example_en", nullptr},
                                                        {"example_zh", nullptr}}})}};
        check(valid_explanation(explanation), "explanation valid");
        explanation["items"][0]["example_en"] = "Hi";
        check(!valid_explanation(explanation), "bilingual pair required");
        check(!valid_explanation(Json{{"items", Json::array()}}), "empty explanation rejected");
        auto valid_item = Json{{"expression", "take a break"},
                               {"kind", "phrase"},
                               {"meaning_zh", "休息一下"},
                               {"example_en", "Let's take a break."},
                               {"example_zh", "我们休息一下吧。"}};
        auto extra_examples = Json{{"items", Json::array({valid_item, valid_item})}};
        check(normalize_explanation(extra_examples) && extra_examples["items"][1]["example_en"].is_null(),
              "extra complete examples safely reduced");
        auto wrong_type = Json{{"items", Json::array({valid_item})}};
        wrong_type["items"][0]["example_en"] = 123;
        check(!normalize_explanation(wrong_type), "bad optional field type rejected");
        Json decoded;
        std::string decode_error;
        auto response =
            Json{{"choices", Json::array({Json{{"finish_reason", "stop"},
                                               {"message", {{"content", extra_examples.dump()}}}}})}};
        check(decode_explanation_response(response.dump(), decoded, decode_error),
              "complete real-shaped explanation response");
        response["choices"][0]["finish_reason"] = "length";
        check(!decode_explanation_response(response.dump(), decoded, decode_error) && decoded.is_null() &&
                  decode_error.find("截断") != std::string::npos,
              "truncation distinct and never shown as complete");
        check(!decode_explanation_response("invalid", decoded, decode_error) &&
                  decode_error.find("解析") != std::string::npos,
              "malformed JSON distinct error");
        response["choices"][0]["finish_reason"] = "stop";
        response["choices"][0]["message"]["content"] = "```json\n" + extra_examples.dump() + "\n```";
        check(decode_explanation_response(response.dump(), decoded, decode_error),
              "complete outer fence normalized");
        RimeEngine r;
        check(r.initialize(module_dir()), "real Rime initializes from prebuilt schema");
        b.clear();
        r.key('n');
        r.key('i');
        r.sync(b);
        check(b.pending() && !b.candidates.empty(), "real full-pinyin candidates");
        check(b.text.empty(), "candidate does not commit to sentence");
        auto first = b.candidates.front();
        auto source_before_confirmation = b.source();
        auto revision_before_confirmation = b.revision;
        r.select(0);
        r.sync(b);
        check(b.text == first && !b.pending(), "selected real candidate enters sentence buffer");
        check(b.source() != source_before_confirmation || b.revision == revision_before_confirmation,
              "confirming same preview preserves content revision");
        r.clear();
        b.clear();
        for (char c : std::string("wojintian"))
            r.key(c);
        r.sync(b);
        check(!b.preview.empty(), "multi-syllable pinyin");
        auto type_pinyin = [&](const std::string &input) {
            r.clear();
            b.clear();
            for (char c : input)
                r.key(c);
            r.sync(b);
        };
        type_pinyin("ta");
        check(b.candidates.size() == 7 && b.page == 0 && !b.last_page, "seven candidates on first ta page");
        auto page_one = b.candidates;
        r.key(0xff56);
        r.sync(b);
        check(b.page == 1 && b.candidates != page_one, "real next page");
        r.key(0xff55);
        r.sync(b);
        check(b.page == 0 && b.candidates == page_one, "real previous page");
        auto seventh = b.candidates[6];
        r.select(6);
        r.sync(b);
        check(b.text == seventh && !b.pending(), "seventh candidate selection");
        for (auto input : {"mingzi", "mz", "mingz", "mzi"}) {
            type_pinyin(input);
            bool found = false;
            for (int page = 0; page < 50; ++page) {
                for (auto &candidate : b.candidates)
                    if (candidate == L"名字")
                        found = true;
                if (found || b.last_page)
                    break;
                r.key(0xff56);
                r.sync(b);
            }
            check(found, input);
        }
        for (auto input : {"zg", "zhg", "zhongg"}) {
            type_pinyin(input);
            bool found = false;
            for (int page = 0; page < 50; ++page) {
                for (auto &candidate : b.candidates)
                    if (candidate == L"中国")
                        found = true;
                if (found || b.last_page)
                    break;
                r.key(0xff56);
                r.sync(b);
            }
            check(found, input);
        }
        r.clear();
        b.clear();
        const auto start = Clock::now();
        for (int i = 0; i < 200; i++) {
            for (char c : std::string("nihao"))
                r.key(c);
            r.sync(b);
            r.select(0);
            r.sync(b);
            r.clear();
            b.clear();
        }
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                             sizeof(counters));
        Json report = {{"checks", passed},
                       {"rime", "1.17.0"},
                       {"cycles", 200},
                       {"average_cycle_ms", ms / 200},
                       {"private_MiB", counters.PrivateUsage / 1048576.0},
                       {"working_set_MiB", counters.WorkingSetSize / 1048576.0},
                       {"scope", "Unit-test process including real Rime, not whole installed IME"}};
        std::cout << report.dump() << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAILED: " << e.what() << " after " << passed << " checks\n";
        return 1;
    }
}
