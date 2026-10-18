#include "app.hpp"
#include "sandbox.hpp"
#include <shellapi.h>
#include <imm.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // Owned test windows use the broker directly, not the user's registered TSF profile.
    if (GetEnvironmentVariableW(L"ES_SHURU_TEST_SCOPE", nullptr, 0))
        ImmDisableIME(DWORD(-1));
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    bool headless = false, settings = count == 1, deploy = false, sandbox = false, server = false;
    std::filesystem::path render;
    int scenario = 0;
    for (int i = 1; i < count; ++i) {
        const std::wstring arg = args[i];
        if (arg == L"--server")
            server = true;
        if (arg == L"--headless")
            headless = true;
        if (arg == L"--settings")
            settings = true;
        if (arg == L"--deploy")
            deploy = true;
        if (arg == L"--sandbox")
            sandbox = true;
        if (arg == L"--render" && i + 1 < count)
            render = args[++i];
        if ((arg == L"--render-candidates" || arg == L"--render-error") && i + 1 < count) {
            scenario = arg == L"--render-candidates" ? 1 : 2;
            render = args[++i];
        }
    }
    LocalFree(args);
    int code = 0;
    try {
        if (!deploy && render.empty()) {
            if (server && !es::auto_start_allowed()) {
                CoUninitialize();
                return 0;
            }
            if (!server && !es::set_auto_start_allowed(true)) {
                if (!headless)
                    MessageBoxW(nullptr, L"无法恢复言桥启动状态，请检查当前用户权限。", L"言桥",
                                MB_ICONERROR);
                CoUninitialize();
                return 5;
            }
        }
        if (sandbox) {
            code = es::run_sandbox(headless);
        } else if (deploy) {
            es::RimeEngine engine;
            code = engine.initialize(es::module_dir(), true) ? 0 : 2;
        } else if (!render.empty()) {
            es::App app;
            code = app.render(render, scenario);
        } else {
            auto name = L"Local\\EShuRu.Broker." + es::ipc_scope();
            es::Handle mutex(CreateMutexW(nullptr, FALSE, name.c_str()));
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                if (settings)
                    PostMessageW(FindWindowW(L"EShuRu.Broker", nullptr), WM_APP + 74, 0, 0);
                code = headless ? 4 : 0;
            } else {
                es::App app;
                code = app.run(headless, settings);
            }
        }
    } catch (...) {
        code = 3;
    }
    CoUninitialize();
    return code;
}
