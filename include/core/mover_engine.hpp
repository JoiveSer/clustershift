#pragma once

#include "scoped_handle.hpp"
#include "extent_resolver.hpp"
#include <string>
#include <expected>
#include <filesystem>

namespace clustershift::core {

struct MoveResult {
    bool success{false};
    uint64_t clusters_moved{0};
    int64_t new_starting_lcn{0};
    DWORD win32_error{0};
};

class MoverEngine {
public:
    static std::expected<MoveResult, DWORD> consolidate_file(
        HANDLE volume_handle,
        const std::filesystem::path& file_path,
        int64_t target_lcn
    );

    static std::expected<MoveResult, DWORD> move_extent(
        HANDLE volume_handle,
        HANDLE file_handle,
        int64_t starting_vcn,
        int64_t target_lcn,
        uint32_t cluster_count
    );
};

} // namespace clustershift::core
