#pragma once

#include "scoped_handle.hpp"
#include <windows.h>
#include <cstdint>
#include <expected>

namespace clustershift::core {

struct TrimResult {
    bool success{false};
    uint64_t bytes_trimmed{0};
    DWORD win32_error{0};
};

class TrimEngine {
public:
    static std::expected<TrimResult, DWORD> trim_volume(HANDLE volume_handle);
    static std::expected<TrimResult, DWORD> trim_range(HANDLE volume_handle, uint64_t offset, uint64_t length);
};

} // namespace clustershift::core
