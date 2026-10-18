#include "ai.hpp"
#include <iostream>
using namespace es;
static bool finished = false;
static Json report;
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ES_AI) {
        std::unique_ptr<AiResult> r(reinterpret_cast<AiResult *>(l));
        if (r->complete) {
            report = {{"complete", true},
                      {"success", r->error.empty()},
                      {"elapsed_ms", r->elapsed_ms},
                      {"error", r->error},
                      {"items_count", r->items.contains("items") ? r->items["items"].size() : 0},
                      {"scope", "One live explanation request for a fixed synthetic sentence. No user text "
                                "or API key recorded."}};
            finished = true;
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
int main() {
    auto key = read_key();
    bool configured = !key.empty();
    SecureZeroMemory(key.data(), key.size());
    key.clear();
    if (!configured) {
        std::cout << "{\"skipped\":true,\"reason\":\"No saved API key\"}" << std::endl;
        return 2;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.lpszClassName = L"EShuRu.ApiProbe";
    wc.hInstance = GetModuleHandleW(nullptr);
    RegisterClassW(&wc);
    HWND h =
        CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    {
        AiWorker worker(h);
        worker.submit(
            AiRequest{"synthetic_probe", 1, "要不我们改天再约？", "Can we take a rain check?", true, 1});
        auto started = Clock::now();
        MSG msg;
        while (!finished && Clock::now() - started < std::chrono::seconds(25)) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                DispatchMessageW(&msg);
            if (!finished)
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        }
        worker.cancel();
    }
    MSG msg;
    while (PeekMessageW(&msg, h, WM_ES_AI, WM_ES_AI, PM_REMOVE))
        delete reinterpret_cast<AiResult *>(msg.lParam);
    DestroyWindow(h);
    if (!finished)
        report = {{"success", false}, {"error", "probe deadline"}};
    std::cout << report.dump() << std::endl;
    return report.value("success", false) ? 0 : 1;
}
