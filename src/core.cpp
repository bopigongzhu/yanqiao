#include "core.hpp"
namespace es {
void InputBuffer::edit_learning(size_t start, size_t removed, size_t inserted) {
    for (auto it = learning_spans.begin(); it != learning_spans.end();) {
        size_t end = it->start + it->length;
        bool overlap =
            removed ? it->start < start + removed && end > start : it->start < start && end > start;
        if (overlap)
            it = learning_spans.erase(it);
        else {
            if (it->start >= start + removed)
                it->start = it->start - removed + inserted;
            ++it;
        }
    }
}
void InputBuffer::changed(const std::wstring &before) {
    if (before != source()) {
        ++revision;
        show_explanation = false;
        explanation = nullptr;
        explanation_loading = false;
        explanation_error.clear();
        notice.clear();
    }
}
bool InputBuffer::insert(const std::wstring &value) {
    if (committing)
        return false;
    if (character_count(text) + character_count(value) > 300) {
        notice = L"当前句子最多 300 字，请先输入或取消。";
        return false;
    }
    auto before = source();
    edit_learning(caret, 0, value.size());
    text.insert(caret, value);
    caret += value.size();
    changed(before);
    return true;
}
void InputBuffer::backspace() {
    if (committing || !caret)
        return;
    auto before = source();
    auto p = previous_character(text, caret);
    edit_learning(p, caret - p, 0);
    text.erase(p, caret - p);
    caret = p;
    changed(before);
}
void InputBuffer::erase() {
    if (committing || caret >= text.size())
        return;
    auto before = source();
    auto e = next_character(text, caret);
    edit_learning(caret, e - caret, 0);
    text.erase(caret, e - caret);
    changed(before);
}
void InputBuffer::move(int direction) {
    if (pending() || committing)
        return;
    caret = direction < 0 ? previous_character(text, caret) : next_character(text, caret);
}
void InputBuffer::set_preedit(std::wstring input, std::wstring candidate, std::vector<std::wstring> list,
                              int index) {
    auto before = source();
    pinyin = std::move(input);
    preview = std::move(candidate);
    candidates = std::move(list);
    selected = index;
    changed(before);
}
void InputBuffer::clear() {
    learning_spans.clear();
    text.clear();
    preview.clear();
    pinyin.clear();
    english.clear();
    notice.clear();
    candidates.clear();
    explanation = nullptr;
    explanation_loading = false;
    explanation_error.clear();
    page = 0;
    last_page = true;
    caret = 0;
    ++revision;
    result_revision = UINT64_MAX;
    display_revision = 0;
    has_display_revision = false;
    committing = false;
    show_explanation = false;
}
bool InputBuffer::accept_result(uint64_t version, const std::wstring &result, bool complete) {
    if (version > revision || (has_display_revision && version < display_revision))
        return false;
    english = result;
    display_revision = version;
    has_display_revision = true;
    result_revision = complete ? version : UINT64_MAX;
    return true;
}
bool valid_translation(const std::string &text) {
    if (text.empty() || text.size() > 16384 || text.find("```") != std::string::npos)
        return false;
    auto w = wide(text);
    if (w.empty())
        return false;
    for (wchar_t c : w)
        if (c >= 0x4e00 && c <= 0x9fff)
            return false;
    return text.find('\0') == std::string::npos;
}
bool normalize_explanation(Json &b) {
    if (!b.is_object() || !b.contains("items") || !b["items"].is_array())
        return false;
    bool example_kept = false;
    for (auto &item : b["items"]) {
        if (!item.is_object())
            return false;
        // Optional examples may be omitted or empty; never manufacture missing text.
        for (auto key : {"example_en", "example_zh"})
            if (!item.contains(key) || (item[key].is_string() && item[key].get<std::string>().empty()))
                item[key] = nullptr;
        if (!item["example_en"].is_null() && !item["example_zh"].is_null()) {
            if (!item["example_en"].is_string() || !item["example_zh"].is_string())
                return false;
            if (example_kept) {
                item["example_en"] = nullptr;
                item["example_zh"] = nullptr;
            }
            example_kept = true;
        }
    }
    return valid_explanation(b);
}
bool decode_explanation_response(const std::string &response, Json &items, std::string &error) {
    error.clear();
    items = nullptr;
    try {
        const auto outer = Json::parse(response);
        const auto &choice = outer.at("choices").at(0);
        auto reason = choice.value("finish_reason", "");
        if (reason != "stop") {
            error = reason == "length" ? "讲解输出被截断，请重新讲解。" : "讲解未正常完成，请重新讲解。";
            return false;
        }
        auto content = choice.at("message").at("content").get<std::string>();
        auto first = content.find_first_not_of(" \r\n\t");
        auto last = content.find_last_not_of(" \r\n\t");
        if (first == std::string::npos)
            throw std::runtime_error("empty");
        content = content.substr(first, last - first + 1);
        // Only remove a complete outer code fence, never extract arbitrary fragments.
        if (content.starts_with("```json\n") && content.ends_with("```"))
            content = content.substr(8, content.size() - 11);
        items = Json::parse(content);
        if (!normalize_explanation(items)) {
            items = nullptr;
            error = "讲解字段不符合要求，请重新讲解。";
            return false;
        }
        return true;
    } catch (...) {
        items = nullptr;
        error = "讲解返回格式无法解析，请重新讲解。";
        return false;
    }
}
bool valid_explanation(const Json &b) {
    if (!b.is_object() || !b.contains("items") || !b["items"].is_array() || b["items"].empty() ||
        b["items"].size() > 3)
        return false;
    int examples = 0;
    for (auto &i : b["items"]) {
        if (!i.is_object())
            return false;
        for (auto key : {"expression", "kind", "meaning_zh"})
            if (!i.contains(key) || !i[key].is_string() || i[key].get<std::string>().empty())
                return false;
        auto k = i["kind"].get<std::string>();
        if (k != "word" && k != "phrase" && k != "pattern")
            return false;
        if (character_count(wide(i["meaning_zh"])) > 100 || i["expression"].get<std::string>().size() > 256)
            return false;
        bool en = i.contains("example_en") && !i["example_en"].is_null(),
             zh = i.contains("example_zh") && !i["example_zh"].is_null();
        if (en != zh)
            return false;
        if (en) {
            if (!i["example_en"].is_string() || !i["example_zh"].is_string() ||
                i["example_en"].get<std::string>().size() > 512 ||
                i["example_zh"].get<std::string>().size() > 512 || ++examples > 1)
                return false;
        }
    }
    return true;
}
bool SseParser::push(std::string_view chunk, const std::function<void(const std::string &)> &on_text) {
    bytes += chunk.size();
    if (bytes > 65536)
        return false;
    pending_.append(chunk);
    size_t pos;
    while ((pos = pending_.find('\n')) != std::string::npos) {
        std::string line = pending_.substr(0, pos);
        pending_.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.starts_with("data:"))
            continue;
        auto data = line.substr(5);
        if (!data.empty() && data.front() == ' ')
            data.erase(0, 1);
        if (data == "[DONE]") {
            done = true;
            continue;
        }
        if (data.empty())
            continue;
        try {
            auto j = Json::parse(data);
            if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty())
                continue;
            auto &c = j["choices"][0];
            if (c.contains("delta") && c["delta"].contains("content") && c["delta"]["content"].is_string()) {
                text += c["delta"]["content"].get<std::string>();
                if (text.size() > 16384)
                    return false;
                on_text(text);
            }
            if (c.contains("finish_reason") && !c["finish_reason"].is_null()) {
                stopped = c["finish_reason"] == "stop";
                truncated = !stopped;
            }
        } catch (...) {
            return false;
        }
    }
    return true;
}
} // namespace es
