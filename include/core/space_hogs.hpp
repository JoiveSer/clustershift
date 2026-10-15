#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <expected>

namespace clustershift::core {

struct FileEntry {
    std::wstring path;
    uint64_t size_bytes{0};
};

struct ExtensionStats {
    std::wstring extension;
    uint64_t total_bytes{0};
    uint64_t file_count{0};
};

struct FolderSizeEntry {
    std::wstring path;
    uint64_t size_bytes{0};
};

struct SpaceHogsReport {
    std::wstring root_path;
    uint64_t total_bytes{0};
    uint64_t total_files{0};
    uint64_t total_directories{0};
    double scan_duration_seconds{0.0};
    bool is_rotational_hdd{false};
    uint32_t threads_used{1};
    std::vector<FileEntry> top_files;
    std::vector<ExtensionStats> top_extensions;
    std::vector<FolderSizeEntry> top_subfolders;
};

class SpaceHogsScanner {
public:
    static std::expected<SpaceHogsReport, DWORD> scan(
        const std::wstring& target_path,
        size_t top_count = 15
    );
};

} // namespace clustershift::core
