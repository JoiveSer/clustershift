// language: C++, file: gui_main.cpp, target: Windows 10/11, MSVC, DirectX 11 + Dear ImGui
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <iomanip>
#include <sstream>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "gui/modern_theme.hpp"
#include "core/volume_manager.hpp"
#include "core/disk_telemetry.hpp"
#include "core/disk_benchmark.hpp"
#include "core/space_hogs.hpp"
#include "core/cluster_map.hpp"
#include "core/trim_engine.hpp"
#include "core/mover_engine.hpp"
#include "core/shell_integration.hpp"
#include "core/win32_utils.hpp"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

ID3D11Device*           g_pd3dDevice = nullptr;
ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
IDXGISwapChain*         g_pSwapChain = nullptr;
UINT                    g_ResizeWidth = 0, g_ResizeHeight = 0;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

inline std::vector<wchar_t> enumerate_all_drives() {
    std::vector<wchar_t> drives;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (mask & (1 << i)) {
            drives.push_back(static_cast<wchar_t>(L'A' + i));
        }
    }
    return drives;
}

// Application State
struct GuiState {
    int active_tab = 0;
    std::vector<wchar_t> available_drives;
    int selected_drive_idx = 0;

    // Telemetry cache
    clustershift::core::VolumeDetails volume_details{};
    clustershift::core::DiskHealthTelemetry telemetry{};
    bool telemetry_loaded = false;

    // Benchmark state
    std::atomic<bool> bench_running{false};
    clustershift::core::BenchmarkMetrics bench_results{};
    bool bench_has_results = false;
    std::string bench_status_text = "Ready to benchmark";
    int bench_sample_size_mb = 1024;
    int bench_threads = 16;
    bool bench_quick_mode = false;

    // Space Hogs state
    std::atomic<bool> hogs_running{false};
    clustershift::core::SpaceHogsReport hogs_results{};
    bool hogs_has_results = false;
    char hogs_path_input[MAX_PATH] = "C:\\";
    std::string hogs_status_text = "Ready to scan";

    // Cluster Map state
    std::atomic<bool> map_loading{false};
    std::shared_ptr<clustershift::core::ClusterMap> cluster_map_data;
    bool map_loaded = false;
    std::string map_status_text = "Click 'Load Map' to query volume bitmap";

    // Maintenance state
    std::string trim_result_msg;
    int min_frag_size_mb = 50;
    bool defrag_dry_run = true;
    std::string defrag_result_msg;
    bool context_menu_installed = false;
};

GuiState g_state;

void RefreshDrives() {
    g_state.available_drives = enumerate_all_drives();
    if (!g_state.available_drives.empty()) {
        g_state.selected_drive_idx = 0;
        wchar_t drv = g_state.available_drives[0];
        auto vol = clustershift::core::VolumeManager::open_volume(drv, false);
        if (vol.has_value()) {
            auto telem = clustershift::core::DiskTelemetry::query_telemetry(vol->get());
            if (telem.has_value()) {
                g_state.telemetry = *telem;
            }
        }
        auto det = clustershift::core::VolumeManager::query_volume_details(drv);
        if (det.has_value()) {
            g_state.volume_details = *det;
            g_state.telemetry_loaded = true;
        }
    }
}

} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW wc = {
        sizeof(wc),
        CS_CLASSDC,
        WndProc,
        0L, 0L,
        hInstance,
        nullptr, nullptr, nullptr, nullptr,
        L"ClusterShift_GUI_Class",
        nullptr
    };
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowW(
        wc.lpszClassName,
        L"ClusterShift v0.6.0 [by Joive] — Hardware Storage Suite",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1100, 720,
        nullptr, nullptr,
        wc.hInstance,
        nullptr
    );

    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    if (!CreateDeviceD3D(hWnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    clustershift::gui::apply_modern_theme();

    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    clustershift::core::set_se_manage_volume_privilege();
    RefreshDrives();

    g_state.bench_threads = (std::max)(1u, std::thread::hardware_concurrency());

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
        }
        if (done) break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | 
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin("HostWindow", nullptr, host_flags);

        // Header Banner
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.94f, 1.0f, 1.0f));
        ImGui::SetWindowFontScale(1.3f);
        ImGui::Text("  ClusterShift");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.48f, 0.16f, 0.79f, 1.0f));
        ImGui::Text("v0.6.0");
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextDisabled("| Architect: Joive");

        ImGui::SameLine(ImGui::GetWindowWidth() - 220);
        if (clustershift::core::is_user_elevated()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "[ ADMIN / ELEVATED ]");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "[ STANDARD USER ]");
        }

        ImGui::Separator();
        ImGui::Spacing();

        // 2-Column Layout: Sidebar (Left) + Content (Right)
        ImGui::Columns(2, "MainColumns", false);
        ImGui::SetColumnWidth(0, 200.0f);

        // Sidebar Navigation
        ImGui::BeginChild("SidebarChild", ImVec2(185.0f, -1.0f), true);
        ImGui::TextDisabled("NAVIGATION");
        ImGui::Spacing();

        const char* tab_names[] = {
            "  Dashboard",
            "  Benchmark",
            "  Space Hogs",
            "  Cluster Map",
            "  Maintenance"
        };

        for (int i = 0; i < 5; ++i) {
            bool is_selected = (g_state.active_tab == i);
            if (is_selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.94f, 1.0f, 0.25f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.94f, 1.0f, 1.0f));
            }

            if (ImGui::Button(tab_names[i], ImVec2(-1.0f, 40.0f))) {
                g_state.active_tab = i;
            }

            if (is_selected) {
                ImGui::PopStyleColor(2);
            }
            ImGui::Spacing();
        }

        ImGui::EndChild();

        // Right Column: Main Content
        ImGui::NextColumn();
        ImGui::BeginChild("ContentChild", ImVec2(-1.0f, -1.0f), true);

        // TAB 0: DASHBOARD
        if (g_state.active_tab == 0) {
            ImGui::TextColored(ImVec4(0.0f, 0.94f, 1.0f, 1.0f), "STORAGE DASHBOARD & TELEMETRY");
            ImGui::Separator();
            ImGui::Spacing();

            // Drive Selector Combo
            if (!g_state.available_drives.empty()) {
                std::vector<std::string> combo_items;
                for (wchar_t d : g_state.available_drives) {
                    combo_items.push_back(std::string("Drive ") + static_cast<char>(d) + ":\\");
                }
                const char* preview = combo_items[g_state.selected_drive_idx].c_str();
                ImGui::SetNextItemWidth(250.0f);
                if (ImGui::BeginCombo("Select Drive", preview)) {
                    for (int n = 0; n < static_cast<int>(combo_items.size()); n++) {
                        const bool is_selected = (g_state.selected_drive_idx == n);
                        if (ImGui::Selectable(combo_items[n].c_str(), is_selected)) {
                            g_state.selected_drive_idx = n;
                            wchar_t drv = g_state.available_drives[n];
                            auto vol = clustershift::core::VolumeManager::open_volume(drv, false);
                            if (vol.has_value()) {
                                auto telem = clustershift::core::DiskTelemetry::query_telemetry(vol->get());
                                if (telem.has_value()) g_state.telemetry = *telem;
                            }
                            auto det = clustershift::core::VolumeManager::query_volume_details(drv);
                            if (det.has_value()) g_state.volume_details = *det;
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::Spacing();

            // Volume Capacity Progress Bar
            if (g_state.volume_details.total_clusters > 0) {
                double total_gb = (double)(g_state.volume_details.total_clusters * g_state.volume_details.cluster_size) / (1024.0 * 1024.0 * 1024.0);
                double free_gb = (double)(g_state.volume_details.free_clusters * g_state.volume_details.cluster_size) / (1024.0 * 1024.0 * 1024.0);
                double used_gb = total_gb - free_gb;
                float fraction = static_cast<float>(used_gb / total_gb);

                char overlay[128];
                snprintf(overlay, sizeof(overlay), "%.1f GB used / %.1f GB total (%.1f%%)", used_gb, total_gb, fraction * 100.0f);
                ImGui::ProgressBar(fraction, ImVec2(-1.0f, 26.0f), overlay);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Telemetry Metric Cards
            ImGui::Columns(2, "TelemCards", false);

            // Card 1: Hardware Model & Bus
            ImGui::BeginChild("Card1", ImVec2(-1.0f, 130.0f), true);
            ImGui::TextDisabled("HARDWARE CONTROLLER");
            ImGui::Spacing();
            ImGui::Text("Bus: %ls", clustershift::core::DiskTelemetry::bus_type_to_string(g_state.telemetry.bus_type));
            if (!g_state.telemetry.product_id.empty()) {
                ImGui::Text("Model: %ls", g_state.telemetry.product_id.c_str());
            }
            if (!g_state.telemetry.serial_number.empty()) {
                ImGui::TextDisabled("S/N: %ls", g_state.telemetry.serial_number.c_str());
            }
            ImGui::EndChild();

            // Card 2: S.M.A.R.T. & Thermal
            ImGui::NextColumn();
            ImGui::BeginChild("Card2", ImVec2(-1.0f, 130.0f), true);
            ImGui::TextDisabled("S.M.A.R.T. & THERMALS");
            ImGui::Spacing();
            if (g_state.telemetry.is_failure_predicted) {
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[ CRITICAL FAULT PREDICTED ]");
            } else {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "[ HEALTHY - No Faults Flagged ]");
            }
            ImGui::Spacing();
            if (g_state.telemetry.temperature_celsius > 0) {
                ImGui::Text("Controller Temp: %d °C", g_state.telemetry.temperature_celsius);
            } else {
                ImGui::TextDisabled("Thermal Sensor: N/A");
            }
            ImGui::EndChild();

            ImGui::Columns(1);
            ImGui::Spacing();

            // Card 3: DirectStorage / BypassIO Status
            ImGui::BeginChild("Card3", ImVec2(-1.0f, 110.0f), true);
            ImGui::TextDisabled("DIRECTSTORAGE / BYPASSIO DIAGNOSTIC");
            ImGui::Spacing();
            if (g_state.telemetry.bypass_io.is_supported) {
                ImGui::TextColored(ImVec4(0.0f, 0.94f, 1.0f, 1.0f), "[ ACTIVE & OPTIMAL ] Hardware Direct GPU Asset Streaming Ready");
            } else if (g_state.telemetry.bypass_io.is_filter_blocked) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "[ BLOCKED BY FILTER DRIVER ]");
                if (!g_state.telemetry.bypass_io.failing_driver.empty()) {
                    ImGui::Text("Driver: %ls", g_state.telemetry.bypass_io.failing_driver.c_str());
                }
                if (!g_state.telemetry.bypass_io.failure_reason.empty()) {
                    ImGui::TextDisabled("Reason: %ls", g_state.telemetry.bypass_io.failure_reason.c_str());
                }
            } else if (!g_state.telemetry.bypass_io.failure_reason.empty()) {
                ImGui::TextDisabled("Status: %ls", g_state.telemetry.bypass_io.failure_reason.c_str());
            } else {
                ImGui::TextDisabled("Status: Legacy volume stack (Not active)");
            }
            ImGui::EndChild();
        }

        // TAB 1: BENCHMARK
        else if (g_state.active_tab == 1) {
            ImGui::TextColored(ImVec4(0.0f, 0.94f, 1.0f, 1.0f), "UNBUFFERED HARDWARE STORAGE BENCHMARK");
            ImGui::TextDisabled("Direct I/O (FILE_FLAG_NO_BUFFERING) | Parallel IOCP Queue | Sub-Microsecond Latency");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Sample Size:");
            ImGui::SameLine();
            if (ImGui::RadioButton("128 MB (Quick)", g_state.bench_sample_size_mb == 128)) {
                g_state.bench_sample_size_mb = 128;
                g_state.bench_quick_mode = true;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("1024 MB (Standard)", g_state.bench_sample_size_mb == 1024)) {
                g_state.bench_sample_size_mb = 1024;
                g_state.bench_quick_mode = false;
            }

            ImGui::Spacing();
            ImGui::SetNextItemWidth(250.0f);
            ImGui::SliderInt("Worker Threads", &g_state.bench_threads, 1, 32);

            ImGui::Spacing();

            if (!g_state.bench_running) {
                if (ImGui::Button("  START UNBUFFERED BENCHMARK  ", ImVec2(280.0f, 44.0f))) {
                    g_state.bench_running = true;
                    g_state.bench_has_results = false;
                    g_state.bench_status_text = "Running unbuffered hardware tests...";

                    wchar_t drv = g_state.available_drives.empty() ? L'C' : g_state.available_drives[g_state.selected_drive_idx];
                    uint64_t sz = static_cast<uint64_t>(g_state.bench_sample_size_mb) * 1024 * 1024;
                    bool qk = g_state.bench_quick_mode;
                    uint32_t th = static_cast<uint32_t>(g_state.bench_threads);

                    std::thread([drv, sz, qk, th]() {
                        auto res = clustershift::core::DiskBenchmark::run_benchmark(drv, sz, qk, th);
                        if (res.has_value()) {
                            g_state.bench_results = *res;
                            g_state.bench_has_results = true;
                            g_state.bench_status_text = "Benchmark completed successfully!";
                        } else {
                            g_state.bench_status_text = "Benchmark failed (Access Denied or I/O error)";
                        }
                        g_state.bench_running = false;
                    }).detach();
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.48f, 0.16f, 0.79f, 0.6f));
                ImGui::Button("  BENCHMARK IN PROGRESS...  ", ImVec2(280.0f, 44.0f));
                ImGui::PopStyleColor();
            }

            ImGui::SameLine();
            ImGui::TextDisabled("%s", g_state.bench_status_text.c_str());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (g_state.bench_has_results) {
                ImGui::Columns(2, "BenchCols", false);

                // Card: Sequential Write
                ImGui::BeginChild("SeqW", ImVec2(-1.0f, 90.0f), true);
                ImGui::TextDisabled("SEQUENTIAL WRITE (1 MB)");
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.5f, 1.0f));
                ImGui::SetWindowFontScale(1.4f);
                ImGui::Text("%.1f MB/s", g_state.bench_results.seq_write_mbps);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
                ImGui::EndChild();

                // Card: Sequential Read
                ImGui::NextColumn();
                ImGui::BeginChild("SeqR", ImVec2(-1.0f, 90.0f), true);
                ImGui::TextDisabled("SEQUENTIAL READ (1 MB)");
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.5f, 1.0f));
                ImGui::SetWindowFontScale(1.4f);
                ImGui::Text("%.1f MB/s", g_state.bench_results.seq_read_mbps);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
                ImGui::EndChild();

                ImGui::NextColumn();
                // Card: Random 4K Q32Tx Peak IOPS
                ImGui::BeginChild("Rnd4KQ32", ImVec2(-1.0f, 100.0f), true);
                ImGui::TextDisabled("RANDOM 4KB READ (Q32T%u)", g_state.bench_results.thread_count);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.94f, 1.0f, 1.0f));
                ImGui::SetWindowFontScale(1.4f);
                ImGui::Text("%.0f IOPS", g_state.bench_results.rnd_4k_read_iops);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
                ImGui::TextDisabled("%.1f MB/s (Hardware Queue Saturation)", g_state.bench_results.rnd_4k_read_mbps);
                ImGui::EndChild();

                // Card: Random 4K Q1T1 Latency
                ImGui::NextColumn();
                ImGui::BeginChild("Rnd4KQ1T1", ImVec2(-1.0f, 100.0f), true);
                ImGui::TextDisabled("RANDOM 4KB LATENCY (Q1T1)");
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.84f, 0.0f, 1.0f));
                ImGui::SetWindowFontScale(1.4f);
                ImGui::Text("%.1f us avg", g_state.bench_results.rnd_4k_q1t1_avg_lat_us);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
                ImGui::TextDisabled("Min: %.1f us | Max: %.1f us", g_state.bench_results.rnd_4k_q1t1_min_lat_us, g_state.bench_results.rnd_4k_q1t1_max_lat_us);
                ImGui::EndChild();

                ImGui::Columns(1);
            }
        }

        // TAB 2: SPACE HOGS
        else if (g_state.active_tab == 2) {
            ImGui::TextColored(ImVec4(0.0f, 0.94f, 1.0f, 1.0f), "SPACE-HOG DISK ANALYZER");
            ImGui::TextDisabled("Hardware-Aware Multi-Core Crawl (SSD) / Sequential Anti-Thrash (HDD)");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::SetNextItemWidth(350.0f);
            ImGui::InputText("Target Directory", g_state.hogs_path_input, sizeof(g_state.hogs_path_input));
            ImGui::SameLine();

            if (!g_state.hogs_running) {
                if (ImGui::Button("  START SCAN  ", ImVec2(140.0f, 32.0f))) {
                    g_state.hogs_running = true;
                    g_state.hogs_has_results = false;
                    g_state.hogs_status_text = "Scanning directory tree...";

                    std::string path_str(g_state.hogs_path_input);
                    std::thread([path_str]() {
                        std::wstring wpath(path_str.begin(), path_str.end());
                        auto res = clustershift::core::SpaceHogsScanner::scan(wpath, 20);
                        if (res.has_value()) {
                            g_state.hogs_results = *res;
                            g_state.hogs_has_results = true;
                            g_state.hogs_status_text = "Scan complete!";
                        } else {
                            g_state.hogs_status_text = "Scan failed (Directory inaccessible)";
                        }
                        g_state.hogs_running = false;
                    }).detach();
                }
            } else {
                ImGui::Button("Scanning...", ImVec2(140.0f, 32.0f));
            }

            ImGui::SameLine();
            ImGui::TextDisabled("%s", g_state.hogs_status_text.c_str());

            ImGui::Spacing();
            ImGui::Separator();

            if (g_state.hogs_has_results) {
                ImGui::Text("Scanned %llu files across %llu folders in %.2fs (Total: %.2f GB)",
                    g_state.hogs_results.total_files,
                    g_state.hogs_results.total_directories,
                    g_state.hogs_results.scan_duration_seconds,
                    (double)g_state.hogs_results.total_bytes / (1024.0 * 1024.0 * 1024.0));

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.0f, 0.94f, 1.0f, 1.0f), "TOP LARGEST FILES");

                if (ImGui::BeginTable("HogsFilesTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(-1.0f, 240.0f))) {
                    ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed, 45.0f);
                    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("File Path", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    int rank = 1;
                    for (const auto& item : g_state.hogs_results.top_files) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%d", rank++);
                        ImGui::TableSetColumnIndex(1);
                        double sz_mb = (double)item.size_bytes / (1024.0 * 1024.0);
                        if (sz_mb >= 1024.0) {
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%.2f GB", sz_mb / 1024.0);
                        } else {
                            ImGui::Text("%.1f MB", sz_mb);
                        }
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%ls", item.path.c_str());
                    }
                    ImGui::EndTable();
                }
            }
        }

        // TAB 3: CLUSTER MAP
        else if (g_state.active_tab == 3) {
            ImGui::TextColored(ImVec4(0.0f, 0.94f, 1.0f, 1.0f), "NTFS REAL-TIME CLUSTER ALLOCATION GRID");
            ImGui::TextDisabled("Hardware Volume Cluster Representation | $MFT Protected Zone Lockout");
            ImGui::Separator();
            ImGui::Spacing();

            if (!g_state.map_loading) {
                if (ImGui::Button("  LOAD CLUSTER BITMAP  ", ImVec2(200.0f, 36.0f))) {
                    g_state.map_loading = true;
                    g_state.map_status_text = "Querying NTFS volume bitmap...";

                    wchar_t drv = g_state.available_drives.empty() ? L'C' : g_state.available_drives[g_state.selected_drive_idx];
                    std::thread([drv]() {
                        auto vol = clustershift::core::VolumeManager::open_volume(drv, false);
                        if (vol.has_value()) {
                            auto map = clustershift::core::ClusterMap::load(vol->get());
                            if (map.has_value()) {
                                g_state.cluster_map_data = std::make_shared<clustershift::core::ClusterMap>(std::move(*map));
                                g_state.map_loaded = true;
                                g_state.map_status_text = "Cluster bitmap loaded successfully!";
                            } else {
                                g_state.map_status_text = "Failed to query cluster bitmap (Admin rights required)";
                            }
                        } else {
                            g_state.map_status_text = "Cannot open volume device";
                        }
                        g_state.map_loading = false;
                    }).detach();
                }
            } else {
                ImGui::Button("Reading Bitmap...", ImVec2(200.0f, 36.0f));
            }

            ImGui::SameLine();
            ImGui::TextDisabled("%s", g_state.map_status_text.c_str());

            ImGui::Spacing();

            // Legend
            ImGui::Text("Legend: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 0.94f, 1.0f, 1.0f), "[■] Allocated");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "[■] Free Space");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.48f, 0.16f, 0.79f, 1.0f), "[■] $MFT Reserved Zone");

            ImGui::Spacing();

            // Interactive 2D Grid Canvas
            ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
            ImVec2 canvas_sz = ImVec2(ImGui::GetContentRegionAvail().x, 260.0f);
            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            draw_list->AddRectFilled(canvas_p0, ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y), IM_COL32(19, 21, 31, 255));
            draw_list->AddRect(canvas_p0, ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y), IM_COL32(35, 39, 58, 255));

            if (g_state.map_loaded && g_state.cluster_map_data) {
                const int cols = 64;
                const int rows = 16;
                const float cell_w = (canvas_sz.x - 4.0f) / cols;
                const float cell_h = (canvas_sz.y - 4.0f) / rows;
                const uint64_t total_c = g_state.cluster_map_data->total_clusters();
                const uint64_t clusters_per_cell = total_c / (cols * rows);

                for (int r = 0; r < rows; ++r) {
                    for (int c = 0; c < cols; ++c) {
                        uint64_t lcn = (r * cols + c) * clusters_per_cell;
                        ImU32 col = IM_COL32(0, 240, 255, 220); // Allocated cyan

                        if (g_state.cluster_map_data->is_cluster_free(lcn)) {
                            col = IM_COL32(0, 255, 128, 220); // Free green
                        }

                        ImVec2 p_min(canvas_p0.x + 2.0f + c * cell_w, canvas_p0.y + 2.0f + r * cell_h);
                        ImVec2 p_max(p_min.x + cell_w - 1.0f, p_min.y + cell_h - 1.0f);
                        draw_list->AddRectFilled(p_min, p_max, col);
                    }
                }
            } else {
                draw_list->AddText(ImVec2(canvas_p0.x + 20.0f, canvas_p0.y + 110.0f), IM_COL32(140, 150, 170, 255), "Cluster bitmap not loaded. Click 'LOAD CLUSTER BITMAP' above.");
            }

            ImGui::Dummy(canvas_sz);
        }

        // TAB 4: MAINTENANCE
        else if (g_state.active_tab == 4) {
            ImGui::TextColored(ImVec4(0.0f, 0.94f, 1.0f, 1.0f), "SYSTEM MAINTENANCE & CONTEXT INTEGRATION");
            ImGui::Separator();
            ImGui::Spacing();

            // Section 1: SSD TRIM
            ImGui::BeginChild("TrimSection", ImVec2(-1.0f, 120.0f), true);
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "FLASH STORAGE TRIM ENGINE");
            ImGui::TextDisabled("Issues instant kernel DeviceDsmAction_Trim to preserve flash endurance & write IOPS.");
            ImGui::Spacing();

            if (ImGui::Button("  DISPATCH INSTANT TRIM  ", ImVec2(220.0f, 36.0f))) {
                wchar_t drv = g_state.available_drives.empty() ? L'C' : g_state.available_drives[g_state.selected_drive_idx];
                auto vol = clustershift::core::VolumeManager::open_volume(drv, true);
                if (vol.has_value()) {
                    auto res = clustershift::core::TrimEngine::trim_volume(vol->get());
                    if (res.has_value()) {
                        g_state.trim_result_msg = "TRIM successfully completed for drive " + std::string(1, (char)drv) + ":";
                    } else {
                        g_state.trim_result_msg = "TRIM failed: " + clustershift::core::format_win32_error_a(res.error());
                    }
                } else {
                    g_state.trim_result_msg = "Cannot open volume for write (Run as Administrator)";
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", g_state.trim_result_msg.c_str());
            ImGui::EndChild();

            ImGui::Spacing();

            // Section 2: Explorer Shell Menu
            ImGui::BeginChild("ShellSection", ImVec2(-1.0f, 120.0f), true);
            ImGui::TextColored(ImVec4(0.48f, 0.16f, 0.79f, 1.0f), "WINDOWS EXPLORER CONTEXT MENU");
            ImGui::TextDisabled("Integrates 1-Click ClusterShift actions directly into right-click folder context menu.");
            ImGui::Spacing();

            if (ImGui::Button("  INSTALL EXPLORER MENU  ", ImVec2(220.0f, 36.0f))) {
                auto ok = clustershift::core::ShellIntegration::install_context_menu();
                g_state.context_menu_installed = ok.has_value();
            }
            ImGui::SameLine();
            if (ImGui::Button("  REMOVE EXPLORER MENU  ", ImVec2(220.0f, 36.0f))) {
                clustershift::core::ShellIntegration::uninstall_context_menu();
                g_state.context_menu_installed = false;
            }
            ImGui::EndChild();
        }

        ImGui::EndChild();
        ImGui::Columns(1);
        ImGui::End();

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.047f, 0.051f, 0.078f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // VSync enabled for silky 60+ FPS
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

namespace {

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
    );
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
            &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
        );
    }
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            g_ResizeWidth = (UINT)LOWORD(lParam);
            g_ResizeHeight = (UINT)HIWORD(lParam);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace
