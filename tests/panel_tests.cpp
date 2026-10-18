#include "panel.hpp"
#include <shellscalingapi.h>
#include <cmath>
#include <iostream>
#include <imm.h>
using namespace es;
namespace es {
struct PanelTestAccess {
    static void paint(Panel &p) {
        SendMessageW(p.hwnd_, WM_PAINT, 0, 0);
    }
    static ID2D1HwndRenderTarget *target(Panel &p) {
        return p.target_.get();
    }
    static float scale(Panel &p) {
        return p.scale_;
    }
    static D2D1_RECT_F hit(Panel &p, int id) {
        for (auto &[r, action] : p.hits_)
            if (action == id)
                return r;
        throw std::runtime_error("missing hit region");
    }
    static D2D1_RECT_F candidate(Panel &p, size_t i) {
        return p.candidate_rects_.at(i);
    }
    static void discard_target(Panel &p) {
        p.target_.reset();
    }
};
} // namespace es
int main() {
    ImmDisableIME(DWORD(-1));
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int checks = 0;
    try {
        auto check = [&](bool ok, const char *label) {
            if (!ok)
                throw std::runtime_error(label);
            ++checks;
        };
        Panel p;
        InputBuffer buffer;
        buffer.set_preedit(L"ta", L"他", {L"他", L"她", L"它", L"塔", L"踏", L"他们", L"她们"}, 0);
        int action = 0;
        p.create([&](int a, int) { action = a; });
        auto view = PanelView{&buffer, L"就绪", L"", false, false, true, Json{{"theme", "light"}}};
        RECT anchor{100, 100, 100, 124};
        p.update(view, anchor);
        auto output = module_dir() / L"dpi-preview";
        std::filesystem::create_directories(output);
        for (UINT dpi : {96u, 120u, 144u, 192u, 144u, 96u}) {
            RECT suggested{};
            GetWindowRect(p.hwnd(), &suggested);
            SendMessageW(p.hwnd(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
            PanelTestAccess::paint(p);
            auto rt = PanelTestAccess::target(p);
            check(rt != nullptr, "real HWND Direct2D render target created");
            float x = 0, y = 0;
            rt->GetDpi(&x, &y);
            check(x == dpi && y == dpi, "render target matches changed window DPI");
            check(std::abs(rt->GetSize().width - 560) < 1, "logical panel width stays 560 DIPs");
            RECT client{};
            GetClientRect(p.hwnd(), &client);
            check(rt->GetPixelSize().width == UINT(client.right) &&
                      rt->GetPixelSize().height == UINT(client.bottom),
                  "render target uses actual client pixels, excluding border");
            auto click = [&](int id) {
                auto r = PanelTestAccess::hit(p, id);
                action = 0;
                auto px = int(std::lround((r.left + r.right) / 2 * dpi / 96.f));
                auto py = int(std::lround((r.top + r.bottom) / 2 * dpi / 96.f));
                SendMessageW(p.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(px, py));
                check(action == id, "physical click selects displayed action at current DPI");
            };
            click(Select + 6);
            click(Chinese);
            click(Settings);
            auto old_scale = PanelTestAccess::scale(p);
            PanelTestAccess::discard_target(p);
            PanelTestAccess::paint(p);
            PanelTestAccess::target(p)->GetDpi(&x, &y);
            check(x == dpi && old_scale == PanelTestAccess::scale(p), "device-loss recreation retains DPI");
            p.bitmap(output / (std::to_wstring(dpi) + L".bmp"), dpi);
        }
        // Use actual attached monitors as well as synthetic DPI-change messages.
        std::vector<RECT> work;
        EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR m, HDC, LPRECT, LPARAM data) -> BOOL {
                MONITORINFO info{sizeof(info)};
                if (GetMonitorInfoW(m, &info))
                    reinterpret_cast<std::vector<RECT> *>(data)->push_back(info.rcWork);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&work));
        Json monitors = Json::array();
        for (const auto &r : work) {
            anchor = {r.left + 40, r.top + 40, r.left + 40, r.top + 64};
            p.update(view, anchor);
            PanelTestAccess::paint(p);
            float x = 0, y = 0;
            PanelTestAccess::target(p)->GetDpi(&x, &y);
            check(x == GetDpiForWindow(p.hwnd()), "first update on target monitor uses its actual DPI");
            RECT outer{};
            GetWindowRect(p.hwnd(), &outer);
            check(outer.left >= r.left && outer.right <= r.right && outer.top >= r.top &&
                      outer.bottom <= r.bottom,
                  "panel stays within target monitor work area");
            monitors.push_back(Json{{"dpi", x}, {"work", {r.left, r.top, r.right, r.bottom}}});
        }
        std::cout << Json{{"checks", checks},
                          {"monitors", monitors},
                          {"result", "passed"},
                          {"scope", "Real hidden HWND rendering and clicks, synthetic DPI transitions plus "
                                    "attached monitors; no WeChat automation"}}
                         .dump()
                  << '\n';
    } catch (const std::exception &e) {
        std::cerr << "FAILED after " << checks << " checks: " << e.what() << '\n';
        CoUninitialize();
        return 1;
    }
    CoUninitialize();
}
