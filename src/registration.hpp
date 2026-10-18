#pragma once
#include "common.hpp"
namespace es {
// Per-user TSF registration state. The COM server itself is registered by
// regsvr32; this covers the keyboard-list entry and the enable flag that make
// the profile selectable from Win+Space.
struct InputRegistration {
    bool language_tip = false;
    bool profile_enabled = false;
    bool com_x64 = false;
    bool com_x86 = false;
    std::wstring com_x64_path, com_x86_path;
};
std::wstring input_profile_tip();
InputRegistration input_registration();
// Idempotent. Requires the COM server to be registered first.
bool ensure_input_registered();
// Removes the per-user list entry and enable flag; COM unregistration is separate.
bool unregister_input_profile();
} // namespace es
