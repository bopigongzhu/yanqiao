#include "registration.hpp"
namespace es {
namespace {
constexpr wchar_t kTip[] =
    L"0804:{5DB324AE-9ED8-4E80-9BCE-8520DFFA3C93}{6ADE2191-955D-4BB3-BB54-8B9EE941FA20}";
constexpr wchar_t kClsid[] = L"{5DB324AE-9ED8-4E80-9BCE-8520DFFA3C93}";
constexpr wchar_t kUserProfileKey[] = L"Control Panel\\International\\User Profile\\zh-Hans-CN";
constexpr wchar_t kCtfKey[] = L"Software\\Microsoft\\CTF\\TIP\\{5DB324AE-9ED8-4E80-9BCE-8520DFFA3C93}"
                               L"\\LanguageProfile\\0x00000804"
                               L"\\{6ADE2191-955D-4BB3-BB54-8B9EE941FA20}";
constexpr DWORD kIltUninstall = 0x00000001;
using InstallLayoutOrTipFn = BOOL(WINAPI *)(LPCWSTR, DWORD);

bool value_exists(HKEY root, const wchar_t *subkey, const wchar_t *name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, size = 0;
    LONG result = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}
bool profile_enabled() {
    DWORD value = 0, size = sizeof(value);
    LONG result =
        RegGetValueW(HKEY_CURRENT_USER, kCtfKey, L"Enable", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return result == ERROR_SUCCESS && value != 0;
}
void enable_profile() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kCtfKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return;
    DWORD value = 1;
    RegSetValueExW(key, L"Enable", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
    RegCloseKey(key);
}
bool read_com_path(DWORD view, std::wstring &path) {
    std::wstring subkey = std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + kClsid + L"\\InprocServer32";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey.c_str(), 0, KEY_READ | view, &key) != ERROR_SUCCESS)
        return false;
    wchar_t buffer[32768]{};
    DWORD size = sizeof(buffer), type = 0;
    LONG result = RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<BYTE *>(buffer), &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t) ||
        size > sizeof(buffer))
        return false;
    path.assign(buffer, size / sizeof(wchar_t));
    while (!path.empty() && path.back() == L'\0')
        path.pop_back();
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}
bool install_layout_or_tip(DWORD flags) {
    HMODULE module = LoadLibraryExW(L"input.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module)
        return false;
    auto function = reinterpret_cast<InstallLayoutOrTipFn>(GetProcAddress(module, "InstallLayoutOrTip"));
    bool ok = function && function(kTip, flags) != FALSE;
    FreeLibrary(module);
    return ok;
}
} // namespace
std::wstring input_profile_tip() {
    return kTip;
}
InputRegistration input_registration() {
    InputRegistration state;
    state.language_tip = value_exists(HKEY_CURRENT_USER, kUserProfileKey, kTip);
    state.profile_enabled = profile_enabled();
    state.com_x64 = read_com_path(KEY_WOW64_64KEY, state.com_x64_path);
    state.com_x86 = read_com_path(KEY_WOW64_32KEY, state.com_x86_path);
    return state;
}
bool ensure_input_registered() {
    auto state = input_registration();
    // Never add a tip whose COM server is missing; the keyboard would be dead.
    if (!state.com_x64 && !state.com_x86)
        return false;
    if (state.language_tip && state.profile_enabled)
        return true;
    // InstallLayoutOrTip can report FALSE even when it updated the language
    // list, and it may clear the per-user enable value. Trust the resulting
    // registry state instead of the return code.
    if (!state.language_tip)
        install_layout_or_tip(0);
    auto after = input_registration();
    if (!after.language_tip)
        return false;
    if (!after.profile_enabled) {
        enable_profile();
        after = input_registration();
    }
    return after.language_tip && after.profile_enabled;
}
bool unregister_input_profile() {
    install_layout_or_tip(kIltUninstall);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kUserProfileKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, kTip);
        RegCloseKey(key);
    }
    // Remove the per-user profile state as well; the COM registration itself is
    // removed separately by regsvr32 /unregister.
    RegDeleteTreeW(HKEY_CURRENT_USER, kCtfKey);
    return !value_exists(HKEY_CURRENT_USER, kUserProfileKey, kTip);
}
} // namespace es
