#include "core/trim_engine.hpp"
#include <winioctl.h>
#include <memory>

namespace clustershift::core {

std::expected<TrimResult, DWORD> TrimEngine::trim_range(HANDLE volume_handle, uint64_t offset, uint64_t length) {
    const size_t buffer_size = sizeof(FILE_LEVEL_TRIM) + sizeof(FILE_LEVEL_TRIM_RANGE);
    auto buffer = std::make_unique<uint8_t[]>(buffer_size);

    auto* trim_input = reinterpret_cast<PFILE_LEVEL_TRIM>(buffer.get());
    trim_input->Key = 0;
    trim_input->NumRanges = 1;
    trim_input->Ranges[0].Offset = offset;
    trim_input->Ranges[0].Length = length;

    FILE_LEVEL_TRIM_OUTPUT trim_output{};
    DWORD bytes_returned = 0;

    const BOOL success = DeviceIoControl(
        volume_handle,
        FSCTL_FILE_LEVEL_TRIM,
        trim_input,
        static_cast<DWORD>(buffer_size),
        &trim_output,
        sizeof(trim_output),
        &bytes_returned,
        nullptr
    );

    if (!success) {
        return std::unexpected(GetLastError());
    }

    TrimResult res{};
    res.success = (trim_output.NumRangesProcessed > 0);
    res.bytes_trimmed = length;
    return res;
}

std::expected<TrimResult, DWORD> TrimEngine::trim_volume(HANDLE volume_handle) {
    // Issue Storage Management Data Set Attributes (DSM) Trim on volume device
    DEVICE_MANAGE_DATA_SET_ATTRIBUTES dsm{};
    dsm.Size = sizeof(dsm);
    dsm.Action = DeviceDsmAction_Trim;
    dsm.Flags = DEVICE_DSM_FLAG_TRIM_NOT_FS_ALLOCATED;

    DWORD bytes_returned = 0;
    const BOOL success = DeviceIoControl(
        volume_handle,
        IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES,
        &dsm,
        sizeof(dsm),
        nullptr,
        0,
        &bytes_returned,
        nullptr
    );

    if (!success) {
        return std::unexpected(GetLastError());
    }

    TrimResult res{};
    res.success = true;
    return res;
}

} // namespace clustershift::core
