#pragma once

#include "scoped_handle.hpp"
#include <string>
#include <cstdint>
#include <expected>

namespace clustershift::core {

enum class DriveKind {
    RotationalHdd,
    SolidStateDrive,
    Unknown
};

struct VolumeDetails {
    std::wstring path;
    std::wstring file_system;
    uint32_t cluster_size{4096};
    uint64_t total_clusters{0};
    uint64_t free_clusters{0};
    DriveKind drive_kind{DriveKind::Unknown};
};

class VolumeManager {
public:
    static std::expected<VolumeDetails, DWORD> query_volume_details(wchar_t drive_letter);
    static std::expected<ScopedHandle, DWORD> open_volume(wchar_t drive_letter, bool write_access = false);
    static DriveKind detect_drive_kind(HANDLE volume_handle);
};

} // namespace clustershift::core
