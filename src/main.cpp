#include "core/win32_utils.hpp"
#include "core/volume_manager.hpp"
#include "core/extent_resolver.hpp"
#include "core/cluster_map.hpp"
#include "core/mover_engine.hpp"
#include "core/trim_engine.hpp"
#include "core/disk_telemetry.hpp"
#include "core/shell_integration.hpp"
#include "core/disk_benchmark.hpp"
#include "core/space_hogs.hpp"
#include "ui/ansi_visualizer.hpp"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <filesystem>
#include <io.h>
#include <fcntl.h>

namespace {

void print_banner() {
    std::wcout << L"\n";
    std::wcout << L"\033[1;36m   ____ _           _            ____  _     _  __ _   \033[0m\n";
    std::wcout << L"\033[1;36m  / ___| |_   _ ___| |_ ___ _ __/ ___|| |__ (_)/ _| |_ \033[0m\n";
    std::wcout << L"\033[1;36m | |   | | | | / __| __/ _ \\ '__\\___ \\| '_ \\| | |_| __|\033[0m\n";
    std::wcout << L"\033[1;36m | |___| | |_| \\__ \\ ||  __/ |   ___) | | | | |  _| |_ \033[0m\n";
    std::wcout << L"\033[1;36m  \\____|_|\\__,_|___/\\__\\___|_|  |____/|_| |_|_|_|  \\__|\033[0m\n";
    std::wcout << L"\033[1;35m  [ ARCHITECT & DEVELOPER: JOIVE ]\033[0m \033[90m- Universal Win32 Storage Optimizer v0.3\033[0m\n\n";
}

void print_usage() {
    std::wcout << L"\033[1;37mUsage:\033[0m clustershift <command> [arguments]\n";
    std::wcout << L"       clustershift (run without arguments for Interactive Dashboard)\n\n";
    std::wcout << L"\033[1;33mCommands:\033[0m\n";
    std::wcout << L"  \033[1;32minfo <drive>\033[0m                                 Query volume geometry, media type & hardware bus\n";
    std::wcout << L"  \033[1;32mhealth <drive>\033[0m                               Check S.M.A.R.T. health, bus & thermal telemetry\n";
    std::wcout << L"  \033[1;32mbench <drive> [--quick]\033[0m                      Direct I/O benchmark (1GB / 128MB, QD32 4K IOPS)\n";
    std::wcout << L"  \033[1;32mhogs <path> [--top N]\033[0m                        Parallel multi-core space-hog scanner\n";
    std::wcout << L"  \033[1;32mmap <drive>\033[0m                                  Render real-time ANSI cluster allocation map\n";
    std::wcout << L"  \033[1;32mscan <path> [--recursive]\033[0m                    Analyze file fragmentation & cluster extents\n";
    std::wcout << L"  \033[1;32mtrim <drive>\033[0m                                 Dispatch fast DSM/TRIM for SSD/NVMe flash maintenance\n";
    std::wcout << L"  \033[1;32moptimize <file> [--dry-run] [--min-frag-size N]\033[0m ZBR-aware cluster relocation (HDD only; skips >= N MB)\n";
    std::wcout << L"  \033[1;32minstall-shell\033[0m                                Add Right-Click context menu to Windows Explorer\n";
    std::wcout << L"  \033[1;32muninstall-shell\033[0m                              Remove Explorer context menu integration\n\n";
}

std::wstring format_size_human(uint64_t bytes) {
    const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 1.0) {
        wchar_t buf[64];
        swprintf_s(buf, L"%.2f GB", gb);
        return buf;
    }
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1.0) {
        wchar_t buf[64];
        swprintf_s(buf, L"%.1f MB", mb);
        return buf;
    }
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb >= 1.0) {
        wchar_t buf[64];
        swprintf_s(buf, L"%.0f KB", kb);
        return buf;
    }
    return std::to_wstring(bytes) + L" B";
}

std::vector<wchar_t> get_system_drives() {
    std::vector<wchar_t> drives;
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (mask & (1 << i)) {
            drives.push_back(static_cast<wchar_t>(L'A' + i));
        }
    }
    return drives;
}

void cmd_info(wchar_t drive_letter) {
    const wchar_t upper = static_cast<wchar_t>(towupper(drive_letter));
    auto details = clustershift::core::VolumeManager::query_volume_details(upper);
    if (!details.has_value()) {
        std::wcout << L"\033[1;31m[-] Failed to query volume details: \033[0m" 
                   << clustershift::core::format_win32_error(details.error()) << L"\n";
        return;
    }

    const auto& d = *details;
    const double total_gb = static_cast<double>(d.total_clusters * d.cluster_size) / (1024.0 * 1024.0 * 1024.0);
    const double free_gb = static_cast<double>(d.free_clusters * d.cluster_size) / (1024.0 * 1024.0 * 1024.0);

    std::wcout << L"\033[1;37m=== Volume Architecture ===\033[0m\n";
    std::wcout << L"  Path:            " << d.path << L"\n";
    std::wcout << L"  File System:     " << d.file_system << L"\n";
    std::wcout << L"  Cluster Size:    " << d.cluster_size << L" bytes\n";
    std::wcout << L"  Total Capacity:  " << std::fixed << std::setprecision(2) << total_gb << L" GB (" << d.total_clusters << L" clusters)\n";
    std::wcout << L"  Free Space:      " << std::fixed << std::setprecision(2) << free_gb << L" GB (" << d.free_clusters << L" clusters)\n";
    
    std::wcout << L"  Media Type:      ";
    if (d.drive_kind == clustershift::core::DriveKind::SolidStateDrive) {
        std::wcout << L"\033[1;32mSolid-State Drive (SSD / NVMe)\033[0m [IncursSeekPenalty = FALSE]\n";
        std::wcout << L"                   \033[90m-> Flash memory active. Use 'trim' to maintain peak write IOPS.\033[0m\n";
    } else if (d.drive_kind == clustershift::core::DriveKind::RotationalHdd) {
        std::wcout << L"\033[1;33mMechanical Hard Disk (HDD)\033[0m [IncursSeekPenalty = TRUE]\n";
        std::wcout << L"                   \033[90m-> Eligible for cluster relocation and continuous alignment.\033[0m\n";
    } else {
        std::wcout << L"\033[1;37mUnknown / Virtual\033[0m\n";
    }
}

void cmd_health(wchar_t drive_letter) {
    const wchar_t upper = static_cast<wchar_t>(towupper(drive_letter));
    auto volume = clustershift::core::VolumeManager::open_volume(upper, false);
    if (!volume.has_value()) {
        std::wcout << L"\033[1;31m[-] Cannot open volume: \033[0m" 
                   << clustershift::core::format_win32_error(volume.error()) << L"\n";
        return;
    }

    auto telem = clustershift::core::DiskTelemetry::query_telemetry(volume->get());
    if (!telem.has_value()) {
        std::wcout << L"\033[1;31m[-] Storage telemetry query failed: \033[0m" 
                   << clustershift::core::format_win32_error(telem.error()) << L"\n";
        return;
    }

    std::wcout << L"\033[1;37m=== Storage Controller & S.M.A.R.T. Health ===\033[0m\n";
    std::wcout << L"  Interface Bus:   \033[1;36m" << clustershift::core::DiskTelemetry::bus_type_to_string(telem->bus_type) << L"\033[0m\n";
    if (!telem->vendor_id.empty()) {
        std::wcout << L"  Vendor:          " << telem->vendor_id << L"\n";
    }
    if (!telem->product_id.empty()) {
        std::wcout << L"  Model / Product: " << telem->product_id << L"\n";
    }
    if (!telem->serial_number.empty()) {
        std::wcout << L"  Serial Number:   " << telem->serial_number << L"\n";
    }

    std::wcout << L"  Failure Status:  ";
    if (telem->is_failure_predicted) {
        std::wcout << L"\033[1;31m[CRITICAL PREDICTED FAILURE - BACKUP DATA IMMEDIATELY]\033[0m\n";
    } else {
        std::wcout << L"\033[1;32m[HEALTHY - No hardware faults flagged]\033[0m\n";
    }

    if (telem->temperature_celsius > 0) {
        std::wcout << L"  Thermal Sensor:  ";
        if (telem->temperature_celsius > 70) {
            std::wcout << L"\033[1;31m" << telem->temperature_celsius << L" °C (HOT / Throttling Risk)\033[0m\n";
        } else {
            std::wcout << L"\033[1;32m" << telem->temperature_celsius << L" °C (Normal)\033[0m\n";
        }
    }
    std::wcout << L"\n";
}

void cmd_bench(wchar_t drive_letter, bool quick_mode) {
    const wchar_t upper = static_cast<wchar_t>(towupper(drive_letter));
    std::wcout << L"[*] Initializing unbuffered storage benchmark on drive " << upper << L": ...\n";
    std::wcout << L"    \033[90m-> Mode: " << (quick_mode ? L"Quick (128 MB)" : L"Standard (1 GB)") 
               << L" | Direct I/O (Bypassing Windows OS File Cache)\033[0m\n";
    std::wcout << L"    \033[90m-> Random 4K Engine: Overlapped Asynchronous I/O (Queue Depth = 32)\033[0m\n\n";

    auto res = clustershift::core::DiskBenchmark::run_benchmark(upper, 1024ULL * 1024 * 1024, quick_mode);
    if (!res.has_value()) {
        std::wcout << L"\033[1;31m[-] Benchmark failed: \033[0m" 
                   << clustershift::core::format_win32_error(res.error()) << L"\n";
        return;
    }

    const auto& m = *res;
    std::wcout << L"\033[1;37m+--------------------------------------------------------------+\033[0m\n";
    std::wcout << L"\033[1;36m|          ClusterShift Hardware Storage Benchmark Results     |\033[0m\n";
    std::wcout << L"\033[1;37m+--------------------------------------------------------------+\033[0m\n";
    std::wcout << L"  Test Target:     Drive " << upper << L":\\ (Unbuffered Direct I/O)\n";
    std::wcout << L"  Sample Size:     " << (m.test_file_size_bytes / (1024 * 1024)) << L" MB\n\n";

    std::wcout << L"  \033[1;33mSequential Write (1MB):\033[0m  \033[1;32m" 
               << std::fixed << std::setprecision(1) << std::setw(8) << m.seq_write_mbps << L" MB/s\033[0m\n";
    std::wcout << L"  \033[1;33mSequential Read  (1MB):\033[0m  \033[1;32m" 
               << std::fixed << std::setprecision(1) << std::setw(8) << m.seq_read_mbps << L" MB/s\033[0m\n";
    std::wcout << L"  \033[1;33mRandom Read 4KB (QD32):\033[0m  \033[1;36m" 
               << std::fixed << std::setprecision(0) << std::setw(8) << m.rnd_4k_read_iops << L" IOPS\033[0m"
               << L" \033[90m(" << std::fixed << std::setprecision(1) << m.rnd_4k_read_mbps << L" MB/s)\033[0m\n";
    
    std::wcout << L"\n  Queue Depth:     " << m.queue_depth << L" (Overlapped Async I/O)\n";
    std::wcout << L"  Total Duration:  " << std::fixed << std::setprecision(2) << m.duration_seconds << L" seconds\n";
    std::wcout << L"\033[1;37m+--------------------------------------------------------------+\033[0m\n\n";
}

void cmd_hogs(const std::wstring& target_path, size_t top_count) {
    std::wcout << L"[*] High-speed parallel space scan on: " << target_path << L" ...\n";
    auto res = clustershift::core::SpaceHogsScanner::scan(target_path, top_count);
    if (!res.has_value()) {
        std::wcout << L"\033[1;31m[-] Scan failed: \033[0m" 
                   << clustershift::core::format_win32_error(res.error()) << L"\n";
        return;
    }

    const auto& r = *res;
    std::wcout << L"\n\033[1;37m=== Space-Hog Analysis: " << r.root_path << L" ===\033[0m\n";
    std::wcout << L"  Scanned: " << r.total_files << L" files across " << r.total_directories 
               << L" directories in \033[1;36m" << std::fixed << std::setprecision(2) << r.scan_duration_seconds << L"s\033[0m\n";
    std::wcout << L"  Total Aggregate Size: \033[1;32m" << format_size_human(r.total_bytes) << L"\033[0m\n\n";

    if (!r.top_files.empty()) {
        std::wcout << L"\033[1;33m[ Top Largest Files ]\033[0m\n";
        for (size_t i = 0; i < r.top_files.size(); ++i) {
            std::wcout << L"  " << std::setw(2) << (i + 1) << L". [\033[1;31m" 
                       << std::setw(10) << format_size_human(r.top_files[i].size_bytes) << L"\033[0m]  " 
                       << r.top_files[i].path << L"\n";
        }
        std::wcout << L"\n";
    }

    if (!r.top_subfolders.empty()) {
        std::wcout << L"\033[1;33m[ Top Subfolders by Capacity ]\033[0m\n";
        for (size_t i = 0; i < r.top_subfolders.size(); ++i) {
            std::wcout << L"  " << std::setw(2) << (i + 1) << L". [\033[1;35m" 
                       << std::setw(10) << format_size_human(r.top_subfolders[i].size_bytes) << L"\033[0m]  " 
                       << r.top_subfolders[i].path << L"\n";
        }
        std::wcout << L"\n";
    }

    if (!r.top_extensions.empty()) {
        std::wcout << L"\033[1;33m[ Top Extensions by Footprint ]\033[0m\n";
        for (const auto& ext : r.top_extensions) {
            std::wcout << L"  " << std::setw(15) << ext.extension 
                       << L" : \033[1;36m" << std::setw(10) << format_size_human(ext.total_bytes) << L"\033[0m"
                       << L" \033[90m(" << ext.file_count << L" files)\033[0m\n";
        }
        std::wcout << L"\n";
    }
}

void cmd_map(wchar_t drive_letter) {
    const wchar_t upper = static_cast<wchar_t>(towupper(drive_letter));
    auto volume = clustershift::core::VolumeManager::open_volume(upper, false);
    if (!volume.has_value()) {
        std::wcout << L"\033[1;31m[-] Cannot open volume: \033[0m" 
                   << clustershift::core::format_win32_error(volume.error()) << L"\n";
        return;
    }

    std::wcout << L"[*] Querying volume cluster allocation bitmap for " << upper << L": ...\n";
    auto bitmap = clustershift::core::ClusterMap::load(volume->get());
    if (!bitmap.has_value()) {
        std::wcout << L"\033[1;31m[-] Failed to load volume bitmap: \033[0m" 
                   << clustershift::core::format_win32_error(bitmap.error()) << L"\n";
        return;
    }

    clustershift::ui::AnsiVisualizer::render_cluster_grid(
        bitmap->raw_bitmap_data(),
        bitmap->total_clusters(),
        64,
        8
    );

    std::wcout << L"  Clusters Total: " << bitmap->total_clusters() 
               << L" | Free Clusters: " << bitmap->free_clusters() << L"\n";
}

void cmd_trim(wchar_t drive_letter) {
    const wchar_t upper = static_cast<wchar_t>(towupper(drive_letter));
    auto volume = clustershift::core::VolumeManager::open_volume(upper, true);
    if (!volume.has_value()) {
        std::wcout << L"\033[1;31m[-] Cannot open volume with write permissions (Run as Administrator): \033[0m" 
                   << clustershift::core::format_win32_error(volume.error()) << L"\n";
        return;
    }

    std::wcout << L"[*] Issuing Storage Management Data Set Attributes (DSM TRIM) on drive " << upper << L": ...\n";
    auto res = clustershift::core::TrimEngine::trim_volume(volume->get());
    if (!res.has_value() || !res->success) {
        const DWORD err = res.has_value() ? res->win32_error : res.error();
        std::wcout << L"\033[1;31m[-] TRIM dispatch failed: \033[0m" 
                   << clustershift::core::format_win32_error(err) << L"\n";
        return;
    }

    std::wcout << L"\033[1;32m[+] TRIM Successfully Dispatched! SSD controller unmapped deallocated blocks.\033[0m\n";
}

void cmd_scan(const std::filesystem::path& target_path, bool recursive) {
    std::error_code ec;
    if (!std::filesystem::exists(target_path, ec)) {
        std::wcout << L"\033[1;31m[-] Target path does not exist:\033[0m " << target_path.wstring() << L"\n";
        return;
    }

    if (std::filesystem::is_regular_file(target_path, ec)) {
        auto rep = clustershift::core::ExtentResolver::analyze_file(target_path);
        if (!rep.has_value()) {
            std::wcout << L"\033[1;31m[-] Failed to resolve extents: \033[0m" 
                       << clustershift::core::format_win32_error(rep.error()) << L"\n";
            return;
        }

        std::wcout << L"\033[1;37m=== File Extent Analysis ===\033[0m\n";
        std::wcout << L"  File:            " << rep->path.filename().wstring() << L"\n";
        std::wcout << L"  Size:            " << (rep->file_size_bytes / (1024 * 1024)) << L" MB (" << rep->file_size_bytes << L" bytes)\n";
        std::wcout << L"  Clusters:        " << rep->total_clusters << L"\n";
        std::wcout << L"  Fragments:       " << (rep->extent_count > 1 ? L"\033[1;31m" : L"\033[1;32m") 
                   << rep->extent_count << L"\033[0m\n";
        std::wcout << L"  Frag Score:      " << std::fixed << std::setprecision(1) << rep->fragmentation_score << L"%\n\n";

        if (rep->extent_count > 1) {
            std::wcout << L"\033[1;33m  Extents Breakdown:\033[0m\n";
            for (size_t i = 0; i < rep->extents.size() && i < 15; ++i) {
                const auto& ext = rep->extents[i];
                std::wcout << L"    [" << std::setw(3) << i << L"] VCN: " << std::setw(8) << ext.starting_vcn 
                            << L" -> LCN: " << std::setw(10) << ext.target_lcn 
                            << L" (" << ext.cluster_count << L" clusters)\n";
            }
            if (rep->extents.size() > 15) {
                std::wcout << L"    ... (" << (rep->extents.size() - 15) << L" more fragments)\n";
            }
        }
    } else if (std::filesystem::is_directory(target_path, ec)) {
        std::wcout << L"[*] Scanning directory: " << target_path.wstring() << L" ...\n";
        auto reports = clustershift::core::ExtentResolver::scan_directory(target_path, recursive, 2);
        
        std::wcout << L"\n\033[1;37mFound " << reports.size() << L" fragmented files (threshold >= 2 fragments):\033[0m\n";
        for (const auto& r : reports) {
            std::wcout << L"  - \033[1;31m[" << std::setw(4) << r.extent_count << L" fragments]\033[0m " 
                       << r.path.filename().wstring() 
                       << L" (" << (r.file_size_bytes / 1024) << L" KB)\n";
        }
    }
}

void cmd_optimize(const std::filesystem::path& file_path, bool dry_run, uint64_t min_frag_size_mb) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file_path, ec)) {
        std::wcout << L"\033[1;31m[-] Target must be a valid regular file:\033[0m " << file_path.wstring() << L"\n";
        return;
    }

    const std::wstring root = file_path.root_name().wstring();
    if (root.empty() || root.size() < 2) {
        std::wcout << L"\033[1;31m[-] Unable to determine root drive letter for file.\033[0m\n";
        return;
    }
    const wchar_t drive_letter = root[0];

    auto volume = clustershift::core::VolumeManager::open_volume(drive_letter, !dry_run);
    if (!volume.has_value()) {
        std::wcout << L"\033[1;31m[-] Cannot open volume: \033[0m" 
                   << clustershift::core::format_win32_error(volume.error()) << L"\n";
        return;
    }

    const auto kind = clustershift::core::VolumeManager::detect_drive_kind(volume->get());
    if (kind == clustershift::core::DriveKind::SolidStateDrive) {
        std::wcout << L"\033[1;33m[!] Hardware Safety Intercept: Target volume is an SSD / NVMe.\033[0m\n";
        std::wcout << L"    Defragmentation is intentionally disabled on flash memory by architectural policy.\n";
        std::wcout << L"    -> Use '\033[1;36mclustershift trim " << drive_letter << L"\033[0m' instead for NAND garbage collection.\n";
        return;
    }

    // Set background thread priority to prevent system / foreground UI stutter
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    std::wcout << L"\033[90m[*] I/O Scheduler: Thread priority set to BELOW_NORMAL to prevent foreground stuttering.\033[0m\n";

    std::wcout << L"[*] Analyzing file clusters...\n";
    auto report = clustershift::core::ExtentResolver::analyze_file(file_path);
    if (!report.has_value()) {
        std::wcout << L"\033[1;31m[-] Analysis failed: \033[0m" 
                   << clustershift::core::format_win32_error(report.error()) << L"\n";
        return;
    }

    if (report->extent_count <= 1) {
        std::wcout << L"\033[1;32m[+] File is already fully contiguous (1 extent). No optimization needed.\033[0m\n";
        return;
    }

    auto vol_details = clustershift::core::VolumeManager::query_volume_details(drive_letter);
    const uint64_t cluster_size = vol_details.has_value() ? vol_details->cluster_size : 4096;
    const uint64_t min_frag_clusters = (min_frag_size_mb * 1024 * 1024) / cluster_size;

    // Evaluate fragments against physical HDD physics (ZBR / large extent filter)
    bool all_large = true;
    for (const auto& ext : report->extents) {
        if (!ext.is_sparse && ext.cluster_count < min_frag_clusters) {
            all_large = false;
            break;
        }
    }

    if (all_large) {
        std::wcout << L"\033[1;32m[+] Physics-Aware Optimization Filter active (--min-frag-size " << min_frag_size_mb << L" MB):\033[0m\n";
        std::wcout << L"    All " << report->extent_count << L" fragments are >= " << min_frag_size_mb << L" MB.\n";
        std::wcout << L"    On rotational HDDs, linear streaming transfer dominates head seek latency (4-12ms).\n";
        std::wcout << L"    -> Skipping redundant physical sector relocation to prevent disk wear.\n";
        return;
    }

    std::wcout << L"[*] File has " << report->extent_count << L" fragments across " 
               << report->total_clusters << L" clusters. Loading volume bitmap...\n";

    auto bitmap = clustershift::core::ClusterMap::load(volume->get());
    if (!bitmap.has_value()) {
        std::wcout << L"\033[1;31m[-] Failed to load volume bitmap: \033[0m" 
                   << clustershift::core::format_win32_error(bitmap.error()) << L"\n";
        return;
    }

    std::wcout << L"[*] ZBR Platter Architecture: Searching for continuous space starting from lowest LCN (outer cylinders) for peak linear bandwidth (220-260 MB/s)...\n";
    auto target_lcn = bitmap->find_contiguous_free_space(report->total_clusters, 0);
    if (!target_lcn.has_value()) {
        std::wcout << L"\033[1;31m[-] No single contiguous block of free clusters large enough on volume.\033[0m\n";
        return;
    }

    if (dry_run) {
        std::wcout << L"\033[1;33m[DRY-RUN SIMULATION]\033[0m\n";
        std::wcout << L"  Target Continuous LCN: " << *target_lcn << L" (Outer Track Zone)\n";
        std::wcout << L"  Clusters to Consolidate: " << report->total_clusters << L"\n";
        std::wcout << L"  Fragments Reduced: " << report->extent_count << L" -> 1\n";
        std::wcout << L"\033[1;32m[+] Dry-run simulation completed safely. No physical sectors were modified.\033[0m\n";
        return;
    }

    std::wcout << L"[*] Found contiguous free block at LCN " << *target_lcn << L". Relocating clusters...\n";
    auto move_res = clustershift::core::MoverEngine::consolidate_file(volume->get(), file_path, *target_lcn);
    if (!move_res.has_value() || !move_res->success) {
        const DWORD err = move_res.has_value() ? move_res->win32_error : move_res.error();
        std::wcout << L"\033[1;31m[-] Relocation failed: \033[0m" 
                   << clustershift::core::format_win32_error(err) << L"\n";
        return;
    }

    std::wcout << L"\033[1;32m[+] Optimization Complete! Moved " << move_res->clusters_moved 
               << L" clusters into monolithic sequence starting at LCN " << move_res->new_starting_lcn << L".\033[0m\n";
}

void cmd_install_shell() {
    auto res = clustershift::core::ShellIntegration::install_context_menu();
    if (!res.has_value()) {
        std::wcout << L"\033[1;31m[-] Failed to install context menu (Run as Administrator): \033[0m" 
                   << clustershift::core::format_win32_error(res.error()) << L"\n";
        return;
    }
    std::wcout << L"\033[1;32m[+] Windows Explorer Integration Installed Successfully!\033[0m\n";
    std::wcout << L"    Right-click any folder -> 'ClusterShift: Scan Fragmentation [by Joive]'\n";
    std::wcout << L"    Right-click any drive  -> 'ClusterShift: Storage Health & Map [by Joive]'\n\n";
}

void cmd_uninstall_shell() {
    auto res = clustershift::core::ShellIntegration::uninstall_context_menu();
    if (!res.has_value()) {
        std::wcout << L"\033[1;31m[-] Failed to remove context menu: \033[0m" 
                   << clustershift::core::format_win32_error(res.error()) << L"\n";
        return;
    }
    std::wcout << L"\033[1;32m[+] Windows Explorer Integration Removed.\033[0m\n\n";
}

void run_interactive_dashboard() {
    while (true) {
        std::wcout << L"\033[1;37m=================================================================\033[0m\n";
        std::wcout << L"\033[1;36m  ClusterShift Universal Storage Console [by Joive] — Dashboard \033[0m\n";
        std::wcout << L"\033[1;37m=================================================================\033[0m\n";

        auto drives = get_system_drives();
        std::wcout << L"\033[1;33mDetected Storage Volumes:\033[0m\n";
        for (wchar_t d : drives) {
            auto details = clustershift::core::VolumeManager::query_volume_details(d);
            if (details.has_value()) {
                const double total_gb = static_cast<double>(details->total_clusters * details->cluster_size) / (1024.0 * 1024.0 * 1024.0);
                const double free_gb = static_cast<double>(details->free_clusters * details->cluster_size) / (1024.0 * 1024.0 * 1024.0);
                std::wcout << L"  [" << d << L":] " 
                           << std::fixed << std::setprecision(1) << total_gb << L" GB (" 
                           << std::fixed << std::setprecision(1) << free_gb << L" GB free) — " 
                           << (details->drive_kind == clustershift::core::DriveKind::SolidStateDrive ? L"\033[1;32mSSD/NVMe\033[0m" : L"\033[1;33mHDD\033[0m") 
                           << L"\n";
            }
        }

        std::wcout << L"\n\033[1;33mInteractive Actions:\033[0m\n";
        std::wcout << L"  \033[1;32m[1]\033[0m Detailed Storage & S.M.A.R.T. Health Telemetry\n";
        std::wcout << L"  \033[1;32m[2]\033[0m Render Real-Time Cluster Allocation Map\n";
        std::wcout << L"  \033[1;32m[3]\033[0m Dispatch Fast Flash TRIM (SSD/NVMe)\n";
        std::wcout << L"  \033[1;32m[4]\033[0m Scan Folder or Game for Fragmentation\n";
        std::wcout << L"  \033[1;32m[5]\033[0m Run Storage Benchmark (QD32 Async 4K IOPS)\n";
        std::wcout << L"  \033[1;32m[6]\033[0m Space-Hog Analyzer (Find Largest Files & Folders)\n";
        std::wcout << L"  \033[1;32m[7]\033[0m Add 1-Click Right-Click Context Menu to Windows Explorer\n";
        std::wcout << L"  \033[1;32m[8]\033[0m Remove Explorer Context Menu\n";
        std::wcout << L"  \033[1;31m[0] Exit\033[0m\n\n";
        std::wcout << L"\033[1;37mSelect an option (0-8): \033[0m";

        std::wstring choice;
        if (!std::getline(std::wcin, choice)) {
            break;
        }

        if (choice == L"0" || choice == L"q" || choice == L"exit") {
            std::wcout << L"Exiting ClusterShift. Keep storage optimized, Joive!\n";
            break;
        }

        if (choice == L"1") {
            std::wcout << L"Enter drive letter (e.g. C): ";
            std::wstring d;
            std::getline(std::wcin, d);
            if (!d.empty()) {
                cmd_info(d[0]);
                cmd_health(d[0]);
            }
        } else if (choice == L"2") {
            std::wcout << L"Enter drive letter (e.g. C): ";
            std::wstring d;
            std::getline(std::wcin, d);
            if (!d.empty()) {
                cmd_map(d[0]);
            }
        } else if (choice == L"3") {
            std::wcout << L"Enter SSD drive letter (e.g. C): ";
            std::wstring d;
            std::getline(std::wcin, d);
            if (!d.empty()) {
                cmd_trim(d[0]);
            }
        } else if (choice == L"4") {
            std::wcout << L"Enter directory path to scan: ";
            std::wstring path;
            std::getline(std::wcin, path);
            if (!path.empty()) {
                cmd_scan(path, true);
            }
        } else if (choice == L"5") {
            std::wcout << L"Enter drive letter to benchmark (e.g. C): ";
            std::wstring d;
            std::getline(std::wcin, d);
            if (!d.empty()) {
                cmd_bench(d[0], false);
            }
        } else if (choice == L"6") {
            std::wcout << L"Enter directory or drive root (e.g. C:\\ or D:\\Games): ";
            std::wstring path;
            std::getline(std::wcin, path);
            if (!path.empty()) {
                cmd_hogs(path, 15);
            }
        } else if (choice == L"7") {
            cmd_install_shell();
        } else if (choice == L"8") {
            cmd_uninstall_shell();
        }

        std::wcout << L"\nPress Enter to continue...";
        std::wstring dummy;
        std::getline(std::wcin, dummy);
        std::wcout << L"\n\n";
    }
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(stdout_handle, &mode)) {
        SetConsoleMode(stdout_handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    print_banner();

    if (!clustershift::core::enable_privilege(SE_MANAGE_VOLUME_NAME)) {
        std::wcout << L"\033[90m[*] Notice: SE_MANAGE_VOLUME_NAME privilege inactive (elevation required for low-level writes).\033[0m\n\n";
    }

    if (argc < 2) {
        run_interactive_dashboard();
        return 0;
    }

    const std::wstring command = argv[1];

    if (command == L"info" && argc >= 3) {
        cmd_info(argv[2][0]);
    } else if (command == L"health" && argc >= 3) {
        cmd_health(argv[2][0]);
    } else if (command == L"bench" && argc >= 3) {
        bool quick = false;
        if (argc >= 4 && std::wstring(argv[3]) == L"--quick") {
            quick = true;
        }
        cmd_bench(argv[2][0], quick);
    } else if (command == L"hogs" && argc >= 3) {
        size_t top_n = 15;
        if (argc >= 5 && std::wstring(argv[3]) == L"--top") {
            top_n = std::stoull(argv[4]);
        }
        cmd_hogs(argv[2], top_n);
    } else if (command == L"map" && argc >= 3) {
        cmd_map(argv[2][0]);
    } else if (command == L"trim" && argc >= 3) {
        cmd_trim(argv[2][0]);
    } else if (command == L"scan" && argc >= 3) {
        bool recursive = false;
        if (argc >= 4 && std::wstring(argv[3]) == L"--recursive") {
            recursive = true;
        }
        cmd_scan(argv[2], recursive);
    } else if (command == L"optimize" && argc >= 3) {
        bool dry_run = false;
        uint64_t min_frag_size = 50; // default 50 MB
        for (int i = 3; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--dry-run") {
                dry_run = true;
            } else if (arg == L"--min-frag-size" && i + 1 < argc) {
                min_frag_size = std::stoull(argv[++i]);
            }
        }
        cmd_optimize(argv[2], dry_run, min_frag_size);
    } else if (command == L"install-shell") {
        cmd_install_shell();
    } else if (command == L"uninstall-shell") {
        cmd_uninstall_shell();
    } else {
        print_usage();
    }

    return 0;
}
