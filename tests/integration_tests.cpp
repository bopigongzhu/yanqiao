#include "ipc.hpp"
#include "frequency.hpp"
#include <iostream>
#include <fstream>
#include <psapi.h>
#include <msctf.h>
#include <imm.h>
using namespace es;
struct TestHost {
    HWND hwnd{};
    PipeClient client;
    std::vector<Json> events;
    std::shared_ptr<Channel> latest_peer;
    static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<TestHost *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<TestHost *>(reinterpret_cast<CREATESTRUCT *>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (m == WM_ES_EVENT && self) {
            std::unique_ptr<PipeEvent> e(reinterpret_cast<PipeEvent *>(l));
            if (e->message.value("type", "") == "ready")
                self->latest_peer = e->peer;
            self->events.push_back(std::move(e->message));
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }
    void start() {
        WNDCLASSW c{};
        c.lpfnWndProc = proc;
        c.lpszClassName = L"EShuRu.TestHost";
        c.hInstance = GetModuleHandleW(nullptr);
        RegisterClassW(&c);
        hwnd = CreateWindowW(c.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, c.hInstance, this);
        client.start(module_dir(), hwnd);
    }
    void pump() {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    bool wait(const std::function<bool()> &predicate, int milliseconds = 5000) {
        auto start = Clock::now();
        while (Clock::now() - start < std::chrono::milliseconds(milliseconds)) {
            pump();
            if (predicate())
                return true;
            Sleep(2);
        }
        return false;
    }
    Json last(std::string type) {
        for (auto it = events.rbegin(); it != events.rend(); ++it)
            if (it->value("type", "") == type)
                return *it;
        return Json::object();
    }
    ~TestHost() {
        client.stop();
        pump();
        DestroyWindow(hwnd);
    }
};
int main() {
    if (!GetEnvironmentVariableW(L"ES_SHURU_TEST_SCOPE", nullptr, 0)) {
        auto scope = L"exit" + std::to_wstring(GetCurrentProcessId()) + std::to_wstring(now_ms());
        SetEnvironmentVariableW(L"ES_SHURU_TEST_SCOPE", scope.c_str());
    }
    set_auto_start_allowed(true);
    ImmDisableIME(DWORD(-1));
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<std::string> checks;
    PROCESS_INFORMATION server{};
    int code = 0;
    try {
        auto check = [&](bool ok, const char *name) {
            if (!ok)
                throw std::runtime_error(name);
            checks.push_back(name);
        };
        std::wstring exe = (module_dir() / L"EShuRu.exe").wstring(),
                     cmd = L"\"" + exe + L"\" --server --headless";
        STARTUPINFOW si{sizeof(si)};
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        check(CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                             module_dir().c_str(), &si, &server),
              "Headless broker starts");
        CloseHandle(server.hThread);
        Sleep(800);
        check(WaitForSingleObject(server.hProcess, 0) == WAIT_TIMEOUT, "Test owns a fresh broker");
        TestHost host;
        host.start();
        check(host.wait([&] { return host.last("ready").value("engine", false); }),
              "Authenticated IPC and real engine ready");
        auto id = session_id();
        uint64_t seq = 0;
        auto key = [&](int vk, std::string character = "", bool ctrl = false) {
            host.client.send(Json{{"type", "key"},
                                  {"session", id},
                                  {"seq", ++seq},
                                  {"vk", vk},
                                  {"char", character},
                                  {"ctrl", ctrl},
                                  {"shift", false}});
        };
        key('N', "n");
        key('I', "i");
        check(host.wait([&] {
            return host.last("state").value("pending", false) &&
                   !host.last("state").value("candidates", Json::array()).empty();
        }),
              "Actual pinyin candidates cross IPC");
        check(host.last("commit").empty(), "Typing does not commit to host");
        key(VK_SPACE, " ");
        check(host.wait([&] {
            auto s = host.last("state");
            return s.value("active", false) && !s.value("pending", true);
        }),
              "Space only accepts candidate into buffer");
        check(host.last("commit").empty(), "Word selection does not insert host text");
        auto original_state = host.last("state");
        auto move_anchor = [&](uint64_t serial, bool visible, Json rect, std::string session) {
            host.client.send(Json{{"type", "anchor"},
                                  {"session", session},
                                  {"layout_seq", serial},
                                  {"visible", visible},
                                  {"anchor", rect}});
        };
        move_anchor(1, true, Json::array({400, 200, 400, 224}), id);
        check(host.wait([&] { return host.last("layout").value("layout_seq", uint64_t(0)) == 1; }),
              "Geometry update accepted for active session");
        check(host.last("state") == original_state,
              "Geometry never changes draft revision or candidate state");
        move_anchor(2, false, Json::array(), id);
        check(host.wait([&] { return host.last("layout").value("layout_seq", uint64_t(0)) == 2; }) &&
                  !host.last("layout")["visible"].get<bool>(),
              "Clipped caret hides geometry without losing draft");
        move_anchor(3, true, Json::array({450, 300, 450, 324}), id);
        check(host.wait([&] { return host.last("layout").value("layout_seq", uint64_t(0)) == 3; }) &&
                  host.last("layout")["visible"].get<bool>(),
              "Visible caret restores geometry");
        auto current_layout = host.last("layout");
        move_anchor(2, false, Json::array(), id);
        move_anchor(4, true, Json::array({400, 250, 390, 220}), id);
        move_anchor(99, true, Json::array({400, 200, 400, 224}), "stale-session");
        Sleep(80);
        host.pump();
        check(host.last("layout") == current_layout,
              "Out-of-order, invalid and stale-session geometry ignored");
        check(host.last("commit").empty(), "Geometry changes never submit text");
        auto source = host.last("state").value("source", "");
        key(VK_RETURN);
        check(host.wait([&] { return !host.last("commit").empty(); }),
              "Enter requests whole-sentence commit");
        auto commit = host.last("commit");
        check(commit["text"] == source, "Commit text matches selected Chinese");
        host.client.send(
            Json{{"type", "ack"}, {"session", id}, {"revision", commit["revision"]}, {"ok", true}});
        check(host.wait([&] { return !host.last("committed").empty(); }), "Commit acknowledged exactly once");
        check(host.wait([&] { return !host.last("state").value("active", true); }), "Committed draft clears");
        host.events.clear();
        move_anchor(100, true, Json::array({500, 200, 500, 224}), id);
        Sleep(60);
        host.pump();
        check(host.last("layout").empty(), "Late geometry cannot resurrect committed draft");
        host.events.clear();
        key('W', "w");
        key('O', "o");
        check(host.wait([&] { return host.last("state").value("pending", false); }), "New draft starts");
        key(VK_RETURN, "", true);
        Sleep(80);
        host.pump();
        check(host.last("commit").empty(), "Ctrl Enter rejects unfinished translation");
        host.client.send(Json{{"type", "end"}, {"session", id}});
        Sleep(50);
        host.pump();
        host.events.clear();
        id = session_id();
        seq = 0;
        auto settled = [&] {
            return host.wait([&] { return host.last("state").value("seq", uint64_t(0)) == seq; });
        };
        // A later-page word must not learn from failed commits, but successful acknowledgements train it.
        for (int attempt = 0; attempt < 4; ++attempt) {
            key('T', "t");
            key('A', "a");
            check(settled(), "Frequency draft ready");
            auto candidates = host.last("state")["candidates"];
            int selected = -1;
            for (size_t i = 0; i < candidates.size(); ++i)
                if (candidates[i] == utf8(L"她"))
                    selected = int(i);
            check(selected >= 0, "Frequency target present");
            key('1' + selected, std::to_string(selected + 1));
            check(settled(), "Frequency candidate selected");
            host.events.clear();
            key(VK_RETURN);
            check(host.wait([&] { return !host.last("commit").empty(); }), "Frequency commit requested");
            auto request = host.last("commit");
            Json ack{
                {"type", "ack"}, {"session", id}, {"revision", request["revision"]}, {"ok", attempt != 0}};
            host.client.send(ack);
            host.client.send(ack); // duplicate must be ignored
            Sleep(450);
            host.pump();
            FrequencyStore saved(config_dir() / (L"test-frequency-" + ipc_scope() + L".dat"));
            auto matches = saved.matching("ta");
            if (attempt < 2)
                check(matches.empty(), "Failure and single choice do not promote");
            else {
                auto found = std::find_if(matches.begin(), matches.end(),
                                          [](auto &v) { return v.first.text == utf8(L"她"); });
                check(found != matches.end() && found->second <= attempt * 3.0,
                      "Successful acknowledgement trains once despite duplicate");
            }
            key(VK_ESCAPE);
            check(settled(), "Frequency draft ends");
        }
        host.events.clear();
        key('T', "t");
        key('A', "a");
        check(settled() && host.last("state")["candidates"].size() == 7, "Seven real candidates cross IPC");
        auto first_page = host.last("state")["candidates"];
        key(VK_OEM_PLUS, "=");
        check(settled() && host.last("state").value("page", -1) == 1,
              "Equals pages forward without inserting punctuation");
        key(VK_OEM_MINUS, "-");
        check(settled() && host.last("state")["candidates"] == first_page,
              "Minus pages back to exact first page");
        key(VK_OEM_MINUS, "-");
        check(settled() && host.last("state").value("page", -1) == 0,
              "Previous on first page stays on first page");
        for (int i = 0; i < 9; ++i)
            key(VK_DOWN);
        check(settled() && host.last("state").value("page", -1) == 0, "Arrow selection does not auto-page");
        key('7', "7");
        check(settled() && !host.last("state").value("pending", true) &&
                  host.last("state")["source"] == first_page[6],
              "Digit seven selects correct candidate into buffer");
        check(host.last("commit").empty(), "Paging and selecting never commit early");
        key(VK_ESCAPE);
        check(settled() && !host.last("state").value("active", true), "Cancel selected draft");
        key('M', "m");
        key('Z', "z");
        check(settled() && host.last("state").value("source", "") == utf8(L"名字"),
              "Abbreviation mz works through real broker");
        host.client.send(Json{{"type", "end"}, {"session", id}});
        Sleep(50);
        host.pump();
        // Measure a quiet, initialized broker; no key, network or rendered popup is involved.
        FILETIME ct, et, k1, u1, k2, u2;
        GetProcessTimes(server.hProcess, &ct, &et, &k1, &u1);
        auto t = Clock::now();
        Sleep(10000);
        GetProcessTimes(server.hProcess, &ct, &et, &k2, &u2);
        auto ticks = [](FILETIME f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
        double seconds = std::chrono::duration<double>(Clock::now() - t).count();
        auto cpus = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        double cpu = double(ticks(k2) + ticks(u2) - ticks(k1) - ticks(u1)) / 1e7 / seconds / cpus * 100;
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        GetProcessMemoryInfo(server.hProcess, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                             sizeof(memory));
        for (int cycle = 0; cycle < 3; ++cycle) {
            host.events.clear();
            auto old = host.latest_peer;
            check(host.client.current(old), "Current connection identity accepted");
            TerminateProcess(server.hProcess, 0);
            WaitForSingleObject(server.hProcess, 3000);
            CloseHandle(server.hProcess);
            server.hProcess = nullptr;
            check(host.wait([&] { return !host.last("disconnected").empty(); }), "Broker death delivered");
            check(!host.client.current(old), "Dead connection identity rejected");
            check(host.wait(
                      [&] { return host.latest_peer != old && host.last("ready").value("engine", false); },
                      10000),
                  "Client automatically launches and reconnects broker");
            server.hProcess = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, host.latest_peer->peer_pid);
            check(server.hProcess != nullptr, "Replacement test broker owned for cleanup");
            id = session_id();
            seq = 0;
            host.events.clear();
            key('N', "n");
            key('I', "i");
            check(settled() && host.last("state").value("pending", false),
                  "New session accepts input after reconnect");
            check(host.last("commit").empty(), "Reconnect never replays old commit");
            key(VK_ESCAPE);
            check(settled(), "Recovered session cancels");
        }
        struct CloseTarget {
            DWORD pid;
            HWND hwnd = nullptr;
        } close{host.latest_peer->peer_pid};
        EnumWindows(
            [](HWND h, LPARAM data) -> BOOL {
                auto &target = *reinterpret_cast<CloseTarget *>(data);
                DWORD pid = 0;
                GetWindowThreadProcessId(h, &pid);
                wchar_t name[128]{};
                GetClassNameW(h, name, 128);
                if (pid == target.pid && std::wstring_view(name) == L"EShuRu.Broker")
                    target.hwnd = h;
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&close));
        check(close.hwnd != nullptr, "Locate owned broker for user-style close");
        key('N', "n");
        key('I', "i");
        key(VK_SPACE, " ");
        check(settled() && host.last("state").value("active", false) &&
                  !host.last("state").value("pending", true),
              "Draft selected before clear");
        auto selected_before_clear = host.last("state").value("source", "");
        host.events.clear();
        PostMessageW(close.hwnd, WM_APP + 76, 0, 0);
        check(host.wait([&] {
            return host.last("state").value("source", "") == selected_before_clear &&
                   host.last("state").value("active", false);
        }),
              "Clearing frequency preserves selected draft and refreshes host state");
        key(VK_RETURN);
        check(host.wait([&] { return !host.last("commit").empty(); }), "Cleared draft still commits");
        auto cleared_commit = host.last("commit");
        host.client.send(
            Json{{"type", "ack"}, {"session", id}, {"revision", cleared_commit["revision"]}, {"ok", true}});
        check(host.wait([&] { return !host.last("committed").empty(); }), "Cleared draft acknowledged");
        host.events.clear();
        PostMessageW(close.hwnd, WM_CLOSE, 0, 0);
        check(WaitForSingleObject(server.hProcess, 5000) == WAIT_OBJECT_0, "Explicit close exits broker");
        {
            auto frequency_path = config_dir() / (L"test-frequency-" + ipc_scope() + L".dat");
            FrequencyStore saved(frequency_path);
            check(saved.size() == 0 && !saved.failed(),
                  "Post-clear acknowledgement cannot relearn old spans");
            frequency_path += L".bak";
            check(!std::filesystem::exists(frequency_path), "Clear removes backup through actual broker");
        }
        check(!auto_start_allowed(), "Explicit exit persists pause state before disconnect");
        check(host.wait([&] { return !host.last("paused").empty(); }),
              "Connected host receives paused state");
        check(!host.wait([&] { return !host.last("ready").empty(); }, 6200),
              "Explicit exit does not restart after retry interval");
        check(!launch_companion(module_dir()), "Client launch gate refuses while paused");
        PROCESS_INFORMATION blocked{};
        check(CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                             module_dir().c_str(), &si, &blocked),
              "Probe legacy client automatic server launch");
        CloseHandle(blocked.hThread);
        auto blocked_wait = WaitForSingleObject(blocked.hProcess, 3000);
        DWORD blocked_code = 999;
        GetExitCodeProcess(blocked.hProcess, &blocked_code);
        if (blocked_wait != WAIT_OBJECT_0)
            TerminateProcess(blocked.hProcess, 1);
        CloseHandle(blocked.hProcess);
        check(blocked_wait == WAIT_OBJECT_0 && blocked_code == 0,
              "Server entry rejects legacy relaunch while paused");
        CloseHandle(server.hProcess);
        server.hProcess = nullptr;
        auto reopen = L"\"" + exe + L"\" --headless";
        check(CreateProcessW(exe.c_str(), reopen.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                             module_dir().c_str(), &si, &server),
              "Manual launch resumes broker");
        CloseHandle(server.hThread);
        check(host.wait([&] { return host.last("ready").value("engine", false); }),
              "Existing host reconnects after explicit resume");
        check(auto_start_allowed(), "Manual launch clears pause state");
        id = session_id();
        seq = 0;
        host.events.clear();
        key('N', "n");
        key('I', "i");
        check(settled() && host.last("state").value("pending", false), "Typing works after manual resume");
        check(host.last("commit").empty(), "Resume does not replay a previous draft");
        HMODULE dll = LoadLibraryExW((module_dir() / L"EShuRuTip.dll").c_str(), nullptr,
                                     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        check(dll != nullptr, "Native TSF DLL loads");
        using GetFactory = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
        auto factoryFn = reinterpret_cast<GetFactory>(GetProcAddress(dll, "DllGetClassObject"));
        check(factoryFn != nullptr, "COM factory export exists");
        CLSID clsid{0x5db324ae, 0x9ed8, 0x4e80, {0x9b, 0xce, 0x85, 0x20, 0xdf, 0xfa, 0x3c, 0x93}};
        IClassFactory *factory = nullptr;
        check(SUCCEEDED(factoryFn(clsid, IID_IClassFactory, reinterpret_cast<void **>(&factory))),
              "TSF COM factory creates");
        ITfTextInputProcessor *processor = nullptr;
        check(SUCCEEDED(factory->CreateInstance(nullptr, IID_ITfTextInputProcessor,
                                                reinterpret_cast<void **>(&processor))),
              "TSF processor implements required interface");
        processor->Release();
        factory->Release();
        using CanUnload = HRESULT(WINAPI *)();
        auto unload = reinterpret_cast<CanUnload>(GetProcAddress(dll, "DllCanUnloadNow"));
        check(unload && unload() == S_OK, "TSF DLL lifetime releases cleanly");
        FreeLibrary(dll);
        Json report = {
            {"checks", checks},
            {"passed", checks.size()},
            {"private_MiB", memory.PrivateUsage / 1048576.0},
            {"working_set_MiB", memory.WorkingSetSize / 1048576.0},
            {"idle_cpu_machine_percent", cpu},
            {"idle_sample_seconds", seconds},
            {"logical_processors", cpus},
            {"scope", "Headless native broker + real dictionary, no cloud requests or host-injected TSF. Not "
                      "production compatibility or complete resource acceptance."}};
        std::ofstream(module_dir() / L"integration-report.json") << report.dump(2);
        std::cout << report.dump() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        code = 1;
    }
    if (server.hProcess) {
        TerminateProcess(server.hProcess, 0);
        WaitForSingleObject(server.hProcess, 3000);
        CloseHandle(server.hProcess);
    }
    CoUninitialize();
    set_auto_start_allowed(true);
    return code;
}
