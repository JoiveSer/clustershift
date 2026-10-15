#include "core/volume_manager.hpp"
#include <winioctl.h>
#include <ntddstor.h>

namespace clustershift::core {

std::expected<ScopedHandle, DWORD> VolumeManager::open_volume(wchar_t drive_letter, bool write_access) {
    const std::wstring volume_path = L"\\\\.\\" + std::wstring(1, static_cast<wchar_t>(towupper(drive_letter))) + L":";
    
    DWORD access = GENERIC_READ;
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    if (write_access) {
        access |= GENERIC_WRITE;
    }

    HANDLE handle = CreateFileW(
        volume_path.c_str(),
        access,
        share,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(GetLastError());
    }

    return ScopedHandle(handle);
}

DriveKind VolumeManager::detect_drive_kind(HANDLE volume_handle) {
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;

    DEVICE_SEEK_PENALTY_DESCRIPTOR descriptor{};
    DWORD bytes_returned = 0;

    const BOOL success = DeviceIoControl(
        volume_handle,
        IOCTL_STORAGE_QUERY_PROPERTY,
        &query,
        sizeof(query),
        &descriptor,
        sizeof(descriptor),
        &bytes_returned,
        nullptr
    );

    if (!success) {
        return DriveKind::Unknown;
    }

    return descriptor.IncursSeekPenalty ? DriveKind::RotationalHdd : DriveKind::SolidStateDrive;
}

std::expected<VolumeDetails, DWORD> VolumeManager::query_volume_details(wchar_t drive_letter) {
    const wchar_t upper_letter = static_cast<wchar_t>(towupper(drive_letter));
    const std::wstring root_path = std::wstring(1, upper_letter) + L":\\";

    DWORD sectors_per_cluster = 0;
    DWORD bytes_per_sector = 0;
    DWORD number_of_free_clusters = 0;
    DWORD total_number_of_clusters = 0;

    if (!GetDiskFreeSpaceW(
            root_path.c_str(),
            &sectors_per_cluster,
            &bytes_per_sector,
            &number_of_free_clusters,
            &total_number_of_clusters)) {
        return std::unexpected(GetLastError());
    }

    wchar_t fs_name[MAX_PATH + 1]{};
    if (!GetVolumeInformationW(
            root_path.c_str(),
            nullptr,
            0,
            nullptr,
            nullptr,
            nullptr,
            fs_name,
            MAX_PATH)) {
        return std::unexpected(GetLastError());
    }

    auto volume_handle = open_volume(drive_letter, false);
    DriveKind kind = DriveKind::Unknown;
    if (volume_handle.has_value()) {
        kind = detect_drive_kind(volume_handle->get());
    }

    VolumeDetails details{};
    details.path = root_path;
    details.file_system = fs_name;
    details.cluster_size = sectors_per_cluster * bytes_per_sector;
    details.total_clusters = total_number_of_clusters;
    details.free_clusters = number_of_free_clusters;
    details.drive_kind = kind;

    return details;
}

} // namespace clustershift::core
