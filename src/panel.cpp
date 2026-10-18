#include "panel.hpp"
#include "resource.h"
#include <dwmapi.h>
#include <commctrl.h>
#include <fstream>
#include <shellscalingapi.h>
#include <cmath>
namespace es {
static D2D1_COLOR_F color(unsigned hex) {
    return D2D1::ColorF(hex);
}
// A "system" theme follows the current user's app theme preference.
static bool theme_is_dark(const Json &settings) {
    auto theme = settings.value("theme", "system");
    if (theme == "dark")
        return true;
    if (theme == "light")
        return false;
    DWORD light = 1, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light == 0;
}
// GDI takes 0x00BBGGRR; the design tokens are written as 0xRRGGBB.
static COLORREF cref(unsigned rgb) {
    return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}
// Everything in the settings window is described in DIP and scaled for its monitor.
static int dip(int value, UINT dpi) {
    return int(std::lround(value * double(dpi) / 96.0));
}
Panel::~Panel() {
    if (hwnd_)
        DestroyWindow(hwnd_);
}
void Panel::create(std::function<void(int, int)> action) {
    action_ = std::move(action);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.put());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown **>(dwrite_.put()));
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = proc;
    wc.lpszClassName = L"EShuRu.Panel";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName,
                            L"言桥输入面板", WS_POPUP | WS_BORDER, 0, 0, 560, 350, nullptr, nullptr,
                            wc.hInstance, this);
    DWORD corner = 2;
    DwmSetWindowAttribute(hwnd_, 33, &corner, sizeof(corner));
}
void Panel::hide() {
    if (hwnd_)
        ShowWindow(hwnd_, SW_HIDE);
}
void Panel::layout_candidates(float width) {
    candidate_rects_.clear();
    candidate_bottom_ = 167;
    if (!view_.buffer || !view_.buffer->pending())
        return;
    float x = 18, y = 167, row_height = 31;
    ComPtr<IDWriteTextFormat> format;
    dwrite_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13, L"zh-cn",
                              format.put());
    for (size_t i = 0; i < view_.buffer->candidates.size(); ++i) {
        auto label = std::to_wstring(i + 1) + L" " + view_.buffer->candidates[i];
        ComPtr<IDWriteTextLayout> layout;
        dwrite_->CreateTextLayout(label.c_str(), UINT32(label.size()), format.get(), width - 48, 2000,
                                  layout.put());
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        float bw = std::min(width - 36, std::max(50.f, metrics.widthIncludingTrailingWhitespace + 12));
        float bh = std::max(31.f, metrics.height + 10);
        if (x > 18 && x + bw > width - 18) {
            x = 18;
            y += row_height + 4;
            row_height = 31;
        }
        candidate_rects_.push_back({x, y, x + bw, y + bh});
        row_height = std::max(row_height, bh);
        candidate_bottom_ = std::max(candidate_bottom_, y + bh);
        x += bw + 4;
    }
}
void Panel::update(PanelView view, RECT anchor) {
    view_ = std::move(view);
    if (!view_.buffer || !view_.buffer->show_explanation || view_.buffer->explanation_loading ||
        !view_.buffer->explanation_error.empty())
        scroll_ = 0;
    anchor_ = anchor;
    position();
}
void Panel::resize_target() {
    if (!target_)
        return;
    RECT client{};
    GetClientRect(hwnd_, &client);
    target_->SetDpi(scale_ * 96, scale_ * 96);
    if (FAILED(target_->Resize(D2D1::SizeU(std::max(1L, client.right), std::max(1L, client.bottom)))))
        target_.reset();
}
void Panel::position(UINT dpi) {
    if (positioning_)
        return;
    positioning_ = true;
    MONITORINFO mi{sizeof(mi)};
    auto monitor = MonitorFromRect(&anchor_, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(monitor, &mi);
    if (!dpi) {
        UINT y = 96;
        if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi, &y)))
            dpi = GetDpiForWindow(hwnd_);
    }
    scale_ = (dpi ? dpi : 96) / 96.f;
    float width = view_.settings.value("compact", false) ? 440.f : 560.f;
    width = std::min(width, std::max(1.f, (mi.rcWork.right - mi.rcWork.left - 18) / scale_));
    layout_candidates(width);
    float extra = view_.buffer && view_.buffer->pending() ? candidate_bottom_ + 38 - 147 : 0;
    height_ = (view_.buffer && view_.buffer->show_explanation ? 620.f : 350.f) + extra;
    // Layout uses DIPs; the native window and render target use physical client pixels.
    RECT frame{0, 0, int(std::lround(width * scale_)), int(std::lround(height_ * scale_))};
    AdjustWindowRectExForDpi(&frame, WS_POPUP | WS_BORDER, FALSE,
                             WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, dpi ? dpi : 96);
    int w = frame.right - frame.left,
        h = std::min<int>(frame.bottom - frame.top, int(mi.rcWork.bottom - mi.rcWork.top - 16));
    height_ = h / scale_;
    int x =
        std::clamp(anchor_.left, mi.rcWork.left + 8, std::max(mi.rcWork.left + 8, mi.rcWork.right - w - 8));
    int y = anchor_.bottom + 8;
    if (y + h > mi.rcWork.bottom - 8)
        y = anchor_.top - h - 8;
    y = std::max<int>(y, int(mi.rcWork.top + 8));
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | (view_.headless ? 0 : SWP_SHOWWINDOW));
    RECT client{};
    GetClientRect(hwnd_, &client);
    height_ = client.bottom / scale_;
    resize_target();
    positioning_ = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Panel::paint() {
    PAINTSTRUCT ps;
    BeginPaint(hwnd_, &ps);
    if (!target_ && d2d_) {
        RECT r;
        GetClientRect(hwnd_, &r);
        auto properties = D2D1::RenderTargetProperties();
        properties.dpiX = properties.dpiY = scale_ * 96;
        d2d_->CreateHwndRenderTarget(properties,
                                     D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(r.right, r.bottom)),
                                     target_.put());
    }
    if (target_) {
        target_->SetDpi(scale_ * 96, scale_ * 96);
        target_->BeginDraw();
        draw(target_.get());
        if (target_->EndDraw() == D2DERR_RECREATE_TARGET)
            target_.reset();
    }
    EndPaint(hwnd_, &ps);
}
void Panel::draw(ID2D1RenderTarget *rt) {
    if (!view_.buffer || !dwrite_)
        return;
    auto &b = *view_.buffer;
    bool dark = theme_is_dark(view_.settings);
    unsigned bg = dark ? 0x222b29 : 0xffffff, ink = dark ? 0xeaf2ee : 0x1d302b,
             muted = dark ? 0xa7bbb0 : 0x61736b, accent = dark ? 0x9ad6b8 : 0x25634f,
             soft = dark ? 0x2d3028 : 0xf7f5ee, border = dark ? 0x3c4842 : 0xdfe6df;
    auto size = rt->GetSize();
    float w = size.width, h = size.height;
    rt->Clear(color(bg));
    hits_.clear();
    auto rect = [&](D2D1_RECT_F r, unsigned c, float radius = 0) {
        ComPtr<ID2D1SolidColorBrush> brush;
        rt->CreateSolidColorBrush(color(c), brush.put());
        if (radius)
            rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush.get());
        else
            rt->FillRectangle(r, brush.get());
    };
    auto text = [&](const std::wstring &s, D2D1_RECT_F r, float font, unsigned c, bool bold = false) {
        ComPtr<IDWriteTextFormat> format;
        dwrite_->CreateTextFormat(
            L"Microsoft YaHei UI", nullptr, bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, font, L"zh-cn", format.put());
        ComPtr<ID2D1SolidColorBrush> brush;
        rt->CreateSolidColorBrush(color(c), brush.put());
        rt->DrawText(s.c_str(), UINT32(s.size()), format.get(), r, brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    auto hit = [&](D2D1_RECT_F r, int id) { hits_.push_back({r, id}); };
    text(L"言桥", {20, 14, 90, 40}, 16, accent, true);
    text(b.ascii ? L"英 · 直输" : L"中 · 全拼", {94, 18, 220, 40}, 12, muted);
    text(view_.configured ? L"已配置" : L"未配置", {w - 118, 18, w - 52, 40}, 11, muted);
    text(L"设置", {w - 48, 17, w - 10, 41}, 12, accent);
    hit({w - 55, 8, w - 5, 44}, Settings);
    rect({0, 49, w, 50}, border);
    text(L"中文 · 句子编辑区", {20, 62, w - 80, 83}, 12, muted);
    text(std::to_wstring(character_count(b.source())) + L" / 300", {w - 80, 62, w - 12, 83}, 10, muted);
    std::wstring source = b.text.substr(0, b.caret) + L"│" + b.preview + b.text.substr(b.caret);
    text(source, {20, 91, w - 20, 139}, 16, ink);
    hit({20, 88, w - 20, 139}, Caret);
    if (b.pending()) {
        text(b.pinyin, {20, 143, w - 20, 164}, 12, accent);
        for (size_t i = 0; i < b.candidates.size(); i++) {
            if (i >= candidate_rects_.size())
                break;
            auto r = candidate_rects_[i];
            if (int(i) == b.selected)
                rect(r, soft, 5);
            text(std::to_wstring(i + 1) + L" " + b.candidates[i],
                 {r.left + 5, r.top + 4, r.right - 3, r.bottom}, 13, int(i) == b.selected ? accent : ink);
            hit(r, Select + int(i));
        }
        float y = candidate_bottom_ + 5;
        text(L"第 " + std::to_wstring(b.page + 1) + L" 页", {20, y, 100, y + 25}, 11, muted);
        text(L"上一页  - / PgUp", {w - 245, y, w - 130, y + 25}, 11, b.page > 0 ? accent : muted);
        text(L"下一页  = / PgDn", {w - 125, y, w - 10, y + 25}, 11, !b.last_page ? accent : muted);
        if (b.page > 0)
            hit({w - 245, y - 2, w - 130, y + 26}, PreviousPage);
        if (!b.last_page)
            hit({w - 125, y - 2, w - 10, y + 26}, NextPage);
    }
    float top = b.pending() ? candidate_bottom_ + 38 : 147.f;
    rect({0, top, w, top + 1}, border);
    text(L"ENGLISH", {20, top + 12, 160, top + 31}, 11, accent, true);
    text(view_.status, {w - 146, top + 12, w - 16, top + 32}, 11, muted);
    auto english = b.english;
    bool placeholder = english.empty();
    if (placeholder)
        english = view_.error.empty() ? L"输入中文，看看英语怎么说。" : view_.error;
    text(english, {20, top + 39, w - 20, top + 100}, placeholder ? 13.f : 19.f, placeholder ? muted : ink,
         !placeholder);
    float learnY = top + 104;
    text(b.show_explanation ? L"词句讲解  ∧" : L"词句讲解  ∨", {20, learnY, 210, learnY + 24}, 12,
         b.can_english() ? accent : muted);
    text(L"Ctrl J", {w - 67, learnY, w - 15, learnY + 25}, 10, muted);
    hit({15, learnY - 4, w - 15, learnY + 25}, Learn);
    if (!view_.error.empty() && view_.configured) {
        text(L"重试", {w - 61, top + 76, w - 13, top + 101}, 11, accent);
        hit({w - 73, top + 70, w - 7, top + 103}, Retry);
    }
    if (b.show_explanation) {
        float y = learnY + 35;
        D2D1_RECT_F r{16, y, w - 16, h - 66};
        rect(r, soft, 9);
        rt->PushAxisAlignedClip({22, y + 7, w - 22, h - 73}, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        y += 12 - float(scroll_);
        if (b.explanation.contains("items")) {
            for (auto &item : b.explanation["items"]) {
                text(wide(item.value("expression", "")), {30, y, w - 30, y + 25}, 14, accent, true);
                y += 28;
                auto meaning = wide(item.value("meaning_zh", ""));
                float mh = meaning.size() * 14 > (w - 60) ? 44.f : 25.f;
                text(meaning, {30, y, w - 30, y + mh}, 13, muted);
                y += mh + 12;
                if (item.contains("example_en") && item["example_en"].is_string()) {
                    text(wide(item["example_en"]), {30, y, w - 30, y + 42}, 13, ink);
                    y += 37;
                    text(wide(item["example_zh"]), {30, y, w - 30, y + 40}, 12, muted);
                    y += 40;
                }
            }
        } else {
            text(b.explanation_error.empty() ? L"正在整理词句讲解…" : b.explanation_error,
                 {30, y, w - 30, y + 65}, 13, muted);
            if (!b.explanation_error.empty() && !b.explanation_loading) {
                text(L"重新讲解", {30, y + 70, 160, y + 98}, 13, accent, true);
                hit({30, y + 68, 160, y + 100}, ExplainRetry);
            }
        }
        rt->PopAxisAlignedClip();
    }
    float bottom = h - 57;
    rect({0, bottom, w, bottom + 1}, border);
    if (!b.notice.empty())
        text(b.notice, {20, bottom - 22, w - 20, bottom - 1}, 10, muted);
    D2D1_RECT_F cn{16, bottom + 11, 154, h - 10}, en{w - 190, bottom + 11, w - 16, h - 10};
    rect(cn, soft, 7);
    rect(en, b.can_english() ? accent : soft, 7);
    text(L"输入中文  Enter", {cn.left + 10, cn.top + 7, cn.right, cn.bottom}, 12, ink, true);
    text(L"输入英文  Ctrl Enter", {en.left + 12, en.top + 7, en.right, en.bottom}, 12,
         b.can_english() ? (dark ? 0x142d22 : 0xffffff) : muted, true);
    hit(cn, Chinese);
    hit(en, English);
}
LRESULT CALLBACK Panel::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<Panel *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        self = static_cast<Panel *>(reinterpret_cast<CREATESTRUCT *>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        self->paint();
        return 0;
    case WM_LBUTTONUP: {
        float x = float(short(LOWORD(l))) / self->scale_, y = float(short(HIWORD(l))) / self->scale_;
        for (auto &[r, id] : self->hits_)
            if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
                if (id == Caret && self->view_.buffer && !self->view_.buffer->pending()) {
                    auto text = self->view_.buffer->text;
                    ComPtr<IDWriteTextFormat> fmt;
                    self->dwrite_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16,
                                                    L"zh-cn", fmt.put());
                    ComPtr<IDWriteTextLayout> layout;
                    self->dwrite_->CreateTextLayout(text.c_str(), UINT32(text.size()), fmt.get(),
                                                    r.right - r.left, 70, layout.put());
                    BOOL trailing = FALSE, inside = FALSE;
                    DWRITE_HIT_TEST_METRICS metrics{};
                    layout->HitTestPoint(x - r.left, y - r.top, &trailing, &inside, &metrics);
                    self->action_(Caret, int(metrics.textPosition + (trailing ? metrics.length : 0)));
                } else
                    self->action_(id, 0);
                break;
            }
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (self->view_.buffer && self->view_.buffer->show_explanation) {
            POINT point{short(LOWORD(l)), short(HIWORD(l))};
            ScreenToClient(h, &point);
            if (point.y / self->scale_ > 280 && point.y / self->scale_ < self->height_ - 66) {
                self->scroll_ = std::clamp(self->scroll_ - GET_WHEEL_DELTA_WPARAM(w) / 3, 0, 1000);
                InvalidateRect(h, nullptr, FALSE);
            }
        }
        return 0;
    case WM_DPICHANGED:
        self->position(HIWORD(w));
        return 0;
    case WM_SIZE:
        self->resize_target();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
void Panel::bitmap(const std::filesystem::path &path, UINT dpi) {
    float factor = (dpi ? dpi : 96) / 96.f;
    int w = int(std::lround((view_.settings.value("compact", false) ? 440 : 560) * factor)),
        h = int(std::lround(height_ * factor));
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void *pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bm = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    auto old = SelectObject(dc, bm);
    ComPtr<ID2D1DCRenderTarget> rt;
    auto prop =
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                     D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    prop.dpiX = prop.dpiY = dpi ? dpi : 96;
    if (SUCCEEDED(d2d_->CreateDCRenderTarget(&prop, rt.put()))) {
        RECT r{0, 0, w, h};
        rt->BindDC(dc, &r);
        rt->BeginDraw();
        draw(rt.get());
        rt->EndDraw();
        BITMAPFILEHEADER header{0x4d42, DWORD(54 + w * h * 4), 0, 0, 54};
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<char *>(&header), sizeof(header));
        f.write(reinterpret_cast<char *>(&info.bmiHeader), 40);
        f.write(static_cast<char *>(pixels), w * h * 4);
    }
    SelectObject(dc, old);
    DeleteObject(bm);
    DeleteDC(dc);
}

namespace {
// One row per control, laid out in DIP; the same table positions and repositions them at any DPI.
struct ControlLayout {
    int id, x, y, width, height;
};
constexpr int kSettingsWidth = 640, kSettingsHeight = 560, kSettingsPad = 24, kSettingsContent = 592;
constexpr DWORD kSettingsStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
constexpr ControlLayout kSettingsLayout[] = {
    {30, kSettingsPad, 22, kSettingsContent, 30},  {31, 24, 73, 592, 24},
    {10, 24, 103, 592, 33},                        {32, 24, 148, 592, 27},
    {33, 24, 202, 150, 25},                        {11, 190, 195, 220, 140},
    {12, 24, 250, 450, 28},                        {14, 24, 282, 450, 28},
    {34, 24, 330, 592, 55},                        {13, 24, 388, 592, 42},
    {21, 24, 450, 125, 35},                        {22, 173, 450, 125, 35},
    {20, 481, 450, 135, 35},                       {23, 24, 500, 160, 35},
};
} // namespace

struct SettingState {
    std::function<void()> saved;
    HFONT font = nullptr;
    HBRUSH background = nullptr;
    UINT dpi = 96;
    bool dark = false;
};
static void settings_layout(HWND h, SettingState *s) {
    for (auto &c : kSettingsLayout)
        if (HWND child = GetDlgItem(h, c.id))
            SetWindowPos(child, nullptr, dip(c.x, s->dpi), dip(c.y, s->dpi), dip(c.width, s->dpi),
                         dip(c.height, s->dpi), SWP_NOZORDER | SWP_NOACTIVATE);
}
static void settings_font(HWND h, SettingState *s) {
    HFONT previous = s->font;
    s->font = CreateFontW(-dip(16, s->dpi), 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                          0, L"Microsoft YaHei UI");
    for (auto &c : kSettingsLayout)
        if (HWND child = GetDlgItem(h, c.id))
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(s->font), TRUE);
    if (previous)
        DeleteObject(previous);
}
static void settings_theme(HWND h, SettingState *s) {
    s->dark = theme_is_dark(load_settings());
    if (s->background)
        DeleteObject(s->background);
    s->background = CreateSolidBrush(cref(s->dark ? 0x222b29 : 0xffffff));
    BOOL dark = s->dark;
    DwmSetWindowAttribute(h, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    // Owner-drawn buttons and the controls that take WM_CTLCOLOR* all need a repaint.
    RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
// Push buttons are owner-drawn so they follow the same palette as the input panel.
static void settings_button(const DRAWITEMSTRUCT *item, SettingState *s, bool primary) {
    auto dc = item->hDC;
    RECT r = item->rcItem;
    unsigned face = primary ? (s->dark ? 0x9ad6b8 : 0x25634f) : (s->dark ? 0x2d3028 : 0xf7f5ee);
    unsigned label = primary ? (s->dark ? 0x142d22 : 0xffffff) : (s->dark ? 0xeaf2ee : 0x1d302b);
    HBRUSH brush = CreateSolidBrush(cref(face));
    FillRect(dc, &r, brush);
    DeleteObject(brush);
    if (!primary) {
        HBRUSH edge = CreateSolidBrush(cref(s->dark ? 0x3c4842 : 0xdfe6df));
        FrameRect(dc, &r, edge);
        DeleteObject(edge);
    }
    wchar_t text[128]{};
    GetWindowTextW(item->hwndItem, text, 128);
    auto previous = SelectObject(dc, s->font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, cref(label));
    if (item->itemState & ODS_SELECTED)
        OffsetRect(&r, 0, dip(1, s->dpi));
    DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previous);
    if (item->itemState & ODS_FOCUS)
        DrawFocusRect(dc, &item->rcItem);
}
static LRESULT CALLBACK settings_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto s = reinterpret_cast<SettingState *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        s = static_cast<SettingState *>(reinterpret_cast<CREATESTRUCT *>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    if (!s)
        return DefWindowProcW(h, m, w, l);
    if (m == WM_CREATE) {
        if (UINT dpi = GetDpiForWindow(h))
            s->dpi = dpi;
        auto control = [&](const wchar_t *type, const wchar_t *title, DWORD style, int id) {
            return CreateWindowExW(type == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, type, title,
                                   WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, h,
                                   reinterpret_cast<HMENU>(INT_PTR(id)), nullptr, nullptr);
        };
        control(L"STATIC", L"言桥 · 连接与外观", 0, 30);
        control(L"STATIC", L"DeepSeek API Key（留空保留已保存的密钥）", 0, 31);
        SendMessageW(control(L"EDIT", L"", WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL, 10), EM_SETLIMITTEXT,
                     2048, 0);
        control(L"STATIC", L"密钥存于 Windows 凭据管理器。句子与讲解不保存。", 0, 32);
        control(L"STATIC", L"外观主题", 0, 33);
        HWND theme = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 11);
        for (auto text : {L"跟随系统", L"浅色", L"深色"})
            SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        control(L"BUTTON", L"使用紧凑面板", WS_TABSTOP | BS_AUTOCHECKBOX, 12);
        control(L"BUTTON", L"记住常用字词（仅保存在本机）", WS_TABSTOP | BS_AUTOCHECKBOX, 14);
        control(L"STATIC",
                L"Enter 输入中文    Ctrl+Enter 输入英文\r\nCtrl+J 展开讲解    Esc 取消当前组合或草稿", 0, 34);
        control(L"STATIC", read_key().empty() ? L"尚未配置翻译服务。" : L"已保存个人密钥。", 0, 13);
        control(L"BUTTON", L"删除密钥", WS_TABSTOP | BS_OWNERDRAW, 21);
        control(L"BUTTON", L"测试连接", WS_TABSTOP | BS_OWNERDRAW, 22);
        control(L"BUTTON", L"保存设置", WS_TABSTOP | BS_OWNERDRAW, 20);
        control(L"BUTTON", L"清除常用字词", WS_TABSTOP | BS_OWNERDRAW, 23);
        auto config = load_settings();
        auto selected = config.value("theme", "system");
        SendMessageW(theme, CB_SETCURSEL, selected == "dark" ? 2 : selected == "light" ? 1 : 0, 0);
        SendMessageW(GetDlgItem(h, 12), BM_SETCHECK,
                     config.value("compact", false) ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(GetDlgItem(h, 14), BM_SETCHECK,
                     config.value("remember_words", true) ? BST_CHECKED : BST_UNCHECKED, 0);
        settings_font(h, s);
        settings_theme(h, s);
        settings_layout(h, s);
        return 0;
    }
    if (m == WM_DRAWITEM) {
        auto item = reinterpret_cast<DRAWITEMSTRUCT *>(l);
        if (item->CtlType == ODT_BUTTON) {
            settings_button(item, s, item->CtlID == 20);
            return TRUE;
        }
        return FALSE;
    }
    // A custom window class is not a dialog, so IsDialogMessage has to be told which button
    // Enter activates. Without this, Enter does nothing in the settings window.
    if (m == DM_GETDEFID)
        return MAKELRESULT(20, DC_HASDEFID);
    if (m == WM_ERASEBKGND) {
        RECT client{};
        GetClientRect(h, &client);
        FillRect(reinterpret_cast<HDC>(w), &client, s->background);
        return 1;
    }
    if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT || m == WM_CTLCOLORBTN) {
        auto dc = reinterpret_cast<HDC>(w);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, cref(s->dark ? 0xeaf2ee : 0x1d302b));
        SetBkColor(dc, cref(s->dark ? 0x222b29 : 0xffffff));
        return reinterpret_cast<LRESULT>(s->background);
    }
    if (m == WM_DPICHANGED) {
        s->dpi = HIWORD(w);
        auto suggested = reinterpret_cast<RECT *>(l);
        SetWindowPos(h, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        settings_font(h, s);
        settings_layout(h, s);
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    }
    if (m == WM_COMMAND) {
        HWND key = GetDlgItem(h, 10), status = GetDlgItem(h, 13);
        if (LOWORD(w) == 20) {
            int n = GetWindowTextLengthW(key);
            std::wstring value(n + 1, L'\0');
            GetWindowTextW(key, value.data(), n + 1);
            value.resize(n);
            if (n && !save_key(utf8(value))) {
                SetWindowTextW(status, L"密钥未能保存，请检查格式。");
                return 0;
            }
            SecureZeroMemory(value.data(), value.size() * 2);
            SetWindowTextW(key, L"");
            auto i = SendMessageW(GetDlgItem(h, 11), CB_GETCURSEL, 0, 0);
            save_settings(
                Json{{"theme", i == 2   ? "dark"
                               : i == 1 ? "light"
                                        : "system"},
                     {"compact", SendMessageW(GetDlgItem(h, 12), BM_GETCHECK, 0, 0) == BST_CHECKED},
                     {"remember_words", SendMessageW(GetDlgItem(h, 14), BM_GETCHECK, 0, 0) == BST_CHECKED}});
            s->saved();
            settings_theme(h, s);
            SetWindowTextW(status, L"设置已保存。可切换到言桥输入法开始使用。");
        }
        if (LOWORD(w) == 21) {
            delete_key();
            s->saved();
            SetWindowTextW(status, L"密钥已删除，中文输入仍可使用。");
        }
        if (LOWORD(w) == 22) {
            if (GetWindowTextLengthW(key)) {
                SetWindowTextW(status, L"请先保存刚输入的密钥，再测试连接。");
            } else {
                SetWindowTextW(status, L"正在用固定短句测试连接，会产生少量 API 用量…");
                PostMessageW(FindWindowW(L"EShuRu.Broker", nullptr), WM_APP + 75, 0, 0);
            }
        }
        if (LOWORD(w) == 23 &&
            MessageBoxW(h, L"清除所有已记住的常用字词？此操作无法撤销，不影响密钥和外观设置。",
                        L"清除常用字词", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
            SetWindowTextW(status, L"正在清除常用字词…");
            PostMessageW(FindWindowW(L"EShuRu.Broker", nullptr), WM_APP + 76, 0, 0);
        }
        return 0;
    }
    if (m == WM_DESTROY) {
        if (s->font)
            DeleteObject(s->font);
        if (s->background)
            DeleteObject(s->background);
        delete s;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
void show_settings(HWND owner, std::function<void()> saved) {
    auto existing = FindWindowW(L"EShuRu.Settings", nullptr);
    if (existing) {
        ShowWindow(existing, SW_SHOW);
        SetForegroundWindow(existing);
        return;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = settings_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"EShuRu.Settings";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!wc.hIcon)
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassW(&wc);
    // Open on the monitor the user is looking at, at that monitor's scale.
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    UINT dpi = 96, ignored = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi, &ignored)))
        dpi = 96;
    RECT frame{0, 0, dip(kSettingsWidth, dpi), dip(kSettingsHeight, dpi)};
    AdjustWindowRectExForDpi(&frame, kSettingsStyle, FALSE, 0, dpi);
    int width = frame.right - frame.left, height = frame.bottom - frame.top;
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    int x = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - width) / 2;
    int y = info.rcWork.top + ((info.rcWork.bottom - info.rcWork.top) - height) / 2;
    auto state = new SettingState{std::move(saved)};
    state->dpi = dpi;
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"言桥设置", kSettingsStyle, x, y, width, height, owner,
                             nullptr, wc.hInstance, state);
    if (h)
        ShowWindow(h, SW_SHOW);
    else
        delete state;
}
} // namespace es
