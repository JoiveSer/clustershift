#pragma once

#include <windows.h>
#include <string>
#include <system_error>

namespace clustershift::core {

[[nodiscard]] inline std::wstring format_win32_error(DWORD error_code) {
    if (error_code == ERROR_SUCCESS) {
        return L"Success";
    }

    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );

    if (length == 0 || !buffer) {
        return L"Unknown error " + std::to_wstring(error_code);
    }

    std::wstring result(buffer, length);
    LocalFree(buffer);

    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) {
        result.pop_back();
    }
    return result;
}

[[nodiscard]] inline bool enable_privilege(LPCWSTR privilege_name) noexcept {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, privilege_name, &luid)) {
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    const BOOL success = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    const DWORD last_error = GetLastError();
    CloseHandle(token);

    return success && (last_error == ERROR_SUCCESS);
}

[[nodiscard]] inline std::string format_win32_error_a(DWORD error_code) {
    std::wstring ws = format_win32_error(error_code);
    std::string s;
    s.reserve(ws.size());
    for (wchar_t c : ws) {
        s.push_back(static_cast<char>(c & 0x7F));
    }
    return s;
}

[[nodiscard]] inline bool is_user_elevated() noexcept {
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation{};
        DWORD ret_len = 0;
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &ret_len)) {
            CloseHandle(token);
            return elevation.TokenIsElevated != 0;
        }
        CloseHandle(token);
    }
    return false;
}

[[nodiscard]] inline bool set_se_manage_volume_privilege() noexcept {
    return enable_privilege(SE_MANAGE_VOLUME_NAME);
}

} // namespace clustershift::core
