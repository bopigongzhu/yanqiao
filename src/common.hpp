#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>
#include <optional>
#include <chrono>
#include "json.hpp"

namespace es {
std::wstring ipc_scope();
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
inline constexpr UINT WM_ES_EVENT = WM_APP + 71, WM_ES_AI = WM_APP + 72;
inline constexpr size_t MAX_FRAME = 65536;
std::wstring wide(const std::string &s);
std::string utf8(const std::wstring &s);
std::filesystem::path module_dir(HMODULE module = nullptr);
std::filesystem::path config_dir();
std::wstring user_sid();
std::wstring pipe_name();
std::string session_id();
std::wstring win_error(DWORD error = GetLastError());
bool launch_companion(const std::filesystem::path &root);
bool auto_start_allowed();
bool set_auto_start_allowed(bool allowed);
std::string read_key();
bool save_key(const std::string &value);
void delete_key();
Json load_settings();
void save_settings(const Json &settings);
bool is_high_surrogate(wchar_t c);
bool is_low_surrogate(wchar_t c);
size_t previous_character(const std::wstring &text, size_t cursor);
size_t next_character(const std::wstring &text, size_t cursor);
size_t character_count(const std::wstring &text);
inline uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

class Handle {
  public:
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() {
        reset();
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&x) noexcept : value(x.value) {
        x.value = INVALID_HANDLE_VALUE;
    }
    void reset(HANDLE h = INVALID_HANDLE_VALUE) {
        if (value != INVALID_HANDLE_VALUE && value)
            CloseHandle(value);
        value = h;
    }
    explicit operator bool() const {
        return value && value != INVALID_HANDLE_VALUE;
    }
};

template <class T> class ComPtr {
    T *p_ = nullptr;

  public:
    ~ComPtr() {
        reset();
    }
    ComPtr() = default;
    ComPtr(const ComPtr &) = delete;
    T *get() const {
        return p_;
    }
    T *operator->() const {
        return p_;
    }
    explicit operator bool() const {
        return p_ != nullptr;
    }
    T **put() {
        reset();
        return &p_;
    }
    void reset(T *p = nullptr) {
        if (p_)
            p_->Release();
        p_ = p;
    }
    T *detach() {
        T *p = p_;
        p_ = nullptr;
        return p;
    }
};
} // namespace es
