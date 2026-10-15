#pragma once

#include "scoped_handle.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <expected>
#include <filesystem>

namespace clustershift::core {

struct FileClusterExtent {
    int64_t starting_vcn{0};
    int64_t target_lcn{0};
    uint64_t cluster_count{0};
    bool is_sparse{false};
};

struct FileFragmentationReport {
    std::filesystem::path path;
    uint64_t file_size_bytes{0};
    uint64_t total_clusters{0};
    uint32_t extent_count{0};
    double fragmentation_score{0.0};
    std::vector<FileClusterExtent> extents;
};

class ExtentResolver {
public:
    static std::expected<FileFragmentationReport, DWORD> analyze_file(const std::filesystem::path& file_path);
    static std::vector<FileFragmentationReport> scan_directory(
        const std::filesystem::path& directory_path, 
        bool recursive = true,
        uint32_t min_fragments_threshold = 2
    );
};

} // namespace clustershift::core
