#include "app.hpp"
#include "registration.hpp"
#include "resource.h"
#include <shellapi.h>
namespace es {
void App::configure_learning() {
    if (!frequency_)
        return;
    bool enabled = settings_.value("remember_words", true);
    if (enabled != learning_enabled_) {
        learning_enabled_ = enabled;
        buffer_.learning_spans.clear();
        rime_.set_frequency(enabled ? frequency_.get() : nullptr);
    }
}
void App::end() {
    KillTimer(hwnd_, 1);
    if (translation_)
        translation_->cancel();
    if (explanation_)
        explanation_->cancel();
    pending_.reset();
    loading_ = false;
    blocked_ = false;
    configure_learning();
    buffer_.clear();
    rime_.clear();
    panel_.hide();
    session_.clear();
    layout_sequence_ = 0;
    anchor_visible_ = true;
    peer_.reset();
}
void App::refresh() {
    if (frequency_ && frequency_->failed())
        buffer_.notice = L"个人词频读取或保存失败，中文输入仍可使用。";
    else if (frequency_ && buffer_.active() && buffer_.notice.empty() && frequency_->take_recovery_notice())
        buffer_.notice = L"常用字词文件异常，已从加密副本恢复。";
    panel_.update({&buffer_, status_, error_, configured_, loading_, headless_, settings_}, anchor_);
    if (!anchor_visible_ || (!buffer_.active() && !buffer_.show_explanation))
        panel_.hide();
    reply_state();
}
void App::reply_state() {
    if (peer_)
        peer_->send(Json{{"type", "state"},
                         {"session", session_},
                         {"seq", sequence_},
                         {"active", buffer_.active()},
                         {"pending", buffer_.pending()},
                         {"page", buffer_.page},
                         {"last_page", buffer_.last_page},
                         {"ascii", buffer_.ascii},
                         {"revision", buffer_.revision},
                         {"source", utf8(buffer_.source())},
                         {"candidates",
                          [&] {
                              Json a = Json::array();
                              for (auto &c : buffer_.candidates)
                                  a.push_back(utf8(c));
                              return a;
                          }()},
                         {"ready_english", buffer_.can_english()}});
}
void App::source_changed(uint64_t before) {
    if (before == buffer_.revision)
        return;
    explanation_->cancel();
    buffer_.show_explanation = false;
    buffer_.explanation = nullptr;
    if (buffer_.source().empty()) {
        translation_->cancel();
        pending_.reset();
        loading_ = false;
        KillTimer(hwnd_, 1);
        return;
    }
    if (!configured_) {
        error_ = L"连接 DeepSeek 后显示英文。";
        status_ = L"未配置";
        return;
    }
    if (blocked_)
        return;
    pending_ = AiRequest{session_, buffer_.revision, utf8(buffer_.source()), "", false};
    status_ = L"更新中";
    schedule();
}
void App::schedule() {
    if (!pending_ || loading_ || blocked_)
        return;
    auto now = now_ms();
    if (now - last_start_ < 500) {
        SetTimer(hwnd_, 1, UINT(500 - (now - last_start_)), nullptr);
        return;
    }
    KillTimer(hwnd_, 1);
    last_start_ = now;
    loading_ = true;
    error_.clear();
    translation_->submit(std::move(*pending_));
    pending_.reset();
}
void App::result(AiResult &r) {
    if (r.session == "__connection_test") {
        if (r.complete) {
            auto settings = FindWindowW(L"EShuRu.Settings", nullptr);
            if (settings) {
                std::wstring message = r.error.empty() ? L"连接成功。短句完整返回耗时 " +
                                                             std::to_wstring(int(r.elapsed_ms)) + L" ms。"
                                                       : wide(r.error);
                SetDlgItemTextW(settings, 13, message.c_str());
            }
        }
        return;
    }
    if (r.session != session_)
        return;
    if (r.explanation) {
        if (r.revision != buffer_.revision || !buffer_.show_explanation ||
            r.request_id != explanation_request_)
            return;
        if (!r.error.empty())
            buffer_.explanation_error = wide(r.error);
        else if (r.complete)
            buffer_.explanation = std::move(r.items);
        if (r.complete)
            buffer_.explanation_loading = false;
        refresh();
        return;
    }
    if (!r.error.empty()) {
        loading_ = false;
        error_ = wide(r.error);
        status_ = L"暂不可用";
        blocked_ = true;
        pending_.reset();
        KillTimer(hwnd_, 1);
        buffer_.result_revision = UINT64_MAX;
    } else {
        if (r.revision == buffer_.revision && buffer_.english != wide(r.text)) {
            explanation_->cancel();
            ++explanation_request_;
            buffer_.explanation = nullptr;
            buffer_.explanation_error.clear();
            buffer_.explanation_loading = false;
            buffer_.show_explanation = false;
        }
        buffer_.accept_result(r.revision, wide(r.text), r.complete);
        status_ = r.complete && r.revision == buffer_.revision ? L"就绪" : L"更新中";
        if (r.complete) {
            loading_ = false;
            schedule();
        }
    }
    refresh();
}
void App::commit(bool english) {
    if (!peer_ || buffer_.committing)
        return;
    if (english && !buffer_.can_english()) {
        buffer_.notice =
            buffer_.pending() ? L"请先用空格或数字确认当前中文候选。" : L"请等待当前句子的完整翻译。";
        refresh();
        return;
    }
    if (!english && buffer_.pending()) {
        rime_.commit();
        rime_.sync(buffer_);
    }
    auto text = english ? buffer_.english : buffer_.text;
    if (text.empty())
        return;
    buffer_.committing = true;
    if (!peer_->send(Json{{"type", "commit"},
                          {"session", session_},
                          {"revision", buffer_.revision},
                          {"text", utf8(text)}})) {
        buffer_.committing = false;
        buffer_.notice = L"目标应用已断开，未输入文字。";
    }
    refresh();
}
void App::request_explanation() {
    explanation_->cancel();
    buffer_.explanation_error.clear();
    buffer_.explanation_loading = true;
    explanation_->submit(AiRequest{session_, buffer_.revision, utf8(buffer_.source()), utf8(buffer_.english),
                                   true, ++explanation_request_});
}
void App::action(int action, int position) {
    if (buffer_.committing)
        return;
    auto before = buffer_.revision;
    if (action >= Select) {
        rime_.select(action - Select);
        rime_.sync(buffer_);
    } else
        switch (action) {
        case Chinese:
            commit(false);
            return;
        case English:
            commit(true);
            return;
        case Learn:
            if (buffer_.can_english()) {
                buffer_.show_explanation = !buffer_.show_explanation;
                buffer_.explanation_error.clear();
                if (buffer_.show_explanation && buffer_.explanation.is_null()) {
                    request_explanation();
                } else if (!buffer_.show_explanation) {
                    explanation_->cancel();
                    buffer_.explanation_loading = false;
                }
            }
            break;
        case ExplainRetry:
            if (buffer_.can_english() && buffer_.show_explanation && !buffer_.explanation_loading) {
                request_explanation();
            }
            break;
        case PreviousPage:
        case NextPage:
            if (buffer_.pending() && (action == PreviousPage ? buffer_.page > 0 : !buffer_.last_page)) {
                rime_.key(action == PreviousPage ? 0xff55 : 0xff56);
                rime_.sync(buffer_);
            }
            break;
        case Settings:
            if (buffer_.active()) {
                buffer_.notice = L"请先输入或取消当前句子，再打开设置。";
            } else
                show_settings(nullptr, [this] {
                    settings_ = load_settings();
                    configured_ = !read_key().empty();
                    blocked_ = false;
                    configure_learning();
                });
            break;
        case Retry:
            blocked_ = false;
            configure_learning();
            error_.clear();
            pending_ = AiRequest{session_, buffer_.revision, utf8(buffer_.source()), "", false};
            schedule();
            break;
        case Caret:
            if (!buffer_.pending()) {
                buffer_.caret = std::min(size_t(std::max(position, 0)), buffer_.text.size());
                if (buffer_.caret < buffer_.text.size() && is_low_surrogate(buffer_.text[buffer_.caret]) &&
                    buffer_.caret)
                    buffer_.caret--;
            }
            break;
        }
    source_changed(before);
    refresh();
}
void App::key(const Json &j) {
    if (buffer_.committing)
        return;
    auto before = buffer_.revision;
    int vk = j.value("vk", 0);
    bool ctrl = j.value("ctrl", false), shift = j.value("shift", false);
    auto ch = wide(j.value("char", std::string{}));
    if (ctrl) {
        if (vk == VK_RETURN) {
            commit(true);
            return;
        }
        if (vk == 'J') {
            action(Learn);
            return;
        }
        return;
    }
    if (vk == VK_SHIFT) {
        if (!buffer_.active())
            buffer_.ascii = !buffer_.ascii;
        refresh();
        return;
    }
    if (vk == VK_ESCAPE) {
        if (buffer_.pending()) {
            rime_.clear();
            rime_.sync(buffer_);
        } else if (buffer_.show_explanation)
            buffer_.show_explanation = false;
        else {
            buffer_.clear();
            rime_.clear();
        }
    } else if (vk == VK_RETURN) {
        commit(false);
        return;
    } else if (vk == VK_BACK) {
        if (buffer_.pending())
            rime_.key(0xff08);
        else
            buffer_.backspace();
    } else if (vk == VK_DELETE) {
        if (buffer_.pending())
            rime_.key(0xffff);
        else
            buffer_.erase();
    } else if (vk == VK_LEFT || vk == VK_RIGHT) {
        if (buffer_.pending())
            rime_.key(vk == VK_LEFT ? 0xff51 : 0xff53);
        else
            buffer_.move(vk == VK_LEFT ? -1 : 1);
    } else if (vk == VK_HOME || vk == VK_END) {
        if (buffer_.pending())
            rime_.key(vk == VK_HOME ? 0xff50 : 0xff57);
        else
            buffer_.caret = vk == VK_HOME ? 0 : buffer_.text.size();
    } else if (buffer_.pending() &&
               (vk == VK_PRIOR || vk == VK_NEXT || (!shift && (vk == VK_OEM_MINUS || vk == VK_OEM_PLUS)))) {
        action(vk == VK_PRIOR || vk == VK_OEM_MINUS ? PreviousPage : NextPage);
        return;
    } else if (vk == VK_UP || vk == VK_DOWN) {
        if (buffer_.pending())
            // Arrow navigation stays on the current page; only explicit paging changes pages.
            if ((vk == VK_UP && buffer_.selected > 0) ||
                (vk == VK_DOWN && buffer_.selected + 1 < int(buffer_.candidates.size())))
                rime_.key(vk == VK_UP ? 0xff52 : 0xff54);
    } else if (vk == VK_SPACE) {
        if (buffer_.pending())
            rime_.key(' ');
        else
            buffer_.insert(L" ");
    } else if (vk >= '1' && vk <= '7' && buffer_.pending() && !shift) {
        rime_.select(vk - '1');
    } else if (vk >= 'A' && vk <= 'Z' && !shift) {
        if (character_count(buffer_.source()) < 300 && buffer_.pinyin.size() < 256)
            rime_.key(vk - 'A' + 'a');
        else
            buffer_.notice = L"请先输入或取消当前句子。";
    } else if (!ch.empty()) {
        if (buffer_.pending()) {
            rime_.commit();
            rime_.sync(buffer_);
        }
        if (ch == L",")
            ch = L"，";
        else if (ch == L".")
            ch = L"。";
        else if (ch == L"?")
            ch = L"？";
        else if (ch == L"!")
            ch = L"！";
        else if (ch == L";")
            ch = L"；";
        else if (ch == L":")
            ch = L"：";
        buffer_.insert(ch);
    }
    rime_.sync(buffer_);
    source_changed(before);
    refresh();
}
void App::event(PipeEvent &e) {
    auto &j = e.message;
    auto type = j.value("type", "");
    if (type == "hello") {
        if (j.value("pid", DWORD(0)) != e.peer->peer_pid) {
            e.peer->send(Json{{"type", "error"}});
            return;
        }
        e.peer->send(Json{{"type", "ready"}, {"engine", rime_.ready()}});
        return;
    }
    if (type == "disconnected") {
        if (e.peer == peer_)
            end();
        return;
    }
    if (type == "recovery_notice") {
        interrupted_peer_ = e.peer;
        if (!headless_) {
            NOTIFYICONDATAW notice{sizeof(notice)};
            notice.hWnd = hwnd_;
            notice.uID = 1;
            notice.uFlags = NIF_INFO;
            notice.dwInfoFlags = NIIF_WARNING;
            wcscpy_s(notice.szInfoTitle, L"言桥输入服务已恢复");
            wcscpy_s(notice.szInfo,
                     L"连接中断前未提交的草稿未恢复，请重新输入。已输入到应用中的文字不受影响。");
            Shell_NotifyIconW(NIM_MODIFY, &notice);
        }
        return;
    }
    auto session = j.value("session", "");
    if (session.size() > 80 || session.empty())
        return;
    if (type == "key") {
        if (!rime_.ready())
            return;
        if (!headless_) {
            DWORD foreground = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
            if (foreground != e.peer->peer_pid)
                return;
        }
        auto seq = j.value("seq", uint64_t(0));
        if (session != session_ || e.peer != peer_) {
            end();
            peer_ = e.peer;
            session_ = session;
            sequence_ = 0;
            layout_sequence_ = 0;
            anchor_visible_ = true;
            error_ = configured_ ? L"" : L"连接 DeepSeek 后显示英文。";
            status_ = configured_ ? L"等待输入" : L"未配置";
        }
        if (seq <= sequence_)
            return;
        sequence_ = seq;
        if (j.contains("anchor_visible"))
            anchor_visible_ = j.value("anchor_visible", true);
        if (j.contains("anchor") && j["anchor"].is_array() && j["anchor"].size() == 4) {
            anchor_ = {j["anchor"][0].get<LONG>(), j["anchor"][1].get<LONG>(), j["anchor"][2].get<LONG>(),
                       j["anchor"][3].get<LONG>()};
        }
        key(j);
        if (interrupted_peer_.lock() == e.peer) {
            buffer_.notice = L"输入服务已恢复；中断前未提交的草稿未恢复，请重新输入。";
            interrupted_peer_.reset();
            refresh();
        }
        return;
    }
    if (session != session_ || e.peer != peer_)
        return;
    if (type == "anchor") {
        auto seq = j.value("layout_seq", uint64_t(0));
        if (seq <= layout_sequence_ || !buffer_.active())
            return;
        if (!headless_) {
            DWORD foreground = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
            if (foreground != e.peer->peer_pid)
                return;
        }
        bool visible = j.value("visible", false);
        RECT next = anchor_;
        if (visible) {
            auto a = j.find("anchor");
            if (a == j.end() || !a->is_array() || a->size() != 4)
                return;
            for (auto &v : *a)
                if (!v.is_number_integer())
                    return;
            next = {(*a)[0].get<LONG>(), (*a)[1].get<LONG>(), (*a)[2].get<LONG>(), (*a)[3].get<LONG>()};
            if (next.right < next.left || next.bottom <= next.top)
                return;
        }
        layout_sequence_ = seq;
        bool changed = visible != anchor_visible_ || !EqualRect(&next, &anchor_);
        anchor_visible_ = visible;
        anchor_ = next;
        if (changed && visible)
            panel_.move(anchor_);
        if (!visible)
            panel_.hide();
        // Geometry has its own acknowledgement; never schedules translation or changes input revision.
        peer_->send(Json{{"type", "layout"},
                         {"session", session_},
                         {"layout_seq", seq},
                         {"visible", visible},
                         {"anchor", {next.left, next.top, next.right, next.bottom}}});
        return;
    }
    if (type == "end") {
        end();
        return;
    }
    if (type == "ack" && buffer_.committing) {
        if (j.value("revision", UINT64_MAX) != buffer_.revision)
            return;
        if (j.value("ok", false)) {
            if (learning_enabled_ && frequency_)
                frequency_->learn(buffer_.learning_spans);
            auto peer = peer_;
            auto id = session_;
            buffer_.clear();
            rime_.clear();
            translation_->cancel();
            explanation_->cancel();
            pending_.reset();
            loading_ = false;
            peer->send(Json{{"type", "committed"}, {"session", id}});
            refresh();
        } else {
            buffer_.committing = false;
            buffer_.notice = L"目标输入位置已改变，未输入文字。";
            refresh();
        }
    }
}
LRESULT CALLBACK App::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<App *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        self = static_cast<App *>(reinterpret_cast<CREATESTRUCT *>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(h, m, w, l);
    try {
        if (m == WM_CLOSE) {
            if (!set_auto_start_allowed(false)) {
                if (!self->headless_)
                    MessageBoxW(h, L"无法保存退出状态，请检查当前用户权限后重试。", L"言桥", MB_ICONERROR);
                return 0;
            }
            self->end();
            PostQuitMessage(0);
            return 0;
        }
        if (m == WM_APP + 76) {
            if (self->frequency_) {
                auto before = self->buffer_.revision;
                self->buffer_.learning_spans.clear();
                bool ok = self->frequency_->clear();
                self->rime_.set_frequency(self->learning_enabled_ ? self->frequency_.get() : nullptr);
                self->rime_.sync(self->buffer_);
                self->source_changed(before);
                self->refresh();
                auto settings = self->headless_ ? nullptr : FindWindowW(L"EShuRu.Settings", nullptr);
                if (settings)
                    SetDlgItemTextW(settings, 13,
                                    ok ? L"常用字词已清除。"
                                       : L"内存词频已清除，但磁盘清理未完成，请检查目录权限后重试。");
            }
            return 0;
        }
        if (m == WM_APP + 74) {
            show_settings(nullptr, [self] {
                self->settings_ = load_settings();
                self->configured_ = !read_key().empty();
                self->blocked_ = false;
                self->configure_learning();
            });
            return 0;
        }
        if (m == WM_APP + 75) {
            if (!self->headless_ && !self->buffer_.active()) {
                self->translation_->submit(AiRequest{"__connection_test", 0, "你好。", "", false});
            }
            return 0;
        }
        if (m == WM_ES_EVENT) {
            std::unique_ptr<PipeEvent> event(reinterpret_cast<PipeEvent *>(l));
            self->event(*event);
            return 0;
        }
        if (m == WM_ES_AI) {
            std::unique_ptr<AiResult> result(reinterpret_cast<AiResult *>(l));
            self->result(*result);
            return 0;
        }
        if (m == WM_TIMER && w == 1) {
            KillTimer(h, 1);
            self->schedule();
            return 0;
        }
        if (m == WM_APP + 73) {
            if (l == WM_LBUTTONDBLCLK)
                show_settings(nullptr, [self] {
                    self->settings_ = load_settings();
                    self->configured_ = !read_key().empty();
                    self->blocked_ = false;
                    self->configure_learning();
                });
            if (l == WM_RBUTTONUP) {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, 1, L"设置");
                AppendMenuW(menu, MF_STRING, 2, L"退出辅助进程");
                POINT p;
                GetCursorPos(&p);
                SetForegroundWindow(h);
                int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, h, nullptr);
                DestroyMenu(menu);
                if (id == 1)
                    show_settings(nullptr, [self] {
                        self->settings_ = load_settings();
                        self->configured_ = !read_key().empty();
                        self->blocked_ = false;
                        self->configure_learning();
                    });
                if (id == 2)
                    PostMessageW(h, WM_CLOSE, 0, 0);
            }
            return 0;
        }
    } catch (...) {
        self->end();
    }
    return DefWindowProcW(h, m, w, l);
}
int App::run(bool headless, bool settings) {
    headless_ = headless;
    settings_ = load_settings();
    // One-time repair for installations that registered COM but did not add the
    // profile to the current user's keyboard list. Respect later manual removal:
    // once checked, do not re-add it on every launch.
    if (!headless_ && !settings_.value("input_registration_checked", false) &&
        ensure_input_registered()) {
        settings_["input_registration_checked"] = true;
        save_settings(settings_);
    }
    configured_ = !headless && !read_key().empty();
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.lpszClassName = L"EShuRu.Broker";
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!wc.hIcon)
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"言桥", WS_POPUP, 0, 0, 0, 0, nullptr,
                            nullptr, wc.hInstance, this);
    panel_.create([this](int a, int p) { action(a, p); });
    if (!rime_.initialize(module_dir())) {
        if (!headless)
            MessageBoxW(nullptr, rime_.error.c_str(), L"言桥全拼引擎", MB_ICONERROR);
        DestroyWindow(hwnd_);
        return 2;
    }
    auto frequency_path = config_dir() / L"word-frequency.dat";
    if (headless_)
        frequency_path = config_dir() / (L"test-frequency-" + ipc_scope() + L".dat");
    frequency_ = std::make_unique<FrequencyStore>(frequency_path);
    configure_learning();
    translation_ = std::make_unique<AiWorker>(hwnd_);
    explanation_ = std::make_unique<AiWorker>(hwnd_);
    server_.start(hwnd_);
    NOTIFYICONDATAW tray{sizeof(tray)};
    tray.hWnd = hwnd_;
    tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray.uCallbackMessage = WM_APP + 73;
    tray.hIcon = wc.hIcon;
    wcscpy_s(tray.szTip, L"言桥 · 双击打开设置");
    if (!headless)
        Shell_NotifyIconW(NIM_ADD, &tray);
    if (settings)
        show_settings(nullptr, [this] {
            settings_ = load_settings();
            configured_ = !read_key().empty();
            blocked_ = false;
            configure_learning();
        });
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (auto sh = FindWindowW(L"EShuRu.Settings", nullptr); sh && IsDialogMessageW(sh, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    server_.stop();
    translation_.reset();
    explanation_.reset();
    if (!headless)
        Shell_NotifyIconW(NIM_DELETE, &tray);
    DestroyWindow(hwnd_);
    return 0;
}
int App::render(const std::filesystem::path &path, int scenario) {
    panel_.create([](int, int) {});
    buffer_.insert(L"我今天累坏了，想早点睡。");
    buffer_.english = L"I'm beat. I think I'll turn in early tonight.";
    buffer_.result_revision = buffer_.revision;
    buffer_.show_explanation = true;
    buffer_.explanation = Json{
        {"items",
         Json::array(
             {Json{{"expression", "I'm beat"}, {"meaning_zh", "我累坏了，常见的非正式口语表达。"}},
              Json{{"expression", "turn in early"}, {"meaning_zh", "早点睡。turn in 在这里表示上床睡觉。"}},
              Json{{"expression", "I think I'll…"},
                   {"meaning_zh", "用来表达当下的打算。"},
                   {"example_en", "I think I'll take a break."},
                   {"example_zh", "我想休息一下。"}}})}};
    if (scenario == 1) {
        buffer_.clear();
        buffer_.set_preedit(L"ta", L"他",
                            {L"他", L"她", L"它", L"塔", L"踏", L"他们今天下午一起去图书馆", L"她们"}, 0);
        buffer_.last_page = false;
    } else if (scenario == 2) {
        buffer_.explanation = nullptr;
        buffer_.explanation_error = L"讲解字段不符合要求，请重新讲解。";
    }
    panel_.update({&buffer_, L"设计样稿", L"", false, false, true, Json{{"theme", "light"}}}, anchor_);
    panel_.bitmap(path);
    return 0;
}
} // namespace es
