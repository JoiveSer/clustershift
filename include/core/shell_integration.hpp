#pragma once

#include <windows.h>
#include <string>
#include <expected>

namespace clustershift::core {

class ShellIntegration {
public:
    static std::expected<void, DWORD> install_context_menu();
    static std::expected<void, DWORD> uninstall_context_menu();
    static bool is_context_menu_installed() noexcept;
};

} // namespace clustershift::core
