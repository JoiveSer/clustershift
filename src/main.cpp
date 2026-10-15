#include "core/win32_utils.hpp"
#include "core/volume_manager.hpp"
#include "core/extent_resolver.hpp"
#include "core/cluster_map.hpp"
#include "core/mover_engine.hpp"
#include "core/trim_engine.hpp"
#include "core/disk_telemetry.hpp"
#include "core/shell_integration.hpp"
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
    std::wcout << L"\033[1;35m  [ ARCHITECT & DEVELOPER: JOIVE ]\033[0m \033[90m- Universal Win32 Storage Optimizer v0.2\033[0m\n\n";
}

void print_usage() {
    std::wcout << L"\033[1;37mUsage:\033[0m clustershift <command> [arguments]\n";
    std::wcout << L"       clustershift (run without arguments for Interactive Dashboard)\n\n";
    std::wcout << L"\033[1;33mCommands:\033[0m\n";
    std::wcout << L"  \033[1;32minfo <drive>\033[0m                     Query volume geometry, media type & hardware bus\n";
    std::wcout << L"  \033[1;32mhealth <drive>\033[0m                   Check S.M.A.R.T. health, bus & thermal telemetry\n";
    std::wcout << L"  \033[1;32mmap <drive>\033[0m                      Render real-time ANSI cluster allocation map\n";
    std::wcout << L"  \033[1;32mscan <path> [--recursive]\033[0m        Analyze file fragmentation & cluster extents\n";
    std::wcout << L"  \033[1;32mtrim <drive>\033[0m                     Dispatch fast DSM/TRIM for SSD/NVMe flash maintenance\n";
    std::wcout << L"  \033[1;32moptimize <file> [--dry-run]\033[0m     Safely consolidate clusters (HDD only; --dry-run simulates)\n";
    std::wcout << L"  \033[1;32minstall-shell\033[0m                    Add Right-Click context menu to Windows Explorer\n";
    std::wcout << L"  \033[1;32muninstall-shell\033[0m                  Remove Explorer context menu integration\n\n";
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

void cmd_optimize(const std::filesystem::path& file_path, bool dry_run) {
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

    std::wcout << L"[*] File has " << report->extent_count << L" fragments across " 
               << report->total_clusters << L" clusters. Loading volume bitmap...\n";

    auto bitmap = clustershift::core::ClusterMap::load(volume->get());
    if (!bitmap.has_value()) {
        std::wcout << L"\033[1;31m[-] Failed to load volume bitmap: \033[0m" 
                   << clustershift::core::format_win32_error(bitmap.error()) << L"\n";
        return;
    }

    std::wcout << L"[*] Searching for contiguous free run of " << report->total_clusters << L" clusters...\n";
    auto target_lcn = bitmap->find_contiguous_free_space(report->total_clusters);
    if (!target_lcn.has_value()) {
        std::wcout << L"\033[1;31m[-] No single contiguous block of free clusters large enough on volume.\033[0m\n";
        return;
    }

    if (dry_run) {
        std::wcout << L"\033[1;33m[DRY-RUN SIMULATION]\033[0m\n";
        std::wcout << L"  Target Continuous LCN: " << *target_lcn << L"\n";
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
        std::wcout << L"  \033[1;32m[5]\033[0m Add 1-Click Right-Click Context Menu to Windows Explorer\n";
        std::wcout << L"  \033[1;32m[6]\033[0m Remove Explorer Context Menu\n";
        std::wcout << L"  \033[1;31m[0] Exit\033[0m\n\n";
        std::wcout << L"\033[1;37mSelect an option (0-6): \033[0m";

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
            cmd_install_shell();
        } else if (choice == L"6") {
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
        if (argc >= 4 && std::wstring(argv[3]) == L"--dry-run") {
            dry_run = true;
        }
        cmd_optimize(argv[2], dry_run);
    } else if (command == L"install-shell") {
        cmd_install_shell();
    } else if (command == L"uninstall-shell") {
        cmd_uninstall_shell();
    } else {
        print_usage();
    }

    return 0;
}
