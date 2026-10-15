#pragma once

#include "scoped_handle.hpp"
#include <vector>
#include <cstdint>
#include <expected>
#include <optional>

namespace clustershift::core {

struct FreeClusterRun {
    int64_t starting_lcn{0};
    uint64_t cluster_count{0};
};

class ClusterMap {
public:
    static std::expected<ClusterMap, DWORD> load(HANDLE volume_handle);

    [[nodiscard]] std::optional<int64_t> find_contiguous_free_space(
        uint64_t required_clusters, 
        int64_t preferred_starting_lcn = 0
    ) const noexcept;

    [[nodiscard]] const std::vector<uint8_t>& raw_bitmap_data() const noexcept { return bitmap_data_; }
    [[nodiscard]] bool is_cluster_free(int64_t lcn) const noexcept;
    [[nodiscard]] uint64_t total_clusters() const noexcept { return total_clusters_; }
    [[nodiscard]] uint64_t free_clusters() const noexcept { return free_clusters_; }
    [[nodiscard]] int64_t mft_zone_start() const noexcept { return mft_zone_start_; }
    [[nodiscard]] int64_t mft_zone_end() const noexcept { return mft_zone_end_; }

private:
    ClusterMap() = default;

    std::vector<uint8_t> bitmap_data_;
    int64_t starting_lcn_{0};
    uint64_t total_clusters_{0};
    uint64_t free_clusters_{0};
    int64_t mft_zone_start_{-1};
    int64_t mft_zone_end_{-1};
};

} // namespace clustershift::core
