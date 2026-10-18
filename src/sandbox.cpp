#include "sandbox.hpp"
#include "ipc.hpp"
#include "resource.h"
#include <commctrl.h>
#include <imm.h>
namespace es {
class Sandbox {
    HWND window_ = nullptr, edit_ = nullptr, status_ = nullptr;
    HFONT font_ = nullptr;
    PipeClient pipe_;
    bool headless_ = false;
    bool interrupted_ = false;
    bool ready_ = false, active_ = false, ascii_ = false, shift_alone_ = false;
    std::string session_;
    uint64_t seq_ = 0;
    uint64_t layout_seq_ = 0;
    RECT anchor_{};
    bool anchor_visible_ = true;
    DWORD selection_start_ = 0, selection_end_ = 0;
    void end() {
        KillTimer(window_, 2);
        if (!session_.empty())
            pipe_.send(Json{{"type", "end"}, {"session", session_}});
        session_.clear();
        active_ = false;
        seq_ = 0;
        layout_seq_ = 0;
    }
    bool caret_rect(RECT &rect) {
        if (!headless_ && (IsIconic(window_) || !IsWindowVisible(window_)))
            return false;
        LRESULT point = SendMessageW(edit_, EM_POSFROMCHAR, selection_start_, 0);
        if (point == -1)
            return false;
        POINT caret{short(LOWORD(point)), short(HIWORD(point))};
        RECT client{};
        GetClientRect(edit_, &client);
        if (!PtInRect(&client, caret))
            return false;
        HDC dc = GetDC(edit_);
        auto font = SelectObject(dc, font_);
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        SelectObject(dc, font);
        ReleaseDC(edit_, dc);
        ClientToScreen(edit_, &caret);
        rect = {caret.x, caret.y, caret.x, caret.y + std::max(1L, metrics.tmHeight)};
        return true;
    }
    void update_anchor() {
        if (!active_ || session_.empty() || !ready_)
            return;
        if (!headless_ && (GetForegroundWindow() != window_ || GetFocus() != edit_)) {
            end();
            return;
        }
        DWORD start = 0, finish = 0;
        SendMessageW(edit_, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&finish));
        if (start != selection_start_ || finish != selection_end_) {
            end();
            return;
        }
        RECT next{};
        bool visible = caret_rect(next);
        if (visible != anchor_visible_ || (visible && !EqualRect(&next, &anchor_))) {
            anchor_visible_ = visible;
            if (visible)
                anchor_ = next;
            pipe_.send(Json{{"type", "anchor"},
                            {"session", session_},
                            {"layout_seq", ++layout_seq_},
                            {"visible", visible},
                            {"anchor", {anchor_.left, anchor_.top, anchor_.right, anchor_.bottom}}});
        }
    }
    bool eat(WPARAM vk) {
        if (!ready_)
            return false;
        if (GetKeyState(VK_MENU) & 0x8000 || GetKeyState(VK_LWIN) & 0x8000 || GetKeyState(VK_RWIN) & 0x8000)
            return false;
        if (vk == VK_SHIFT)
            return true;
        if (GetKeyState(VK_CONTROL) & 0x8000)
            return active_ && (vk == VK_RETURN || vk == 'J');
        if (ascii_)
            return false;
        return (vk >= 'A' && vk <= 'Z') ||
               (active_ && (vk == VK_RETURN || vk == VK_SPACE || vk == VK_ESCAPE || vk == VK_BACK ||
                            vk == VK_DELETE || (vk >= VK_PRIOR && vk <= VK_DOWN) ||
                            (vk >= '0' && vk <= '9') || (vk >= VK_OEM_1 && vk <= VK_OEM_8)));
    }
    void key(WPARAM vk, LPARAM lp) {
        if (session_.empty()) {
            session_ = session_id();
            SendMessageW(edit_, EM_GETSEL, reinterpret_cast<WPARAM>(&selection_start_),
                         reinterpret_cast<LPARAM>(&selection_end_));
        }
        BYTE keys[256]{};
        GetKeyboardState(keys);
        wchar_t c[8];
        int n = ToUnicodeEx(UINT(vk), UINT((lp >> 16) & 0xff), keys, c, 8, 4, GetKeyboardLayout(0));
        anchor_visible_ = caret_rect(anchor_);
        if (vk >= 'A' && vk <= 'Z')
            active_ = true;
        pipe_.send(Json{{"type", "key"},
                        {"session", session_},
                        {"seq", ++seq_},
                        {"vk", vk},
                        {"ctrl", bool(GetKeyState(VK_CONTROL) & 0x8000)},
                        {"shift", bool(GetKeyState(VK_SHIFT) & 0x8000)},
                        {"char", n > 0 ? utf8(std::wstring(c, size_t(n))) : ""},
                        {"anchor_visible", anchor_visible_},
                        {"anchor", Json::array({anchor_.left, anchor_.top, anchor_.right, anchor_.bottom})}});
        if (active_)
            SetTimer(window_, 2, 100, nullptr);
    }
    void event(PipeEvent &e) {
        auto &j = e.message;
        auto type = j.value("type", "");
        if (type == "paused") {
            ready_ = false;
            interrupted_ = false;
            end();
            SetWindowTextW(status_, L"言桥已退出。请重新打开言桥设置或试用程序以恢复。");
            return;
        }
        if (type == "ready") {
            if (!pipe_.current(e.peer))
                return;
            end();
            ascii_ = false;
            ready_ = j.value("engine", false);
            EnableWindow(edit_, ready_);
            SetWindowTextW(status_,
                           ready_ ? (interrupted_ ? L"输入服务已恢复。中断前未提交的草稿未恢复，请重新输入。"
                                                  : L"已连接真实全拼引擎。请在下方输入全拼。")
                                  : L"拼音引擎未准备好，请检查安装数据。");
            if (ready_ && GetForegroundWindow() == window_)
                SetFocus(edit_);
            return;
        }
        if (type == "disconnected") {
            interrupted_ = interrupted_ || active_;
            ready_ = false;
            end();
            SetWindowTextW(status_, L"输入服务连接中断，正在自动重连；未提交草稿不能恢复。");
            return;
        }
        if (!pipe_.current(e.peer))
            return;
        if (j.value("session", "") != session_)
            return;
        if (type == "state") {
            active_ = j.value("active", false);
            ascii_ = j.value("ascii", false);
            if (active_)
                SetTimer(window_, 2, 100, nullptr);
            else
                KillTimer(window_, 2);
        }
        if (type == "committed") {
            end();
            return;
        }
        if (type == "commit") {
            DWORD start, end;
            SendMessageW(edit_, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            bool ok = (headless_ || (GetForegroundWindow() == window_ && GetFocus() == edit_)) &&
                      start == selection_start_ && end == selection_end_;
            if (ok) {
                auto text = wide(j.value("text", ""));
                SendMessageW(edit_, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text.c_str()));
            }
            pipe_.send(Json{{"type", "ack"}, {"session", session_}, {"revision", j["revision"]}, {"ok", ok}});
        }
    }
    static LRESULT CALLBACK edit_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<Sandbox *>(data);
        if (m == WM_LBUTTONDOWN || m == WM_KILLFOCUS)
            self->end();
        auto result = DefSubclassProc(h, m, w, l);
        if (m == WM_VSCROLL || m == WM_HSCROLL || m == WM_MOUSEWHEEL)
            PostMessageW(self->window_, WM_APP + 78, 0, 0);
        return result;
    }
    static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<Sandbox *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<Sandbox *>(reinterpret_cast<CREATESTRUCT *>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self)
            return DefWindowProcW(h, m, w, l);
        if ((m == WM_TIMER && w == 2) || m == WM_APP + 78) {
            self->update_anchor();
            return 0;
        }
        if (m == WM_MOVE)
            PostMessageW(h, WM_APP + 78, 0, 0);
        if (m == WM_ES_EVENT) {
            std::unique_ptr<PipeEvent> event(reinterpret_cast<PipeEvent *>(l));
            try {
                self->event(*event);
            } catch (...) {
                self->end();
            }
            return 0;
        }
        if (m == WM_COMMAND && LOWORD(w) == 1) {
            self->end();
            auto broker = FindWindowW(L"EShuRu.Broker", nullptr);
            PostMessageW(broker, WM_APP + 74, 0, 0);
            return 0;
        }
        if (m == WM_SIZE) {
            int width = LOWORD(l), height = HIWORD(l);
            MoveWindow(self->edit_, 24, 148, std::max(width - 48, 100), std::max(height - 174, 80), TRUE);
            MoveWindow(self->status_, 24, 104, std::max(width - 48, 100), 30, TRUE);
            PostMessageW(h, WM_APP + 78, 0, 0);
            return 0;
        }
        if (m == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE)
            self->end();
        if (m == WM_DESTROY) {
            self->end();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }

  public:
    int run(bool headless) {
        headless_ = headless;
        WNDCLASSW c{};
        c.lpfnWndProc = proc;
        c.hInstance = GetModuleHandleW(nullptr);
        c.lpszClassName = L"EShuRu.Sandbox";
        c.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        c.hCursor = LoadCursor(nullptr, IDC_ARROW);
        c.hIcon = LoadIconW(c.hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
        if (!c.hIcon)
            c.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        RegisterClassW(&c);
        window_ = CreateWindowW(c.lpszClassName, L"言桥 0.2.5 · 原生开发试用窗口", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 860, 580, nullptr, nullptr, c.hInstance, this);
        font_ = CreateFontW(-17, 0, 0, 0, 400, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                            0, L"Microsoft YaHei UI");
        auto control = [&](const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h,
                           int id) {
            auto child = CreateWindowExW(std::wstring(cls) == L"EDIT" ? WS_EX_CLIENTEDGE : 0, cls, text,
                                         WS_CHILD | WS_VISIBLE | style, x, y, w, h, window_,
                                         reinterpret_cast<HMENU>(INT_PTR(id)), c.hInstance, nullptr);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            return child;
        };
        control(L"STATIC", L"言桥原生试用 · 全拼输入 / 自然英文 / 按需讲解", 0, 24, 22, 680, 28, 0);
        control(L"STATIC",
                L"这是未安装输入法时的开发试用窗口。系统输入法通过安装脚本启用。\r\n空格选词；Enter "
                L"输入中文；Ctrl+Enter 输入英文；Ctrl+J 讲解。",
                0, 24, 60, 650, 46, 0);
        control(L"BUTTON", L"设置", WS_TABSTOP, 730, 22, 80, 32, 1);
        status_ = control(L"STATIC", L"正在连接本地输入服务…", 0, 24, 112, 780, 30, 0);
        edit_ = control(L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 24, 154, 790,
                        345, 2);
        ImmAssociateContext(edit_, nullptr);
        SetWindowSubclass(edit_, edit_proc, 1, reinterpret_cast<DWORD_PTR>(this));
        EnableWindow(edit_, FALSE);
        pipe_.start(module_dir(), window_);
        if (!headless_)
            ShowWindow(window_, SW_SHOW);
        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            if (m.hwnd == edit_ && m.message == WM_KEYDOWN && eat(m.wParam)) {
                if (m.wParam == VK_SHIFT)
                    shift_alone_ = true;
                else {
                    shift_alone_ = false;
                    key(m.wParam, m.lParam);
                }
                continue;
            }
            if (m.hwnd == edit_ && m.message == WM_KEYUP && m.wParam == VK_SHIFT && shift_alone_) {
                shift_alone_ = false;
                key(VK_SHIFT, m.lParam);
                continue;
            }
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        pipe_.stop();
        while (PeekMessageW(&m, window_, WM_ES_EVENT, WM_ES_EVENT, PM_REMOVE))
            delete reinterpret_cast<PipeEvent *>(m.lParam);
        DeleteObject(font_);
        return 0;
    }
};
int run_sandbox(bool headless) {
    Sandbox sandbox;
    return sandbox.run(headless);
}
} // namespace es
