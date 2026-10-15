#include "core/space_hogs.hpp"
#include <windows.h>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <cwctype>

namespace clustershift::core {

namespace {

struct CompareFileMin {
    bool operator()(const FileEntry& a, const FileEntry& b) const noexcept {
        return a.size_bytes > b.size_bytes;
    }
};

std::wstring normalize_path(std::wstring p) {
    if (p.empty()) return L"";
    std::replace(p.begin(), p.end(), L'/', L'\\');
    if (p.back() != L'\\') {
        p.push_back(L'\\');
    }
    return p;
}

std::wstring get_extension_lower(const std::wstring& filename) {
    const size_t dot_pos = filename.find_last_of(L'.');
    if (dot_pos == std::wstring::npos || dot_pos == 0) {
        return L"(no extension)";
    }
    std::wstring ext = filename.substr(dot_pos);
    for (auto& ch : ext) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return ext;
}

} // namespace

std::expected<SpaceHogsReport, DWORD> SpaceHogsScanner::scan(
    const std::wstring& raw_target_path,
    size_t top_count
) {
    std::wstring root = normalize_path(raw_target_path);
    if (root.empty()) {
        return std::unexpected(ERROR_INVALID_PARAMETER);
    }

    LARGE_INTEGER freq, start_time, end_time;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start_time);

    SpaceHogsReport report{};
    report.root_path = root;

    std::priority_queue<FileEntry, std::vector<FileEntry>, CompareFileMin> min_heap;
    std::unordered_map<std::wstring, ExtensionStats> ext_map;
    std::unordered_map<std::wstring, uint64_t> subfolder_sizes;

    // Stack holds pair: {directory_path, immediate_subfolder_name}
    std::vector<std::pair<std::wstring, std::wstring>> dir_stack;

    // Verify root accessibility
    std::wstring initial_pattern = root + L"*";
    WIN32_FIND_DATAW fd;
    HANDLE h_init = FindFirstFileExW(
        initial_pattern.c_str(),
        FindExInfoBasic,
        &fd,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH
    );

    if (h_init == INVALID_HANDLE_VALUE) {
        return std::unexpected(GetLastError());
    }
    FindClose(h_init);

    // Seed stack with root (immediate subfolder marker is empty)
    dir_stack.emplace_back(root, L"");

    while (!dir_stack.empty()) {
        auto [current_dir, top_subfolder] = std::move(dir_stack.back());
        dir_stack.pop_back();

        report.total_directories++;

        std::wstring pattern = current_dir + L"*";
        WIN32_FIND_DATAW find_data;
        HANDLE h_find = FindFirstFileExW(
            pattern.c_str(),
            FindExInfoBasic,
            &find_data,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH
        );

        if (h_find == INVALID_HANDLE_VALUE) {
            continue; // Skip inaccessible folders (e.g. System Volume Information)
        }

        do {
            const wchar_t* name = find_data.cFileName;
            if (name[0] == L'.') {
                if (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0')) {
                    continue; // Skip . and ..
                }
            }

            // Skip junction loops and reparse points
            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                continue;
            }

            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                std::wstring next_dir = current_dir + name + L"\\";
                std::wstring assigned_subfolder = top_subfolder;

                // If currently at root, this directory is an immediate subfolder
                if (assigned_subfolder.empty()) {
                    assigned_subfolder = name;
                }

                dir_stack.emplace_back(std::move(next_dir), assigned_subfolder);
            } else {
                ULARGE_INTEGER file_size;
                file_size.LowPart = find_data.nFileSizeLow;
                file_size.HighPart = find_data.nFileSizeHigh;
                const uint64_t bytes = file_size.QuadPart;

                report.total_files++;
                report.total_bytes += bytes;

                // Top subfolder aggregation
                if (!top_subfolder.empty()) {
                    subfolder_sizes[top_subfolder] += bytes;
                }

                // Extension breakdown
                std::wstring ext = get_extension_lower(name);
                auto& stat = ext_map[ext];
                stat.extension = ext;
                stat.total_bytes += bytes;
                stat.file_count++;

                // Min-heap tracking for top largest files
                if (min_heap.size() < top_count) {
                    min_heap.push(FileEntry{current_dir + name, bytes});
                } else if (!min_heap.empty() && bytes > min_heap.top().size_bytes) {
                    min_heap.pop();
                    min_heap.push(FileEntry{current_dir + name, bytes});
                }
            }
        } while (FindNextFileW(h_find, &find_data));

        FindClose(h_find);
    }

    QueryPerformanceCounter(&end_time);
    report.scan_duration_seconds = static_cast<double>(end_time.QuadPart - start_time.QuadPart) / static_cast<double>(freq.QuadPart);

    // Extract and sort top files in descending order
    while (!min_heap.empty()) {
        report.top_files.push_back(min_heap.top());
        min_heap.pop();
    }
    std::reverse(report.top_files.begin(), report.top_files.end());

    // Extract and sort top extensions
    for (const auto& [_, stat] : ext_map) {
        report.top_extensions.push_back(stat);
    }
    std::sort(report.top_extensions.begin(), report.top_extensions.end(), [](const ExtensionStats& a, const ExtensionStats& b) {
        return a.total_bytes > b.total_bytes;
    });
    if (report.top_extensions.size() > 10) {
        report.top_extensions.resize(10);
    }

    // Extract and sort top subfolders
    for (const auto& [name, bytes] : subfolder_sizes) {
        report.top_subfolders.push_back(FolderSizeEntry{root + name, bytes});
    }
    std::sort(report.top_subfolders.begin(), report.top_subfolders.end(), [](const FolderSizeEntry& a, const FolderSizeEntry& b) {
        return a.size_bytes > b.size_bytes;
    });
    if (report.top_subfolders.size() > 10) {
        report.top_subfolders.resize(10);
    }

    return report;
}

} // namespace clustershift::core
