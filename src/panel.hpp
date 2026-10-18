#pragma once
#include "core.hpp"
#include <d2d1.h>
#include <dwrite.h>
namespace es {
enum Action {
    Chinese = 1,
    English,
    Learn,
    Settings,
    Retry,
    Caret,
    ExplainRetry,
    PreviousPage,
    NextPage,
    Select = 100
};
struct PanelView {
    InputBuffer *buffer = nullptr;
    std::wstring status;
    std::wstring error;
    bool configured = false, loading = false, headless = false;
    Json settings;
};
class Panel {
    friend struct PanelTestAccess;
    HWND hwnd_ = nullptr;
    ComPtr<ID2D1Factory> d2d_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    PanelView view_;
    std::function<void(int, int)> action_;
    std::vector<std::pair<D2D1_RECT_F, int>> hits_;
    float scale_ = 1;
    float height_ = 350;
    int scroll_ = 0;
    RECT anchor_{};
    std::vector<D2D1_RECT_F> candidate_rects_;
    float candidate_bottom_ = 167;
    bool positioning_ = false;
    void position(UINT dpi = 0);
    void resize_target();
    void layout_candidates(float width);
    void paint();
    void draw(ID2D1RenderTarget *rt);
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);

  public:
    ~Panel();
    void create(std::function<void(int, int)> action);
    void update(PanelView view, RECT anchor);
    void hide();
    void move(RECT anchor) {
        anchor_ = anchor;
        position();
    }
    HWND hwnd() const {
        return hwnd_;
    }
    void bitmap(const std::filesystem::path &path, UINT dpi = 96);
};
void show_settings(HWND owner, std::function<void()> saved);
} // namespace es
