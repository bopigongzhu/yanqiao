#include "common.hpp"
#include <sddl.h>
#include <wincred.h>
#include <shlobj.h>
#include <fstream>

namespace es {
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n)
        return {};
    std::wstring v(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), v.data(), n);
    return v;
}
std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0, nullptr,
                                nullptr);
    if (!n)
        return {};
    std::string v(n, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), v.data(), n, nullptr,
                        nullptr);
    return v;
}
std::filesystem::path module_dir(HMODULE module) {
    std::wstring path(32768, L'\0');
    DWORD n = GetModuleFileNameW(module, path.data(), DWORD(path.size()));
    path.resize(n);
    return std::filesystem::path(path).parent_path();
}
std::filesystem::path config_dir() {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)))
        throw std::runtime_error("LocalAppData unavailable");
    std::filesystem::path dir = std::filesystem::path(p) / L"EShuRu";
    CoTaskMemFree(p);
    std::filesystem::create_directories(dir);
    return dir;
}
std::wstring user_sid() {
    Handle token;
    HANDLE h = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &h))
        return {};
    token.reset(h);
    DWORD n = 0;
    GetTokenInformation(h, TokenUser, nullptr, 0, &n);
    std::vector<BYTE> data(n);
    if (!GetTokenInformation(h, TokenUser, data.data(), n, &n))
        return {};
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid, &sid))
        return {};
    std::wstring out = sid;
    LocalFree(sid);
    return out;
}
std::wstring pipe_name() {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    return L"\\\\.\\pipe\\EShuRu.v1." + ipc_scope() + L"." + std::to_wstring(session);
}
std::wstring ipc_scope() {
    auto scope = user_sid();
    wchar_t token[65]{};
    DWORD n = GetEnvironmentVariableW(L"ES_SHURU_TEST_SCOPE", token, 65);
    if (n > 0 && n < 65) {
        bool valid = true;
        for (DWORD i = 0; i < n; ++i)
            if (!((token[i] >= L'a' && token[i] <= L'z') || (token[i] >= L'0' && token[i] <= L'9')))
                valid = false;
        if (valid)
            scope += L".test." + std::wstring(token);
    }
    return scope;
}
std::string session_id() {
    GUID id;
    CoCreateGuid(&id);
    wchar_t b[40]{};
    StringFromGUID2(id, b, 40);
    return utf8(b);
}
std::wstring win_error(DWORD error) {
    wchar_t *p = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, 0, reinterpret_cast<LPWSTR>(&p), 0, nullptr);
    std::wstring out = p ? p : L"Windows error";
    if (p)
        LocalFree(p);
    return out;
}
bool auto_start_allowed() {
    DWORD paused = 0, bytes = sizeof(paused);
    auto name = L"AutostartPaused." + ipc_scope();
    LONG result = RegGetValueW(HKEY_CURRENT_USER, L"Software\\EShuRu\\Runtime", name.c_str(),
                               RRF_RT_REG_DWORD, nullptr, &paused, &bytes);
    return result == ERROR_FILE_NOT_FOUND || (result == ERROR_SUCCESS && paused == 0);
}
bool set_auto_start_allowed(bool allowed) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\EShuRu\\Runtime", 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    auto name = L"AutostartPaused." + ipc_scope();
    DWORD paused = 1;
    LONG result = allowed ? RegDeleteValueW(key, name.c_str())
                          : RegSetValueExW(key, name.c_str(), 0, REG_DWORD,
                                           reinterpret_cast<const BYTE *>(&paused), sizeof(paused));
    RegCloseKey(key);
    return result == ERROR_SUCCESS || (allowed && result == ERROR_FILE_NOT_FOUND);
}
bool launch_companion(const std::filesystem::path &root) {
    if (!auto_start_allowed())
        return false;
    std::wstring exe = (root / L"EShuRu.exe").wstring();
    std::wstring cmd = L"\"" + exe + L"\" --server";
    if (GetEnvironmentVariableW(L"ES_SHURU_TEST_SCOPE", nullptr, 0))
        cmd += L" --headless";
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        root.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
std::string read_key() {
    PCREDENTIALW c = nullptr;
    if (!CredReadW(L"EShuRu/DeepSeek", CRED_TYPE_GENERIC, 0, &c))
        return {};
    std::string s(reinterpret_cast<char *>(c->CredentialBlob), c->CredentialBlobSize);
    CredFree(c);
    return s;
}
bool save_key(const std::string &value) {
    if (value.empty() || value.size() > 2048 || value.find_first_of("\r\n") != std::string::npos)
        return false;
    CREDENTIALW c{};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = const_cast<LPWSTR>(L"EShuRu/DeepSeek");
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    c.CredentialBlobSize = DWORD(value.size());
    c.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(value.data()));
    c.UserName = const_cast<LPWSTR>(L"DeepSeek API");
    return CredWriteW(&c, 0) != FALSE;
}
void delete_key() {
    CredDeleteW(L"EShuRu/DeepSeek", CRED_TYPE_GENERIC, 0);
}
Json load_settings() {
    try {
        std::ifstream f(config_dir() / L"settings.json");
        Json j;
        f >> j;
        return j;
    } catch (...) {
        return Json{{"theme", "system"},
                    {"remember_words", true},
                    {"compact", false},
                    {"input_registration_checked", false}};
    }
}
void save_settings(const Json &settings) {
    auto previous = load_settings();
    Json clean = {
        {"theme", settings.value("theme", "system")},
        {"remember_words", settings.value("remember_words", true)},
        {"compact", settings.value("compact", false)},
        {"input_registration_checked",
         settings.value("input_registration_checked", previous.value("input_registration_checked", false))}};
    auto p = config_dir() / L"settings.json";
    std::ofstream f(p, std::ios::trunc);
    f << clean.dump(2);
}
bool is_high_surrogate(wchar_t c) {
    return c >= 0xd800 && c <= 0xdbff;
}
bool is_low_surrogate(wchar_t c) {
    return c >= 0xdc00 && c <= 0xdfff;
}
size_t previous_character(const std::wstring &text, size_t cursor) {
    if (!cursor)
        return 0;
    --cursor;
    if (cursor && is_low_surrogate(text[cursor]) && is_high_surrogate(text[cursor - 1]))
        --cursor;
    return cursor;
}
size_t next_character(const std::wstring &text, size_t cursor) {
    if (cursor >= text.size())
        return text.size();
    if (is_high_surrogate(text[cursor]) && cursor + 1 < text.size() && is_low_surrogate(text[cursor + 1]))
        return cursor + 2;
    return cursor + 1;
}
size_t character_count(const std::wstring &text) {
    size_t n = 0;
    for (size_t p = 0; p < text.size(); p = next_character(text, p))
        ++n;
    return n;
}
} // namespace es
