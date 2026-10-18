#pragma once
#include "common.hpp"
#include "frequency.hpp"

namespace es {
class InputBuffer {
  public:
    std::wstring text, preview, pinyin, english, notice;
    std::vector<std::wstring> candidates;
    std::vector<LearningSpan> learning_spans;
    void edit_learning(size_t start, size_t removed, size_t inserted);
    size_t caret = 0;
    uint64_t revision = 0, result_revision = UINT64_MAX;
    uint64_t display_revision = 0;
    bool has_display_revision = false;
    int selected = 0;
    int page = 0;
    bool last_page = true;
    bool explanation_loading = false;
    std::wstring explanation_error;
    bool committing = false, ascii = false, show_explanation = false;
    Json explanation;
    std::wstring source() const {
        return text.substr(0, caret) + preview + text.substr(caret);
    }
    bool pending() const {
        return !pinyin.empty();
    }
    bool active() const {
        return !text.empty() || pending();
    }
    bool can_english() const {
        return !committing && !pending() && !english.empty() && revision == result_revision;
    }
    void changed(const std::wstring &before);
    bool insert(const std::wstring &value);
    void backspace();
    void erase();
    void move(int direction);
    void set_preedit(std::wstring input, std::wstring candidate, std::vector<std::wstring> list, int index);
    void clear();
    bool accept_result(uint64_t version, const std::wstring &result, bool complete);
};

// Every network result has an independent session + content version.
struct AiResult {
    std::string session;
    uint64_t revision = 0;
    bool explanation = false, complete = false;
    std::string text, error;
    Json items;
    double elapsed_ms = 0;
    uint64_t request_id = 0;
};
bool valid_translation(const std::string &text);
bool valid_explanation(const Json &body);
bool normalize_explanation(Json &body);
bool decode_explanation_response(const std::string &response, Json &items, std::string &error);
class SseParser {
    std::string pending_;

  public:
    std::string text;
    bool stopped = false, done = false, truncated = false;
    size_t bytes = 0;
    bool push(std::string_view chunk, const std::function<void(const std::string &)> &on_text);
};
} // namespace es
