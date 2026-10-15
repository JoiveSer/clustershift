#include "core/space_hogs.hpp"
#include <windows.h>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <cwctype>
#include <thread>
#include <mutex>
#include <vector>

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

struct ThreadScanResult {
    uint64_t files{0};
    uint64_t directories{0};
    uint64_t bytes{0};
    std::priority_queue<FileEntry, std::vector<FileEntry>, CompareFileMin> min_heap;
    std::unordered_map<std::wstring, ExtensionStats> ext_map;
    std::unordered_map<std::wstring, uint64_t> subfolder_sizes;
};

void scan_subtree(
    std::wstring start_dir,
    std::wstring top_subfolder_name,
    size_t top_count,
    ThreadScanResult& out
) {
    std::vector<std::wstring> stack;
    stack.push_back(std::move(start_dir));

    while (!stack.empty()) {
        std::wstring current = std::move(stack.back());
        stack.pop_back();

        out.directories++;

        std::wstring pattern = current + L"*";
        WIN32_FIND_DATAW fd;
        HANDLE h_find = FindFirstFileExW(
            pattern.c_str(),
            FindExInfoBasic,
            &fd,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH
        );

        if (h_find == INVALID_HANDLE_VALUE) {
            continue;
        }

        do {
            const wchar_t* name = fd.cFileName;
            if (name[0] == L'.') {
                if (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0')) {
                    continue;
                }
            }

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                continue; // Skip junction loops & symlinks
            }

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                stack.push_back(current + name + L"\\");
            } else {
                ULARGE_INTEGER sz;
                sz.LowPart = fd.nFileSizeLow;
                sz.HighPart = fd.nFileSizeHigh;
                const uint64_t b = sz.QuadPart;

                out.files++;
                out.bytes += b;

                if (!top_subfolder_name.empty()) {
                    out.subfolder_sizes[top_subfolder_name] += b;
                }

                std::wstring ext = get_extension_lower(name);
                auto& stat = out.ext_map[ext];
                stat.extension = ext;
                stat.total_bytes += b;
                stat.file_count++;

                if (out.min_heap.size() < top_count) {
                    out.min_heap.push(FileEntry{current + name, b});
                } else if (!out.min_heap.empty() && b > out.min_heap.top().size_bytes) {
                    out.min_heap.pop();
                    out.min_heap.push(FileEntry{current + name, b});
                }
            }
        } while (FindNextFileW(h_find, &fd));

        FindClose(h_find);
    }
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

    // First, verify access and discover immediate subdirectories at root
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

    report.total_directories = 1;

    struct WorkItem {
        std::wstring dir_path;
        std::wstring subfolder_name;
    };
    std::vector<WorkItem> work_items;

    std::priority_queue<FileEntry, std::vector<FileEntry>, CompareFileMin> root_heap;
    std::unordered_map<std::wstring, ExtensionStats> root_ext_map;

    do {
        const wchar_t* name = fd.cFileName;
        if (name[0] == L'.') {
            if (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0')) {
                continue;
            }
        }

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            continue;
        }

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            work_items.push_back(WorkItem{root + name + L"\\", name});
        } else {
            ULARGE_INTEGER sz;
            sz.LowPart = fd.nFileSizeLow;
            sz.HighPart = fd.nFileSizeHigh;
            const uint64_t b = sz.QuadPart;

            report.total_files++;
            report.total_bytes += b;

            std::wstring ext = get_extension_lower(name);
            auto& stat = root_ext_map[ext];
            stat.extension = ext;
            stat.total_bytes += b;
            stat.file_count++;

            if (root_heap.size() < top_count) {
                root_heap.push(FileEntry{root + name, b});
            } else if (!root_heap.empty() && b > root_heap.top().size_bytes) {
                root_heap.pop();
                root_heap.push(FileEntry{root + name, b});
            }
        }
    } while (FindNextFileW(h_init, &fd));
    FindClose(h_init);

    // Multi-threaded parallel processing of root subtrees
    const unsigned int hw_concurrency = (std::max)(1u, std::thread::hardware_concurrency());
    const size_t num_threads = (std::min)(static_cast<size_t>(hw_concurrency), work_items.size());

    if (num_threads > 1) {
        std::mutex queue_mutex;
        size_t next_idx = 0;
        std::vector<ThreadScanResult> thread_results(num_threads);
        std::vector<std::thread> workers;

        for (size_t t = 0; t < num_threads; ++t) {
            workers.emplace_back([&, t]() {
                while (true) {
                    WorkItem item;
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        if (next_idx >= work_items.size()) {
                            break;
                        }
                        item = work_items[next_idx++];
                    }
                    scan_subtree(item.dir_path, item.subfolder_name, top_count, thread_results[t]);
                }
            });
        }

        for (auto& w : workers) {
            if (w.joinable()) {
                w.join();
            }
        }

        // Merge thread results into master report
        std::unordered_map<std::wstring, uint64_t> combined_subfolders;

        for (auto& tr : thread_results) {
            report.total_files += tr.files;
            report.total_directories += tr.directories;
            report.total_bytes += tr.bytes;

            while (!tr.min_heap.empty()) {
                const auto& fe = tr.min_heap.top();
                if (root_heap.size() < top_count) {
                    root_heap.push(fe);
                } else if (!root_heap.empty() && fe.size_bytes > root_heap.top().size_bytes) {
                    root_heap.pop();
                    root_heap.push(fe);
                }
                tr.min_heap.pop();
            }

            for (const auto& [ext, stat] : tr.ext_map) {
                auto& master = root_ext_map[ext];
                master.extension = ext;
                master.total_bytes += stat.total_bytes;
                master.file_count += stat.file_count;
            }

            for (const auto& [folder, bytes] : tr.subfolder_sizes) {
                combined_subfolders[folder] += bytes;
            }
        }

        for (const auto& [name, bytes] : combined_subfolders) {
            report.top_subfolders.push_back(FolderSizeEntry{root + name, bytes});
        }
    } else {
        // Single thread fallback
        ThreadScanResult single_res;
        for (const auto& item : work_items) {
            scan_subtree(item.dir_path, item.subfolder_name, top_count, single_res);
        }

        report.total_files += single_res.files;
        report.total_directories += single_res.directories;
        report.total_bytes += single_res.bytes;

        while (!single_res.min_heap.empty()) {
            const auto& fe = single_res.min_heap.top();
            if (root_heap.size() < top_count) {
                root_heap.push(fe);
            } else if (!root_heap.empty() && fe.size_bytes > root_heap.top().size_bytes) {
                root_heap.pop();
                root_heap.push(fe);
            }
            single_res.min_heap.pop();
        }

        for (const auto& [ext, stat] : single_res.ext_map) {
            auto& master = root_ext_map[ext];
            master.extension = ext;
            master.total_bytes += stat.total_bytes;
            master.file_count += stat.file_count;
        }

        for (const auto& [name, bytes] : single_res.subfolder_sizes) {
            report.top_subfolders.push_back(FolderSizeEntry{root + name, bytes});
        }
    }

    QueryPerformanceCounter(&end_time);
    report.scan_duration_seconds = static_cast<double>(end_time.QuadPart - start_time.QuadPart) / static_cast<double>(freq.QuadPart);

    // Extract sorted top files
    while (!root_heap.empty()) {
        report.top_files.push_back(root_heap.top());
        root_heap.pop();
    }
    std::reverse(report.top_files.begin(), report.top_files.end());

    // Extract sorted top extensions
    for (const auto& [_, stat] : root_ext_map) {
        report.top_extensions.push_back(stat);
    }
    std::sort(report.top_extensions.begin(), report.top_extensions.end(), [](const ExtensionStats& a, const ExtensionStats& b) {
        return a.total_bytes > b.total_bytes;
    });
    if (report.top_extensions.size() > 10) {
        report.top_extensions.resize(10);
    }

    // Sort top subfolders
    std::sort(report.top_subfolders.begin(), report.top_subfolders.end(), [](const FolderSizeEntry& a, const FolderSizeEntry& b) {
        return a.size_bytes > b.size_bytes;
    });
    if (report.top_subfolders.size() > 10) {
        report.top_subfolders.resize(10);
    }

    return report;
}

} // namespace clustershift::core
