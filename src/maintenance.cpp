#include "common.hpp"
#include "registration.hpp"
#include <msctf.h>
#include <imm.h>
#include <iostream>
using namespace es;
namespace {
const CLSID service{0x5db324ae, 0x9ed8, 0x4e80, {0x9b, 0xce, 0x85, 0x20, 0xdf, 0xfa, 0x3c, 0x93}};
const GUID profile{0x6ade2191, 0x955d, 0x4bb3, {0xbb, 0x54, 0x8b, 0x9e, 0xe9, 0x41, 0xfa, 0x20}};
struct InstalledWindows {
    std::wstring executable;
    std::vector<DWORD> pids;
    bool close = false;
};
BOOL CALLBACK visit(HWND h, LPARAM data) {
    auto &state = *reinterpret_cast<InstalledWindows *>(data);
    wchar_t name[128]{};
    GetClassNameW(h, name, 128);
    if (std::wstring_view(name) != L"EShuRu.Broker")
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    wchar_t path[32768];
    DWORD size = 32768;
    if (!process || !QueryFullProcessImageNameW(process.value, 0, path, &size) ||
        _wcsicmp(path, state.executable.c_str()))
        return TRUE;
    state.pids.push_back(pid);
    if (state.close)
        PostMessageW(h, WM_CLOSE, 0, 0);
    return TRUE;
}
Json active_profile() {
    ComPtr<ITfInputProcessorProfileMgr> manager;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITfInputProcessorProfileMgr, reinterpret_cast<void **>(manager.put()));
    if (FAILED(hr))
        return Json{{"active_query_hr", unsigned(hr)}, {"active_is_eshuru", false}};
    TF_INPUTPROCESSORPROFILE current{};
    HRESULT active = manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &current);
    return Json{{"active_query_hr", unsigned(active)},
                {"active_is_eshuru", SUCCEEDED(active) && current.clsid == service}};
}
} // namespace
int main(int argc, char **argv) {
    ImmDisableIME(DWORD(-1));
    std::string mode = argc == 2 ? argv[1] : "";
    if (mode == "--register-input") {
        bool ok = ensure_input_registered();
        std::cout << Json{{"registered", ok}, {"tip", utf8(input_profile_tip())}}.dump() << '\n';
        return ok ? 0 : 1;
    }
    if (mode == "--unregister-input") {
        bool ok = unregister_input_profile();
        std::cout << Json{{"unregistered", ok}, {"tip", utf8(input_profile_tip())}}.dump() << '\n';
        return ok ? 0 : 1;
    }
    if (mode == "--doctor") {
        auto state = input_registration();
        Json report = {
            {"tip", utf8(input_profile_tip())},
            {"language_list", state.language_tip},
            {"profile_enabled", state.profile_enabled},
            {"com_x64", {{"registered", state.com_x64}, {"path", utf8(state.com_x64_path)}}},
            {"com_x86", {{"registered", state.com_x86}, {"path", utf8(state.com_x86_path)}}},
            {"scope", "Read-only per-user registration check; reads no API key or input content."}};
        HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(init) || init == RPC_E_CHANGED_MODE) {
            auto active = active_profile();
            for (auto it = active.begin(); it != active.end(); ++it)
                report[it.key()] = it.value();
            if (SUCCEEDED(init))
                CoUninitialize();
        } else {
            report["active_query_hr"] = unsigned(init);
            report["active_is_eshuru"] = false;
        }
        std::cout << report.dump(2) << '\n';
        return (state.language_tip && state.profile_enabled && (state.com_x64 || state.com_x86)) ? 0 : 1;
    }
    bool stop = mode == "--stop-installed";
    if (!stop && mode != "--inspect") {
        std::cerr << "Use --inspect, --doctor, --register-input, --unregister-input or --stop-installed.\n"
                     "Stop deactivates only EShuRu for this session and closes its installed broker; no "
                     "other applications are closed.\n";
        return 2;
    }
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init))
        return 3;
    int result = 0;
    {
        ComPtr<ITfInputProcessorProfileMgr> manager;
        HRESULT hr =
            CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                             IID_ITfInputProcessorProfileMgr, reinterpret_cast<void **>(manager.put()));
        if (FAILED(hr)) {
            std::cout << Json{{"error", "profile manager"}, {"hr", unsigned(hr)}}.dump();
            result = 3;
        } else {
            TF_INPUTPROCESSORPROFILE current{};
            HRESULT active = manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &current);
            Json report{{"active_query_hr", unsigned(active)},
                        {"active_is_eshuru", SUCCEEDED(active) && current.clsid == service}};
            if (stop) {
                // TF_IPPMF_FORSESSION, SDK flag; never changes the default profile or enable list.
                hr = manager->DeactivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0804, service, profile,
                                                nullptr, 0x20000000);
                report["deactivate_hr"] = unsigned(hr);
                if (FAILED(hr))
                    result = 4;
                else {
                    wchar_t program_files[32768]{};
                    GetEnvironmentVariableW(L"ProgramFiles", program_files, 32768);
                    InstalledWindows windows{
                        (std::filesystem::path(program_files) / L"EShuRu" / L"EShuRu.exe").wstring(),
                        {},
                        true};
                    EnumWindows(visit, reinterpret_cast<LPARAM>(&windows));
                    report["close_requested_pids"] = windows.pids;
                    report["scope"] = "Current-session EShuRu deactivation and graceful broker close only; "
                                      "check process state before upgrading";
                }
            }
            std::cout << report.dump() << '\n';
        }
    }
    CoUninitialize();
    return result;
}
