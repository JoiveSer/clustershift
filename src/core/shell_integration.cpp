#include "core/shell_integration.hpp"
#include <vector>

namespace clustershift::core {

namespace {
std::wstring get_current_executable_path() {
    std::vector<wchar_t> buffer(MAX_PATH);
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length == buffer.size() && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    return std::wstring(buffer.data(), length);
}
} // namespace

bool ShellIntegration::is_context_menu_installed() noexcept {
    HKEY key = nullptr;
    const LSTATUS status = RegOpenKeyExW(
        HKEY_CLASSES_ROOT,
        L"Directory\\shell\\ClusterShift",
        0,
        KEY_READ,
        &key
    );
    if (status == ERROR_SUCCESS) {
        RegCloseKey(key);
        return true;
    }
    return false;
}

std::expected<void, DWORD> ShellIntegration::install_context_menu() {
    const std::wstring exe_path = get_current_executable_path();
    const std::wstring dir_cmd = L"cmd.exe /k \"\"" + exe_path + L"\" scan \"%1\" --recursive\"";
    const std::wstring drive_cmd = L"cmd.exe /k \"\"" + exe_path + L"\" info %1 & \"" + exe_path + L"\" map %1\"";

    // 1. Directory Menu
    HKEY dir_key = nullptr;
    LSTATUS status = RegCreateKeyExW(
        HKEY_CLASSES_ROOT,
        L"Directory\\shell\\ClusterShift",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        nullptr,
        &dir_key,
        nullptr
    );
    if (status != ERROR_SUCCESS) {
        return std::unexpected(static_cast<DWORD>(status));
    }

    const std::wstring dir_title = L"ClusterShift: Scan Fragmentation [by Joive]";
    RegSetValueExW(dir_key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(dir_title.c_str()), static_cast<DWORD>((dir_title.size() + 1) * sizeof(wchar_t)));
    RegSetValueExW(dir_key, L"Icon", 0, REG_SZ, reinterpret_cast<const BYTE*>(exe_path.c_str()), static_cast<DWORD>((exe_path.size() + 1) * sizeof(wchar_t)));

    HKEY dir_cmd_key = nullptr;
    status = RegCreateKeyExW(dir_key, L"command", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &dir_cmd_key, nullptr);
    RegCloseKey(dir_key);
    if (status != ERROR_SUCCESS) {
        return std::unexpected(static_cast<DWORD>(status));
    }

    RegSetValueExW(dir_cmd_key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(dir_cmd.c_str()), static_cast<DWORD>((dir_cmd.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(dir_cmd_key);

    // 2. Drive Menu
    HKEY drive_key = nullptr;
    status = RegCreateKeyExW(
        HKEY_CLASSES_ROOT,
        L"Drive\\shell\\ClusterShift",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        nullptr,
        &drive_key,
        nullptr
    );
    if (status != ERROR_SUCCESS) {
        return std::unexpected(static_cast<DWORD>(status));
    }

    const std::wstring drive_title = L"ClusterShift: Storage Health & Map [by Joive]";
    RegSetValueExW(drive_key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(drive_title.c_str()), static_cast<DWORD>((drive_title.size() + 1) * sizeof(wchar_t)));
    RegSetValueExW(drive_key, L"Icon", 0, REG_SZ, reinterpret_cast<const BYTE*>(exe_path.c_str()), static_cast<DWORD>((exe_path.size() + 1) * sizeof(wchar_t)));

    HKEY drive_cmd_key = nullptr;
    status = RegCreateKeyExW(drive_key, L"command", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &drive_cmd_key, nullptr);
    RegCloseKey(drive_key);
    if (status != ERROR_SUCCESS) {
        return std::unexpected(static_cast<DWORD>(status));
    }

    RegSetValueExW(drive_cmd_key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(drive_cmd.c_str()), static_cast<DWORD>((drive_cmd.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(drive_cmd_key);

    return {};
}

std::expected<void, DWORD> ShellIntegration::uninstall_context_menu() {
    LSTATUS s1 = RegDeleteTreeW(HKEY_CLASSES_ROOT, L"Directory\\shell\\ClusterShift");
    LSTATUS s2 = RegDeleteTreeW(HKEY_CLASSES_ROOT, L"Drive\\shell\\ClusterShift");

    if (s1 != ERROR_SUCCESS && s1 != ERROR_FILE_NOT_FOUND) {
        return std::unexpected(static_cast<DWORD>(s1));
    }
    if (s2 != ERROR_SUCCESS && s2 != ERROR_FILE_NOT_FOUND) {
        return std::unexpected(static_cast<DWORD>(s2));
    }
    return {};
}

} // namespace clustershift::core
