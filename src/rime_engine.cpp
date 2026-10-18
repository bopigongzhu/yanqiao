#include "rime_engine.hpp"
#include <cmath>
#include <cstring>
namespace es {
RimeEngine::~RimeEngine() {
    if (api_) {
        if (session_)
            api_->destroy_session(session_);
        api_->finalize();
    }
    if (library_)
        FreeLibrary(library_);
}
bool RimeEngine::initialize(const std::filesystem::path &root, bool deploy) {
    library_ = LoadLibraryExW((root / L"rime.dll").c_str(), nullptr,
                              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!library_) {
        error = L"无法加载全拼引擎：" + win_error();
        return false;
    }
    auto get = reinterpret_cast<RimeApi *(*)()>(GetProcAddress(library_, "rime_get_api"));
    if (!get) {
        error = L"拼音引擎版本不兼容";
        return false;
    }
    api_ = get();
    shared_ = utf8((root / L"data").wstring());
    prebuilt_ = utf8((root / L"data/build").wstring());
    // This directory holds schema metadata, never learned words. User dictionary is disabled.
    auto user_path = deploy ? root / L"data" : config_dir() / L"rime";
    std::filesystem::create_directories(user_path);
    user_ = utf8(user_path.wstring());
    RIME_STRUCT(RimeTraits, t);
    t.shared_data_dir = shared_.c_str();
    t.user_data_dir = user_.c_str();
    t.prebuilt_data_dir = prebuilt_.c_str();
    t.staging_dir = deploy ? prebuilt_.c_str() : nullptr;
    t.distribution_name = "EShuRu";
    t.distribution_code_name = "e_shuru";
    t.distribution_version = "0.2.5";
    t.app_name = "rime.e_shuru";
    t.min_log_level = 3;
    t.log_dir = "";
    api_->setup(&t);
    api_->initialize(&t);
    if (deploy) {
        api_->deployer_initialize(&t);
        std::filesystem::create_directories(root / L"data/build");
        bool ok = api_->deploy_schema(utf8((root / L"data/e_shuru.schema.yaml").wstring()).c_str());
        if (!ok)
            error = L"全拼词典编译失败";
        return ok;
    }
    session_ = api_->create_session();
    if (!session_ || !api_->select_schema(session_, "e_shuru")) {
        error = L"缺少已编译的全拼词典，请重新运行构建或修复安装。";
        return false;
    }
    api_->set_option(session_, "ascii_mode", False);
    index_ready_ = readings_.open(root / L"data/word-readings.idx");
    return true;
}
bool RimeEngine::key(int code, int modifiers) {
    if (personal_active_ && !modifiers) {
        if (code == ' ') {
            select(selected_, false);
            return true;
        }
        if (code == 0xff55) {
            if (page_ > 0)
                --page_;
            selected_ = 0;
            return true;
        }
        if (code == 0xff56) {
            if (reference(size_t(page_ + 1) * 7))
                ++page_;
            selected_ = 0;
            return true;
        }
        if (code == 0xff52) {
            selected_ = std::max(0, selected_ - 1);
            return true;
        }
        if (code == 0xff54) {
            if (selected_ < 6 && reference(size_t(page_) * 7 + selected_ + 1))
                ++selected_;
            return true;
        }
    }
    dirty_ = true;
    page_ = selected_ = 0;
    chosen_.reset();
    return session_ && api_->process_key(session_, code, modifiers);
}
void RimeEngine::reset_cache() {
    prefix_.clear();
    promoted_.clear();
    pages_.clear();
    bookmarks_.clear();
    raw_.clear();
    tail_start_ = 0;
    prefix_exhausted_ = false;
}
RimeEngine::CacheStats RimeEngine::cache_stats() const {
    CacheStats stats{prefix_.size(), pages_.size(), bookmarks_.size(), 0};
    for (auto &page : pages_)
        stats.references += page.count;
    for (auto &word : promoted_)
        stats.text_bytes += word.text.size() + word.reading.size();
    return stats;
}
void RimeEngine::clear() {
    reset_cache();
    chosen_.reset();
    injected_commit_.clear();
    personal_active_ = false;
    dirty_ = true;
    page_ = selected_ = 0;
    if (session_)
        api_->clear_composition(session_);
}
void RimeEngine::commit() {
    if (personal_active_)
        select(selected_, false);
    if (session_)
        api_->commit_composition(session_);
}
void RimeEngine::select(int index, bool explicit_choice) {
    if (personal_active_) {
        size_t position = size_t(page_) * 7 + size_t(index);
        if (index < 0 || index >= 7)
            return;
        auto ref = reference(position);
        auto loaded = ref ? read(*ref) : std::nullopt;
        if (!loaded)
            return;
        auto &choice = *loaded;
        chosen_ = choice.word;
        chosen_weight_ = explicit_choice || selected_ != 0 || page_ != 0 ? 3 : 1;
        if (choice.index < 0) {
            api_->clear_composition(session_);
            injected_commit_ = choice.text;
        } else if (!api_->select_candidate(session_, size_t(choice.index))) {
            chosen_.reset();
            return;
        }
        dirty_ = true;
        page_ = selected_ = 0;
        personal_active_ = false;
        return;
    }
    if (session_)
        api_->select_candidate_on_current_page(session_, size_t(index));
}
std::optional<RimeEngine::Candidate> RimeEngine::read(Reference ref) {
    if (ref.promoted >= 0 && size_t(ref.promoted) < promoted_.size()) {
        auto &word = promoted_[size_t(ref.promoted)];
        return Candidate{word.text, -1, word};
    }
    if (ref.native < 0)
        return {};
    RimeCandidateListIterator iterator{};
    if (!api_->candidate_list_from_index(session_, &iterator, ref.native))
        return {};
    std::optional<Candidate> result;
    if (api_->candidate_list_next(&iterator) && iterator.index == ref.native) {
        auto text = std::string(iterator.candidate.text ? iterator.candidate.text : "");
        result = Candidate{text, ref.native, readings_.identify(text, raw_)};
    }
    api_->candidate_list_end(&iterator);
    return result;
}
void RimeEngine::bookmark(size_t offset, int native) {
    auto found =
        std::find_if(bookmarks_.begin(), bookmarks_.end(), [offset](auto &b) { return b.offset == offset; });
    if (found != bookmarks_.end())
        bookmarks_.erase(found);
    if (bookmarks_.size() == 32)
        bookmarks_.pop_front();
    bookmarks_.push_back({offset, native});
}
RimeEngine::Page &RimeEngine::tail_page(size_t number) {
    auto found =
        std::find_if(pages_.begin(), pages_.end(), [number](auto &page) { return page.number == number; });
    if (found != pages_.end()) {
        auto page = *found;
        pages_.erase(found);
        pages_.push_back(page);
        return pages_.back();
    }
    size_t offset = 0, target = number * 7;
    int native = tail_start_;
    for (auto &b : bookmarks_)
        if (b.offset <= target && b.offset > offset) {
            offset = b.offset;
            native = b.native;
        }
    for (auto &page : pages_)
        if (page.count == 7 && (page.number + 1) * 7 <= target && (page.number + 1) * 7 > offset) {
            offset = (page.number + 1) * 7;
            native = page.next;
        }
    Page page;
    page.number = number;
    page.next = native;
    RimeCandidateListIterator iterator{};
    if (prefix_exhausted_ || !api_->candidate_list_from_index(session_, &iterator, native))
        page.exhausted = true;
    else {
        while (page.count < 7) {
            if (!api_->candidate_list_next(&iterator)) {
                page.exhausted = true;
                break;
            }
            page.next = iterator.index + 1;
            std::string_view text = iterator.candidate.text ? iterator.candidate.text : "";
            if (std::any_of(promoted_.begin(), promoted_.end(),
                            [text](auto &word) { return word.text == text; }))
                continue;
            if (offset >= target)
                page.native[page.count++] = iterator.index;
            ++offset;
            // Sparse, bounded reconstruction points also cover pages skipped during a cache miss.
            if (offset % (32 * 7) == 0)
                bookmark(offset, page.next);
        }
        api_->candidate_list_end(&iterator);
    }
    if (pages_.size() == 5)
        pages_.pop_front();
    pages_.push_back(page);
    return pages_.back();
}
std::optional<RimeEngine::Reference> RimeEngine::reference(size_t position) {
    if (position < prefix_.size())
        return prefix_[position];
    auto offset = position - prefix_.size();
    auto &page = tail_page(offset / 7);
    if (offset % 7 >= page.count)
        return {};
    return Reference{page.native[offset % 7], -1};
}
void RimeEngine::rebuild() {
    reset_cache();
    auto input = api_->get_input(session_);
    raw_ = input ? input : "";
    std::vector<Candidate> head;
    head.reserve(42);
    RimeCandidateListIterator iterator{};
    if (!api_->candidate_list_from_index(session_, &iterator, 0))
        prefix_exhausted_ = true;
    else {
        while (head.size() < 28) {
            if (!api_->candidate_list_next(&iterator)) {
                prefix_exhausted_ = true;
                break;
            }
            auto text = std::string(iterator.candidate.text ? iterator.candidate.text : "");
            int index = iterator.index;
            tail_start_ = index + 1;
            auto word = readings_.identify(text, raw_);
            head.push_back({text, index, word, -double(index),
                            word && match_reading(word->reading, raw_) != ReadingMatch::Full ? 1 : 0});
        }
        api_->candidate_list_end(&iterator);
    }
    bool boosted = false;
    if (frequency_ && index_ready_) {
        for (auto &[word, weight] : frequency_->matching(raw_)) {
            if (word.text.size() > 64 || word.reading.size() > 128 || !readings_.contains(word))
                continue;
            auto identity = readings_.identify(word.text, raw_);
            if (!identity || *identity != word)
                continue;
            boosted = true;
            auto existing =
                std::find_if(head.begin(), head.end(), [&](auto &c) { return c.word && *c.word == word; });
            double bonus = 6 * std::log2(1 + weight);
            if (existing != head.end())
                existing->score += bonus;
            else
                head.push_back({word.text, -1, word, -21 + bonus,
                                match_reading(word.reading, raw_) == ReadingMatch::Full ? 0 : 1});
        }
        if (boosted)
            std::stable_sort(head.begin(), head.end(), [](auto &a, auto &b) {
                return a.match_class != b.match_class ? a.match_class < b.match_class : a.score > b.score;
            });
    }
    prefix_.reserve(42);
    promoted_.reserve(14);
    for (auto &candidate : head) {
        if (candidate.index < 0) {
            prefix_.push_back({-1, int(promoted_.size())});
            promoted_.push_back(std::move(*candidate.word));
        } else
            prefix_.push_back({candidate.index, -1});
    }
    dirty_ = false;
}
void RimeEngine::append_commit(InputBuffer &b, const std::wstring &text) {
    b.set_preedit({}, {}, {}, 0);
    auto start = b.caret;
    if (b.insert(text) && frequency_ && chosen_ && utf8(text) == chosen_->text)
        b.learning_spans.push_back({start, text.size(), *chosen_, chosen_weight_});
    chosen_.reset();
}
void RimeEngine::sync(InputBuffer &b) {
    if (!session_)
        return;
    auto previous_source = b.source();
    auto previous_revision = b.revision;
    if (!injected_commit_.empty()) {
        append_commit(b, wide(injected_commit_));
        injected_commit_.clear();
    }
    RIME_STRUCT(RimeCommit, c);
    if (api_->get_commit(session_, &c)) {
        if (c.text)
            append_commit(b, wide(c.text));
        api_->free_commit(&c);
    }
    RIME_STRUCT(RimeContext, ctx);
    if (api_->get_context(session_, &ctx)) {
        auto input = api_->get_input(session_);
        personal_active_ = frequency_ && index_ready_ && input && *input && ctx.composition.sel_start == 0 &&
                           api_->get_caret_pos(session_) == strlen(input);
        if (personal_active_) {
            if (dirty_)
                rebuild();
            std::vector<std::wstring> list;
            for (size_t i = size_t(page_) * 7; i < size_t(page_ + 1) * 7; ++i) {
                auto ref = reference(i);
                auto candidate = ref ? read(*ref) : std::nullopt;
                if (!candidate)
                    break;
                list.push_back(wide(candidate->text));
            }
            b.page = page_;
            b.last_page = !reference(size_t(page_ + 1) * 7);
            selected_ = std::clamp(selected_, 0, std::max(0, int(list.size()) - 1));
            auto preview = list.empty() ? L"" : list[size_t(selected_)];
            b.set_preedit(wide(ctx.composition.preedit ? ctx.composition.preedit : ""), preview,
                          std::move(list), selected_);
            api_->free_context(&ctx);
            if (b.source() == previous_source)
                b.revision = previous_revision;
            return;
        }
        std::vector<std::wstring> list;
        b.page = ctx.menu.page_no;
        b.last_page = ctx.menu.is_last_page;
        for (int i = 0; i < ctx.menu.num_candidates && i < 7; i++)
            list.push_back(wide(ctx.menu.candidates[i].text ? ctx.menu.candidates[i].text : ""));
        b.set_preedit(wide(ctx.composition.preedit ? ctx.composition.preedit : ""),
                      wide(ctx.commit_text_preview ? ctx.commit_text_preview : ""), std::move(list),
                      ctx.menu.highlighted_candidate_index);
        api_->free_context(&ctx);
    }
    if (b.source() == previous_source)
        b.revision = previous_revision;
}
} // namespace es
