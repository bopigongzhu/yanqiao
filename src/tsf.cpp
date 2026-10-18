#include "common.hpp"
#include "ipc.hpp"
#include <msctf.h>
#include <initguid.h>
#include <inputscope.h>
#include <array>

namespace {
using namespace es;
HMODULE module = nullptr;
std::atomic_long objects{0}, locks{0};
const CLSID CLSID_ES = {0x5db324ae, 0x9ed8, 0x4e80, {0x9b, 0xce, 0x85, 0x20, 0xdf, 0xfa, 0x3c, 0x93}};
const GUID PROFILE_ES = {0x6ade2191, 0x955d, 0x4bb3, {0xbb, 0x54, 0x8b, 0x9e, 0xe9, 0x41, 0xfa, 0x20}};
const GUID IID_ProcessorEx = {0x6e4e2102, 0xf9cd, 0x433d, {0xb4, 0x96, 0x30, 0x3c, 0xe0, 0x3a, 0x65, 0x07}};
struct ProcessorEx : ITfTextInputProcessor {
    virtual HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr *, TfClientId, DWORD) = 0;
};
// The MinGW SDK omits this standard TSF interface.
const GUID IID_LayoutSink = {0x2af2d06a, 0xdd5b, 0x4927, {0xa0, 0xb4, 0x54, 0xf1, 0x9c, 0x91, 0xfa, 0xde}};
struct LayoutSink : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE OnLayoutChange(ITfContext *, int, ITfContextView *) = 0;
};
std::wstring guid_string(REFGUID guid) {
    wchar_t b[40];
    StringFromGUID2(guid, b, 40);
    return b;
}

class EditSession final : public ITfEditSession {
    ULONG refs_ = 1;
    std::function<HRESULT(TfEditCookie)> fn_;
    IUnknown *owner_;

  public:
    EditSession(IUnknown *owner, std::function<HRESULT(TfEditCookie)> fn)
        : fn_(std::move(fn)), owner_(owner) {
        owner_->AddRef();
        objects++;
    }
    ~EditSession() {
        owner_->Release();
        objects--;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITfEditSession) {
            *out = static_cast<ITfEditSession *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs_;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override {
        try {
            return fn_(cookie);
        } catch (...) {
            return E_FAIL;
        }
    }
};

class Tip final : public ProcessorEx,
                  public ITfKeyEventSink,
                  public ITfThreadMgrEventSink,
                  public ITfTextEditSink,
                  public LayoutSink {
    ULONG refs_ = 1;
    ComPtr<ITfThreadMgr> manager_;
    ComPtr<ITfContext> context_;
    ComPtr<ITfRange> anchor_;
    TfClientId client_ = 0;
    DWORD mgr_cookie_ = TF_INVALID_COOKIE, text_cookie_ = TF_INVALID_COOKIE;
    DWORD layout_cookie_ = TF_INVALID_COOKIE;
    uint64_t layout_request_ = 0, layout_sequence_ = 0, last_layout_read_ = 0;
    bool layout_pending_ = false, layout_posted_ = false, caret_visible_ = true;
    HWND window_ = nullptr;
    PipeClient pipe_;
    std::string session_;
    uint64_t sequence_ = 0;
    bool keys_advised_ = false;
    bool interrupted_ = false;
    bool active_ = false, ascii_ = false, shift_alone_ = false, secure_ = false, committing_ = false,
         engine_ready_ = false;
    std::array<bool, 256> eaten_{};
    RECT caret_{};
    IUnknown *identity() {
        return static_cast<ProcessorEx *>(this);
    }
    void end() {
        if (window_)
            KillTimer(window_, 2);
        ++layout_request_;
        // Keep the one outstanding read until its callback arrives, even across cancellation.
        layout_posted_ = false;
        layout_sequence_ = 0;
        if (!session_.empty())
            pipe_.send(Json{{"type", "end"}, {"session", session_}});
        if (context_) {
            ComPtr<ITfSource> source;
            if (SUCCEEDED(context_->QueryInterface(IID_ITfSource, reinterpret_cast<void **>(source.put())))) {
                if (text_cookie_ != TF_INVALID_COOKIE)
                    source->UnadviseSink(text_cookie_);
                if (layout_cookie_ != TF_INVALID_COOKIE)
                    source->UnadviseSink(layout_cookie_);
            }
        }
        text_cookie_ = TF_INVALID_COOKIE;
        layout_cookie_ = TF_INVALID_COOKIE;
        session_.clear();
        sequence_ = 0;
        active_ = false;
        committing_ = false;
        anchor_.reset();
        context_.reset();
    }
    bool read_caret(TfEditCookie cookie, RECT &rect) {
        if (!context_ || !anchor_)
            return false;
        ComPtr<ITfContextView> view;
        if (FAILED(context_->GetActiveView(view.put())))
            return false;
        BOOL clipped = FALSE;
        HRESULT hr = view->GetTextExt(cookie, anchor_.get(), &rect, &clipped);
        if (SUCCEEDED(hr)) {
            if (clipped || rect.bottom <= rect.top || rect.right < rect.left)
                return false;
            RECT screen{};
            if (SUCCEEDED(view->GetScreenExt(&screen))) {
                RECT point = rect, intersection{};
                point.right = std::max(point.right, point.left + 1);
                if (!IntersectRect(&intersection, &point, &screen))
                    return false;
            }
            return true;
        }
        if (hr == TF_E_NOLAYOUT)
            return false;
        // Legacy controls may expose a native caret instead of text extents.
        HWND owner = nullptr;
        view->GetWnd(&owner);
        GUITHREADINFO info{sizeof(info)};
        if (owner && GetGUIThreadInfo(GetCurrentThreadId(), &info) && info.hwndCaret &&
            (info.hwndCaret == owner || IsChild(owner, info.hwndCaret))) {
            rect = info.rcCaret;
            MapWindowPoints(info.hwndCaret, nullptr, reinterpret_cast<POINT *>(&rect), 2);
            return rect.bottom > rect.top;
        }
        return false;
    }
    void request_layout() {
        if (!active_ || !context_ || session_.empty() || committing_ || layout_pending_ ||
            now_ms() - last_layout_read_ < 50)
            return;
        DWORD foreground = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
        if (foreground != GetCurrentProcessId()) {
            end();
            return;
        }
        auto id = session_;
        auto request = ++layout_request_;
        last_layout_read_ = now_ms();
        layout_pending_ = true;
        context_->AddRef();
        auto context = std::shared_ptr<ITfContext>(context_.get(), [](ITfContext *c) { c->Release(); });
        auto edit = new EditSession(identity(), [this, id, request, context](TfEditCookie cookie) {
            layout_pending_ = false;
            if (id != session_ || request != layout_request_ || context.get() != context_.get())
                return S_OK;
            if (!active_ || committing_)
                return S_OK;
            ComPtr<ITfDocumentMgr> focused;
            ComPtr<ITfContext> current;
            if (manager_)
                manager_->GetFocus(focused.put());
            if (focused)
                focused->GetTop(current.put());
            DWORD foreground = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
            if (current.get() != context_.get() || foreground != GetCurrentProcessId() ||
                blocked(context_.get())) {
                end();
                return S_OK;
            }
            TF_SELECTION selected{};
            ULONG count = 0;
            if (FAILED(context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selected, &count)) ||
                !count) {
                end();
                return S_OK;
            }
            LONG start = 1, finish = 1;
            selected.range->CompareStart(cookie, anchor_.get(), TF_ANCHOR_START, &start);
            selected.range->CompareEnd(cookie, anchor_.get(), TF_ANCHOR_END, &finish);
            selected.range->Release();
            if (start || finish) {
                end();
                return S_OK;
            }
            RECT next{};
            bool visible = read_caret(cookie, next);
            if (visible != caret_visible_ || (visible && !EqualRect(&next, &caret_))) {
                caret_visible_ = visible;
                if (visible)
                    caret_ = next;
                pipe_.send(Json{{"type", "anchor"},
                                {"session", id},
                                {"layout_seq", ++layout_sequence_},
                                {"visible", visible},
                                {"anchor", {caret_.left, caret_.top, caret_.right, caret_.bottom}}});
            }
            return S_OK;
        });
        HRESULT result = E_FAIL;
        HRESULT hr = context->RequestEditSession(client_, edit, TF_ES_READ | TF_ES_ASYNCDONTCARE, &result);
        edit->Release();
        if (FAILED(hr) || (FAILED(result) && result != TF_S_ASYNC))
            layout_pending_ = false;
    }
    void post_layout() {
        if (!active_ || session_.empty() || layout_posted_)
            return;
        layout_posted_ = PostMessageW(window_, WM_APP + 78, 0, 0) != FALSE;
    }
    bool blocked(ITfContext *context) {
        if (secure_ || !context)
            return true;
        TF_STATUS status{};
        if (FAILED(context->GetStatus(&status)) || (status.dwDynamicFlags & TS_SD_READONLY))
            return true;
        HWND focus = GetFocus();
        if (focus && (GetWindowLongPtrW(focus, GWL_STYLE) & ES_PASSWORD)) {
            wchar_t cls[128];
            GetClassNameW(focus, cls, 128);
            if (_wcsicmp(cls, L"Edit") == 0)
                return true;
        }
        ComPtr<ITfCompartmentMgr> compartments;
        if (SUCCEEDED(context->QueryInterface(IID_ITfCompartmentMgr,
                                              reinterpret_cast<void **>(compartments.put())))) {
            for (auto id : {GUID_COMPARTMENT_KEYBOARD_DISABLED, GUID_COMPARTMENT_EMPTYCONTEXT}) {
                ComPtr<ITfCompartment> c;
                if (SUCCEEDED(compartments->GetCompartment(id, c.put()))) {
                    VARIANT v;
                    VariantInit(&v);
                    c->GetValue(&v);
                    bool disabled = v.vt == VT_I4 && v.lVal;
                    VariantClear(&v);
                    if (disabled)
                        return true;
                }
            }
        }
        return false;
    }
    bool begin(ITfContext *context) {
        if (context_.get() == context && !session_.empty())
            return true;
        end();
        if (blocked(context))
            return false;
        context->AddRef();
        context_.reset(context);
        bool allowed = false;
        auto edit = new EditSession(identity(), [&](TfEditCookie cookie) {
            TF_SELECTION sel{};
            ULONG fetched = 0;
            if (FAILED(context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) || !fetched)
                return E_FAIL;
            ComPtr<ITfRange> selection;
            selection.reset(sel.range);
            ComPtr<ITfProperty> property;
            if (SUCCEEDED(context_->GetProperty(GUID_PROP_INPUTSCOPE, property.put()))) {
                VARIANT value;
                VariantInit(&value);
                if (SUCCEEDED(property->GetValue(cookie, selection.get(), &value)) &&
                    value.vt == VT_UNKNOWN && value.punkVal) {
                    ComPtr<ITfInputScope> scope;
                    if (SUCCEEDED(value.punkVal->QueryInterface(IID_ITfInputScope,
                                                                reinterpret_cast<void **>(scope.put())))) {
                        InputScope *values = nullptr;
                        UINT count = 0;
                        if (SUCCEEDED(scope->GetInputScopes(&values, &count))) {
                            for (UINT i = 0; i < count; i++)
                                if (values[i] == IS_PASSWORD || values[i] == IS_NUMERIC_PASSWORD ||
                                    values[i] == IS_NUMERIC_PIN || values[i] == IS_ALPHANUMERIC_PIN ||
                                    values[i] == IS_ALPHANUMERIC_PIN_SET) {
                                    CoTaskMemFree(values);
                                    VariantClear(&value);
                                    return E_ACCESSDENIED;
                                }
                            CoTaskMemFree(values);
                        }
                    }
                }
                VariantClear(&value);
            }
            if (FAILED(selection->Clone(anchor_.put())))
                return E_FAIL;
            caret_visible_ = read_caret(cookie, caret_);
            allowed = true;
            return S_OK;
        });
        HRESULT outcome = E_FAIL;
        HRESULT hr = context_->RequestEditSession(client_, edit, TF_ES_READ | TF_ES_SYNC, &outcome);
        edit->Release();
        if (FAILED(hr) || FAILED(outcome) || !allowed) {
            end();
            return false;
        }
        ComPtr<ITfSource> source;
        if (SUCCEEDED(context_->QueryInterface(IID_ITfSource, reinterpret_cast<void **>(source.put())))) {
            source->AdviseSink(IID_ITfTextEditSink, static_cast<ITfTextEditSink *>(this), &text_cookie_);
            source->AdviseSink(IID_LayoutSink, static_cast<LayoutSink *>(this), &layout_cookie_);
        }
        session_ = session_id();
        return true;
    }
    bool should_eat(ITfContext *context, WPARAM vk) {
        if (!engine_ready_ || !pipe_.ready() || blocked(context) || vk > 255)
            return false;
        if (GetKeyState(VK_MENU) & 0x8000 || GetKeyState(VK_LWIN) & 0x8000 || GetKeyState(VK_RWIN) & 0x8000)
            return false;
        if (vk == VK_SHIFT)
            return true;
        bool ctrl = GetKeyState(VK_CONTROL) & 0x8000;
        if (ctrl)
            return active_ && (vk == VK_RETURN || vk == 'J');
        if (ascii_)
            return false;
        if (vk >= 'A' && vk <= 'Z')
            return true;
        if (!active_)
            return false;
        return vk == VK_SPACE || vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_BACK || vk == VK_DELETE ||
               (vk >= VK_PRIOR && vk <= VK_DOWN) || (vk >= '0' && vk <= '9') ||
               (vk >= VK_OEM_1 && vk <= VK_OEM_8);
    }
    bool send_key(ITfContext *context, WPARAM vk, LPARAM lp) {
        if (!begin(context))
            return false;
        BYTE keys[256]{};
        GetKeyboardState(keys);
        wchar_t chars[8]{};
        int n = ToUnicodeEx(UINT(vk), UINT((lp >> 16) & 0xff), keys, chars, 8, 4, GetKeyboardLayout(0));
        std::wstring text = n > 0 ? std::wstring(chars, size_t(n)) : L"";
        if (vk >= 'A' && vk <= 'Z' && !ascii_)
            active_ = true;
        bool sent =
            pipe_.send(Json{{"type", "key"},
                            {"session", session_},
                            {"seq", ++sequence_},
                            {"vk", vk},
                            {"ctrl", bool(GetKeyState(VK_CONTROL) & 0x8000)},
                            {"shift", bool(GetKeyState(VK_SHIFT) & 0x8000)},
                            {"char", utf8(text)},
                            {"anchor_visible", caret_visible_},
                            {"anchor", Json::array({caret_.left, caret_.top, caret_.right, caret_.bottom})}});
        if (!sent)
            end();
        else if (active_) {
            SetTimer(window_, 2, 100, nullptr);
            post_layout();
        }
        return sent;
    }
    void receive(PipeEvent &event) {
        auto &j = event.message;
        auto type = j.value("type", "");
        if (type == "paused") {
            engine_ready_ = false;
            interrupted_ = false;
            end();
            return;
        }
        if (type == "ready") {
            if (!pipe_.current(event.peer))
                return;
            end();
            ascii_ = false;
            engine_ready_ = j.value("engine", false);
            if (engine_ready_ && interrupted_) {
                pipe_.send(Json{{"type", "recovery_notice"}});
                interrupted_ = false;
            }
            return;
        }
        if (type == "disconnected") {
            interrupted_ = interrupted_ || active_;
            engine_ready_ = false;
            end();
            return;
        }
        if (!pipe_.current(event.peer))
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
            return;
        }
        if (type == "committed") {
            end();
            return;
        }
        if (type != "commit" || !context_ || committing_)
            return;
        auto text = wide(j.value("text", ""));
        if (text.empty() || text.size() > 8192)
            return;
        auto id = session_;
        auto revision = j.value("revision", uint64_t(0));
        committing_ = true;
        auto connection = event.peer;
        auto edit = new EditSession(identity(), [this, text, id, revision, connection](TfEditCookie cookie) {
            bool ok = false;
            if (pipe_.current(connection) && id == session_ && context_ && anchor_) {
                ComPtr<ITfDocumentMgr> focused;
                ComPtr<ITfContext> current;
                manager_->GetFocus(focused.put());
                if (focused)
                    focused->GetTop(current.put());
                DWORD foreground = 0;
                GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
                if (current.get() == context_.get() && foreground == GetCurrentProcessId()) {
                    TF_SELECTION sel{};
                    ULONG n = 0;
                    if (SUCCEEDED(context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &sel, &n)) && n) {
                        LONG start = 1, end = 1;
                        sel.range->CompareStart(cookie, anchor_.get(), TF_ANCHOR_START, &start);
                        sel.range->CompareEnd(cookie, anchor_.get(), TF_ANCHOR_END, &end);
                        if (!start && !end) {
                            HRESULT hr = sel.range->SetText(cookie, 0, text.c_str(), LONG(text.size()));
                            if (SUCCEEDED(hr)) {
                                // Insertion already succeeded: a caret update failure must not allow
                                // retrying and inserting the same sentence twice.
                                ok = true;
                                sel.range->Collapse(cookie, TF_ANCHOR_END);
                                sel.style.ase = TF_AE_NONE;
                                sel.style.fInterimChar = FALSE;
                                context_->SetSelection(cookie, 1, &sel);
                            }
                        }
                        sel.range->Release();
                    }
                }
            }
            if (!ok)
                committing_ = false;
            pipe_.send(Json{{"type", "ack"}, {"session", id}, {"revision", revision}, {"ok", ok}});
            return ok ? S_OK : E_FAIL;
        });
        HRESULT result = E_FAIL;
        HRESULT hr =
            context_->RequestEditSession(client_, edit, TF_ES_READWRITE | TF_ES_ASYNCDONTCARE, &result);
        edit->Release();
        if (FAILED(hr) || (FAILED(result) && result != TF_S_ASYNC)) {
            committing_ = false;
            pipe_.send(Json{{"type", "ack"}, {"session", id}, {"revision", revision}, {"ok", false}});
        }
    }
    static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<Tip *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<Tip *>(reinterpret_cast<CREATESTRUCT *>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && (m == WM_APP + 78 || (m == WM_TIMER && w == 2))) {
            if (m == WM_APP + 78)
                self->layout_posted_ = false;
            try {
                self->request_layout();
            } catch (...) {
                self->end();
            }
            return 0;
        }
        if (m == WM_ES_EVENT && self) {
            std::unique_ptr<PipeEvent> event(reinterpret_cast<PipeEvent *>(l));
            try {
                self->receive(*event);
            } catch (...) {
                self->end();
            }
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }

  public:
    Tip() {
        objects++;
    }
    ~Tip() {
        Deactivate();
        objects--;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITfTextInputProcessor || iid == IID_ProcessorEx)
            *out = static_cast<ProcessorEx *>(this);
        else if (iid == IID_ITfKeyEventSink)
            *out = static_cast<ITfKeyEventSink *>(this);
        else if (iid == IID_ITfThreadMgrEventSink)
            *out = static_cast<ITfThreadMgrEventSink *>(this);
        else if (iid == IID_ITfTextEditSink)
            *out = static_cast<ITfTextEditSink *>(this);
        else if (iid == IID_LayoutSink)
            *out = static_cast<LayoutSink *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs_;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr *manager, TfClientId client) override {
        return ActivateEx(manager, client, 0);
    }
    HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr *manager, TfClientId client, DWORD flags) override {
        if (!manager)
            return E_INVALIDARG;
        if (manager_)
            return E_UNEXPECTED;
        secure_ = bool(flags & TF_TMAE_SECUREMODE);
        manager->AddRef();
        manager_.reset(manager);
        client_ = client;
        // Secure desktops must never subscribe to keys, create UI, or start the broker.
        if (secure_)
            return S_OK;
        ComPtr<ITfKeystrokeMgr> keys;
        HRESULT hr = manager->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void **>(keys.put()));
        if (FAILED(hr)) {
            Deactivate();
            return hr;
        }
        hr = keys->AdviseKeyEventSink(client, static_cast<ITfKeyEventSink *>(this), TRUE);
        if (FAILED(hr)) {
            Deactivate();
            return hr;
        }
        keys_advised_ = true;
        ComPtr<ITfSource> source;
        if (SUCCEEDED(manager->QueryInterface(IID_ITfSource, reinterpret_cast<void **>(source.put()))))
            source->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink *>(this),
                               &mgr_cookie_);
        WNDCLASSW wc{};
        wc.hInstance = module;
        wc.lpfnWndProc = proc;
        wc.lpszClassName = L"EShuRu.TsfBridge";
        RegisterClassW(&wc);
        window_ =
            CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, module, this);
        if (!window_) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Deactivate();
            return hr;
        }
        pipe_.start(std::filesystem::exists(module_dir(module) / L"EShuRu.exe")
                        ? module_dir(module)
                        : module_dir(module).parent_path(),
                    window_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Deactivate() override {
        end();
        pipe_.stop();
        if (manager_) {
            ComPtr<ITfKeystrokeMgr> keys;
            if (keys_advised_ && SUCCEEDED(manager_->QueryInterface(IID_ITfKeystrokeMgr,
                                                                    reinterpret_cast<void **>(keys.put()))))
                keys->UnadviseKeyEventSink(client_);
            ComPtr<ITfSource> source;
            if (mgr_cookie_ != TF_INVALID_COOKIE &&
                SUCCEEDED(manager_->QueryInterface(IID_ITfSource, reinterpret_cast<void **>(source.put()))))
                source->UnadviseSink(mgr_cookie_);
        }
        mgr_cookie_ = TF_INVALID_COOKIE;
        keys_advised_ = false;
        manager_.reset();
        if (window_) {
            MSG msg;
            while (PeekMessageW(&msg, window_, WM_ES_EVENT, WM_ES_EVENT, PM_REMOVE))
                delete reinterpret_cast<PipeEvent *>(msg.lParam);
            DestroyWindow(window_);
            window_ = nullptr;
        }
        UnregisterClassW(L"EShuRu.TsfBridge", module);
        eaten_.fill(false);
        shift_alone_ = false;
        ascii_ = false;
        engine_ready_ = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL foreground) override {
        if (!foreground)
            end();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext *c, WPARAM vk, LPARAM, BOOL *eaten) override {
        if (!eaten)
            return E_POINTER;
        *eaten = should_eat(c, vk);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext *c, WPARAM vk, LPARAM lp, BOOL *eaten) override {
        if (!eaten)
            return E_POINTER;
        *eaten = should_eat(c, vk);
        if (!*eaten)
            return S_OK;
        eaten_[vk] = true;
        if (vk == VK_SHIFT) {
            shift_alone_ = true;
            return S_OK;
        }
        shift_alone_ = false;
        try {
            *eaten = send_key(c, vk, lp);
        } catch (...) {
            end();
            *eaten = FALSE;
        }
        eaten_[vk] = *eaten;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext *, WPARAM vk, LPARAM, BOOL *eaten) override {
        if (!eaten)
            return E_POINTER;
        *eaten = vk < 256 && eaten_[vk];
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext *c, WPARAM vk, LPARAM lp, BOOL *eaten) override {
        if (!eaten)
            return E_POINTER;
        *eaten = vk < 256 && eaten_[vk];
        if (vk < 256)
            eaten_[vk] = false;
        if (vk == VK_SHIFT && shift_alone_) {
            shift_alone_ = false;
            try {
                send_key(c, VK_SHIFT, lp);
            } catch (...) {
                end();
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext *, REFGUID, BOOL *eaten) override {
        if (!eaten)
            return E_POINTER;
        *eaten = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnInitDocumentMgr(ITfDocumentMgr *) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnUninitDocumentMgr(ITfDocumentMgr *) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnSetFocus(ITfDocumentMgr *, ITfDocumentMgr *) override {
        end();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPushContext(ITfContext *) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPopContext(ITfContext *c) override {
        if (c == context_.get())
            end();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnEndEdit(ITfContext *, TfEditCookie, ITfEditRecord *record) override {
        if (!committing_) {
            BOOL changed = FALSE;
            record->GetSelectionStatus(&changed);
            if (changed)
                end();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnLayoutChange(ITfContext *context, int code, ITfContextView *) override {
        if (context == context_.get())
            post_layout();
        return S_OK;
    }
};
class Factory final : public IClassFactory {
    ULONG refs_ = 1;

  public:
    Factory() {
        objects++;
    }
    ~Factory() {
        objects--;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IClassFactory) {
            *out = this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs_;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID iid, void **out) override {
        if (outer)
            return CLASS_E_NOAGGREGATION;
        auto tip = new Tip;
        HRESULT hr = tip->QueryInterface(iid, out);
        tip->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock)
            locks++;
        else
            locks--;
        return S_OK;
    }
};
} // namespace
extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        module = h;
        DisableThreadLibraryCalls(h);
    }
    return TRUE;
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out) {
    if (clsid != CLSID_ES)
        return CLASS_E_CLASSNOTAVAILABLE;
    auto f = new Factory;
    auto result = f->QueryInterface(iid, out);
    f->Release();
    return result;
}
extern "C" HRESULT WINAPI DllCanUnloadNow() {
    return objects == 0 && locks == 0 ? S_OK : S_FALSE;
}
extern "C" HRESULT WINAPI DllRegisterServer() {
    using namespace es;
    std::wstring clsid = guid_string(CLSID_ES);
    wchar_t filename[32768]{};
    DWORD length = GetModuleFileNameW(module, filename, 32768);
    if (!length || length >= 32768)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    auto path = std::filesystem::path(filename);
    std::wstring reg = L"Software\\Classes\\CLSID\\" + clsid + L"\\InprocServer32";
    HKEY key;
    LONG error =
        RegCreateKeyExW(HKEY_LOCAL_MACHINE, reg.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(error);
    error = RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE *>(path.c_str()),
                           DWORD((path.wstring().size() + 1) * 2));
    const wchar_t *apartment = L"Apartment";
    if (error == ERROR_SUCCESS)
        error = RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ, reinterpret_cast<const BYTE *>(apartment),
                               DWORD((wcslen(apartment) + 1) * 2));
    RegCloseKey(key);
    if (error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(error);
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE)
        return init;
    HRESULT hr;
    {
        ComPtr<ITfInputProcessorProfiles> profiles;
        hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ITfInputProcessorProfiles, reinterpret_cast<void **>(profiles.put()));
        if (SUCCEEDED(hr))
            hr = profiles->Register(CLSID_ES);
        if (SUCCEEDED(hr)) {
            const wchar_t *name = L"言桥全拼";
            hr = profiles->AddLanguageProfile(CLSID_ES, 0x0804, PROFILE_ES, name, ULONG(wcslen(name)),
                                              path.c_str(), ULONG(path.wstring().size()), 0);
        }
        if (SUCCEEDED(hr)) {
            hr = profiles->EnableLanguageProfile(CLSID_ES, 0x0804, PROFILE_ES, TRUE);
        }
        if (SUCCEEDED(hr)) {
            ComPtr<ITfCategoryMgr> cat;
            hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                  reinterpret_cast<void **>(cat.put()));
            if (SUCCEEDED(hr))
                hr = cat->RegisterCategory(CLSID_ES, GUID_TFCAT_TIP_KEYBOARD, CLSID_ES);
        }
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return hr;
}
extern "C" HRESULT WINAPI DllUnregisterServer() {
    using namespace es;
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE)
        return init;
    HRESULT hr = S_OK;
    {
        ComPtr<ITfInputProcessorProfiles> p;
        hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ITfInputProcessorProfiles, reinterpret_cast<void **>(p.put()));
        if (SUCCEEDED(hr))
            hr = p->Unregister(CLSID_ES);
        ComPtr<ITfCategoryMgr> cat;
        HRESULT category = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                            IID_ITfCategoryMgr, reinterpret_cast<void **>(cat.put()));
        if (SUCCEEDED(category))
            category = cat->UnregisterCategory(CLSID_ES, GUID_TFCAT_TIP_KEYBOARD, CLSID_ES);
        if (SUCCEEDED(hr) && FAILED(category))
            hr = category;
    }
    auto key = L"Software\\Classes\\CLSID\\" + guid_string(CLSID_ES);
    LONG removed = RegDeleteTreeW(HKEY_LOCAL_MACHINE, key.c_str());
    if (SUCCEEDED(hr) && removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND)
        hr = HRESULT_FROM_WIN32(removed);
    if (SUCCEEDED(init))
        CoUninitialize();
    return hr;
}
