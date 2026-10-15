#include "core/cluster_map.hpp"
#include <windows.h>
#include <winioctl.h>
#include <memory>

namespace clustershift::core {

std::expected<ClusterMap, DWORD> ClusterMap::load(HANDLE volume_handle) {
    STARTING_LCN_INPUT_BUFFER input{};
    input.StartingLcn.QuadPart = 0;

    // Initial query to get header and size
    DWORD buffer_size = 1024 * 1024 * 8; // 8MB buffer chunks (~64 million clusters)
    auto buffer = std::make_unique<uint8_t[]>(buffer_size);
    DWORD bytes_returned = 0;

    const BOOL success = DeviceIoControl(
        volume_handle,
        FSCTL_GET_VOLUME_BITMAP,
        &input,
        sizeof(input),
        buffer.get(),
        buffer_size,
        &bytes_returned,
        nullptr
    );

    const DWORD error = GetLastError();
    if (!success && error != ERROR_MORE_DATA) {
        return std::unexpected(error);
    }

    const auto* header = reinterpret_cast<const VOLUME_BITMAP_BUFFER*>(buffer.get());
    const int64_t total_clusters = header->BitmapSize.QuadPart;
    const size_t total_bytes_needed = static_cast<size_t>((total_clusters + 7) / 8);

    ClusterMap map;
    map.starting_lcn_ = header->StartingLcn.QuadPart;
    map.total_clusters_ = static_cast<uint64_t>(total_clusters);
    map.bitmap_data_.resize(total_bytes_needed, 0xFF); // Default to allocated

    // Query NTFS MFT zone reservation to avoid polluting reserved MFT expansion space
    NTFS_VOLUME_DATA_BUFFER ntfs_data{};
    DWORD ntfs_bytes = 0;
    if (DeviceIoControl(volume_handle, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &ntfs_data, sizeof(ntfs_data), &ntfs_bytes, nullptr)) {
        map.mft_zone_start_ = ntfs_data.MftZoneStart.QuadPart;
        map.mft_zone_end_ = ntfs_data.MftZoneEnd.QuadPart;
    }

    // Copy initial chunk
    const size_t header_offset = offsetof(VOLUME_BITMAP_BUFFER, Buffer);
    if (bytes_returned > header_offset) {
        const size_t valid_bytes = bytes_returned - header_offset;
        memcpy(map.bitmap_data_.data(), header->Buffer, (std::min)(valid_bytes, total_bytes_needed));
    }

    // If more data needed, fetch remaining chunks
    int64_t next_lcn = header->StartingLcn.QuadPart + (static_cast<int64_t>(bytes_returned - header_offset) * 8);
    while (next_lcn < total_clusters) {
        input.StartingLcn.QuadPart = next_lcn;
        const BOOL chunk_res = DeviceIoControl(
            volume_handle,
            FSCTL_GET_VOLUME_BITMAP,
            &input,
            sizeof(input),
            buffer.get(),
            buffer_size,
            &bytes_returned,
            nullptr
        );

        if (!chunk_res && GetLastError() != ERROR_MORE_DATA) {
            break;
        }

        const auto* chunk_header = reinterpret_cast<const VOLUME_BITMAP_BUFFER*>(buffer.get());
        if (bytes_returned > header_offset) {
            const size_t valid_bytes = bytes_returned - header_offset;
            const size_t byte_offset = static_cast<size_t>(chunk_header->StartingLcn.QuadPart / 8);
            if (byte_offset < total_bytes_needed) {
                const size_t copy_len = (std::min)(valid_bytes, total_bytes_needed - byte_offset);
                memcpy(map.bitmap_data_.data() + byte_offset, chunk_header->Buffer, copy_len);
            }
            next_lcn = chunk_header->StartingLcn.QuadPart + (static_cast<int64_t>(valid_bytes) * 8);
        } else {
            break;
        }
    }

    // Count free clusters (0-bits, excluding MFT zone)
    uint64_t free_count = 0;
    for (uint64_t i = 0; i < map.total_clusters_; ++i) {
        if (map.is_cluster_free(static_cast<int64_t>(i))) {
            ++free_count;
        }
    }
    map.free_clusters_ = free_count;

    return map;
}

bool ClusterMap::is_cluster_free(int64_t lcn) const noexcept {
    if (lcn < 0 || static_cast<uint64_t>(lcn) >= total_clusters_) {
        return false;
    }
    // Respect NTFS MFT reserved zone: never allocate user clusters inside MFT expansion boundary
    if (mft_zone_start_ >= 0 && mft_zone_end_ > mft_zone_start_) {
        if (lcn >= mft_zone_start_ && lcn < mft_zone_end_) {
            return false;
        }
    }
    const size_t byte_idx = static_cast<size_t>(lcn / 8);
    const uint8_t bit_idx = static_cast<uint8_t>(lcn % 8);
    // 0 = free, 1 = allocated
    return (bitmap_data_[byte_idx] & (1 << bit_idx)) == 0;
}

std::optional<int64_t> ClusterMap::find_contiguous_free_space(
    uint64_t required_clusters,
    int64_t preferred_starting_lcn
) const noexcept {
    if (required_clusters == 0 || required_clusters > free_clusters_) {
        return std::nullopt;
    }

    uint64_t current_run = 0;
    int64_t run_start = -1;

    const int64_t start_pos = (preferred_starting_lcn >= 0 && static_cast<uint64_t>(preferred_starting_lcn) < total_clusters_) 
        ? preferred_starting_lcn 
        : 0;

    for (int64_t i = start_pos; static_cast<uint64_t>(i) < total_clusters_; ++i) {
        if (is_cluster_free(i)) {
            if (current_run == 0) {
                run_start = i;
            }
            ++current_run;
            if (current_run == required_clusters) {
                return run_start;
            }
        } else {
            current_run = 0;
            run_start = -1;
        }
    }

    // If preferred start didn't find a run, search from beginning up to preferred start
    if (start_pos > 0) {
        current_run = 0;
        run_start = -1;
        for (int64_t i = 0; i < start_pos; ++i) {
            if (is_cluster_free(i)) {
                if (current_run == 0) {
                    run_start = i;
                }
                ++current_run;
                if (current_run == required_clusters) {
                    return run_start;
                }
            } else {
                current_run = 0;
                run_start = -1;
            }
        }
    }

    return std::nullopt;
}

} // namespace clustershift::core
