#include "core/extent_resolver.hpp"
#include <windows.h>
#include <winioctl.h>
#include <memory>
#include <system_error>

namespace clustershift::core {

std::expected<FileFragmentationReport, DWORD> ExtentResolver::analyze_file(const std::filesystem::path& file_path) {
    const ScopedHandle file_handle(CreateFileW(
        file_path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    ));

    if (!file_handle.is_valid()) {
        return std::unexpected(GetLastError());
    }

    LARGE_INTEGER file_size{};
    if (!GetFileSizeEx(file_handle.get(), &file_size)) {
        return std::unexpected(GetLastError());
    }

    if (file_size.QuadPart == 0) {
        FileFragmentationReport empty_report{};
        empty_report.path = file_path;
        empty_report.file_size_bytes = 0;
        empty_report.extent_count = 0;
        return empty_report;
    }

    STARTING_VCN_INPUT_BUFFER input_vcn{};
    input_vcn.StartingVcn.QuadPart = 0;

    size_t buffer_size = sizeof(RETRIEVAL_POINTERS_BUFFER) + (256 * (sizeof(LARGE_INTEGER) * 2));
    auto buffer = std::make_unique<uint8_t[]>(buffer_size);

    std::vector<FileClusterExtent> extents;
    int64_t current_vcn = 0;
    DWORD bytes_returned = 0;

    while (true) {
        const BOOL success = DeviceIoControl(
            file_handle.get(),
            FSCTL_GET_RETRIEVAL_POINTERS,
            &input_vcn,
            sizeof(input_vcn),
            buffer.get(),
            static_cast<DWORD>(buffer_size),
            &bytes_returned,
            nullptr
        );

        const DWORD error = GetLastError();
        if (!success && error != ERROR_MORE_DATA) {
            if (error == ERROR_HANDLE_EOF) {
                break;
            }
            return std::unexpected(error);
        }

        const auto* retrieval = reinterpret_cast<const RETRIEVAL_POINTERS_BUFFER*>(buffer.get());
        current_vcn = retrieval->StartingVcn.QuadPart;

        for (DWORD i = 0; i < retrieval->ExtentCount; ++i) {
            const auto& ext = retrieval->Extents[i];
            const int64_t next_vcn = ext.NextVcn.QuadPart;
            const int64_t lcn = ext.Lcn.QuadPart;
            const uint64_t count = static_cast<uint64_t>(next_vcn - current_vcn);

            extents.push_back(FileClusterExtent{
                .starting_vcn = current_vcn,
                .target_lcn = lcn,
                .cluster_count = count,
                .is_sparse = (lcn == -1)
            });

            current_vcn = next_vcn;
        }

        if (success) {
            break;
        }

        // Buffer was insufficient for all extents, grow buffer and continue from next VCN
        input_vcn.StartingVcn.QuadPart = current_vcn;
        buffer_size *= 2;
        buffer = std::make_unique<uint8_t[]>(buffer_size);
    }

    uint64_t total_clusters = 0;
    for (const auto& ext : extents) {
        total_clusters += ext.cluster_count;
    }

    double score = 0.0;
    if (total_clusters > 1) {
        const uint32_t count = static_cast<uint32_t>(extents.size());
        score = (count > 1) ? (static_cast<double>(count - 1) / static_cast<double>(total_clusters)) * 100.0 : 0.0;
    }

    FileFragmentationReport report{};
    report.path = file_path;
    report.file_size_bytes = static_cast<uint64_t>(file_size.QuadPart);
    report.total_clusters = total_clusters;
    report.extent_count = static_cast<uint32_t>(extents.size());
    report.fragmentation_score = score;
    report.extents = std::move(extents);

    return report;
}

std::vector<FileFragmentationReport> ExtentResolver::scan_directory(
    const std::filesystem::path& directory_path,
    bool recursive,
    uint32_t min_fragments_threshold
) {
    std::vector<FileFragmentationReport> reports;
    std::error_code ec;

    auto options = std::filesystem::directory_options::skip_permission_denied;

    if (recursive) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory_path, options, ec)) {
            if (entry.is_regular_file(ec)) {
                auto result = analyze_file(entry.path());
                if (result.has_value() && result->extent_count >= min_fragments_threshold) {
                    reports.push_back(std::move(*result));
                }
            }
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(directory_path, options, ec)) {
            if (entry.is_regular_file(ec)) {
                auto result = analyze_file(entry.path());
                if (result.has_value() && result->extent_count >= min_fragments_threshold) {
                    reports.push_back(std::move(*result));
                }
            }
        }
    }

    return reports;
}

} // namespace clustershift::core
