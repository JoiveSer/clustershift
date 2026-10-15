#include "core/mover_engine.hpp"
#include <windows.h>
#include <winioctl.h>

namespace clustershift::core {

std::expected<MoveResult, DWORD> MoverEngine::move_extent(
    HANDLE volume_handle,
    HANDLE file_handle,
    int64_t starting_vcn,
    int64_t target_lcn,
    uint32_t cluster_count
) {
    MOVE_FILE_DATA move_data{};
    move_data.FileHandle = file_handle;
    move_data.StartingVcn.QuadPart = starting_vcn;
    move_data.StartingLcn.QuadPart = target_lcn;
    move_data.ClusterCount = cluster_count;

    DWORD bytes_returned = 0;
    const BOOL success = DeviceIoControl(
        volume_handle,
        FSCTL_MOVE_FILE,
        &move_data,
        sizeof(move_data),
        nullptr,
        0,
        &bytes_returned,
        nullptr
    );

    if (!success) {
        return std::unexpected(GetLastError());
    }

    MoveResult res{};
    res.success = true;
    res.clusters_moved = cluster_count;
    res.new_starting_lcn = target_lcn;
    return res;
}

std::expected<MoveResult, DWORD> MoverEngine::consolidate_file(
    HANDLE volume_handle,
    const std::filesystem::path& file_path,
    int64_t target_lcn
) {
    // Open file with required access for FSCTL_MOVE_FILE
    const ScopedHandle file_handle(CreateFileW(
        file_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    ));

    if (!file_handle.is_valid()) {
        return std::unexpected(GetLastError());
    }

    auto report = ExtentResolver::analyze_file(file_path);
    if (!report.has_value()) {
        return std::unexpected(report.error());
    }

    if (report->extents.empty() || report->total_clusters == 0) {
        MoveResult res{};
        res.success = true;
        return res;
    }

    uint64_t current_target_lcn = static_cast<uint64_t>(target_lcn);
    uint64_t total_moved = 0;

    for (const auto& extent : report->extents) {
        if (extent.is_sparse) {
            continue;
        }

        uint64_t remaining_in_extent = extent.cluster_count;
        int64_t vcn_offset = extent.starting_vcn;

        // Break very large extents into chunks if needed
        while (remaining_in_extent > 0) {
            const uint32_t chunk_clusters = static_cast<uint32_t>((std::min)(remaining_in_extent, static_cast<uint64_t>(65536)));

            auto chunk_res = move_extent(
                volume_handle,
                file_handle.get(),
                vcn_offset,
                static_cast<int64_t>(current_target_lcn),
                chunk_clusters
            );

            if (!chunk_res.has_value()) {
                MoveResult partial{};
                partial.success = false;
                partial.clusters_moved = total_moved;
                partial.win32_error = chunk_res.error();
                return partial;
            }

            remaining_in_extent -= chunk_clusters;
            vcn_offset += chunk_clusters;
            current_target_lcn += chunk_clusters;
            total_moved += chunk_clusters;
        }
    }

    MoveResult final_result{};
    final_result.success = true;
    final_result.clusters_moved = total_moved;
    final_result.new_starting_lcn = target_lcn;
    return final_result;
}

} // namespace clustershift::core
