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
#include <filesystem>
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

// Fonts
ImFont* g_font_regular = nullptr;
ImFont* g_font_bold = nullptr;
ImFont* g_font_title = nullptr;
ImFont* g_font_mono = nullptr;

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

struct GuiState {
    int active_tab = 0;
    std::vector<wchar_t> available_drives;
    int selected_drive_idx = 0;

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

// Custom Render Helpers for Linear / Raycast Style
void RenderSectionHeader(const char* title) {
    ImGui::PushStyleColor(ImGuiCol_Text, clustershift::gui::Palette::TextMuted);
    if (g_font_bold) ImGui::PushFont(g_font_bold);
    ImGui::TextUnformatted(title);
    if (g_font_bold) ImGui::PopFont();
    ImGui::PopStyleColor();
}

void RenderCardHeader(const char* title, const char* icon = nullptr) {
    ImGui::PushStyleColor(ImGuiCol_Text, clustershift::gui::Palette::TextMuted);
    if (g_font_bold) ImGui::PushFont(g_font_bold);
    ImGui::TextUnformatted(title);
    if (g_font_bold) ImGui::PopFont();
    ImGui::PopStyleColor();

    if (icon) {
        float text_width = ImGui::CalcTextSize(icon).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - text_width - 16.0f);
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "%s", icon);
    }
}

void RenderKeyValue(const char* key, const char* value, bool mono_value = false) {
    ImGui::TextColored(clustershift::gui::Palette::TextMuted, "%s", key);
    ImGui::SameLine(160.0f);
    if (mono_value && g_font_mono) ImGui::PushFont(g_font_mono);
    ImGui::TextColored(clustershift::gui::Palette::TextPrimary, "%s", value);
    if (mono_value && g_font_mono) ImGui::PopFont();
}

void RenderKeyValueW(const char* key, const wchar_t* value, bool mono_value = false) {
    ImGui::TextColored(clustershift::gui::Palette::TextMuted, "%s", key);
    ImGui::SameLine(160.0f);
    if (mono_value && g_font_mono) ImGui::PushFont(g_font_mono);
    ImGui::TextColored(clustershift::gui::Palette::TextPrimary, "%ls", value);
    if (mono_value && g_font_mono) ImGui::PopFont();
}

// Sleek Pill Progress Bar (height 8px, gradient cyan/teal, border-radius 999px)
void RenderSleekProgressBar(float fraction) {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float bar_width = ImGui::GetContentRegionAvail().x;
    float bar_height = 8.0f;
    ImVec2 p1 = ImVec2(p0.x + bar_width, p0.y + bar_height);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Background track
    draw_list->AddRectFilled(p0, p1, IM_COL32(17, 20, 29, 255), 999.0f);
    draw_list->AddRect(p0, p1, IM_COL32(255, 255, 255, 18), 999.0f, 0, 1.0f);

    // Filled portion with subtle gradient
    if (fraction > 0.001f) {
        float fill_width = (std::max)(bar_height, bar_width * (std::min)(1.0f, fraction));
        ImVec2 fill_p1 = ImVec2(p0.x + fill_width, p0.y + bar_height);
        
        ImU32 col_left = IM_COL32(14, 165, 233, 255);  // #0EA5E9
        ImU32 col_right = IM_COL32(20, 184, 166, 255); // #14B8A6
        draw_list->AddRectFilledMultiColor(p0, fill_p1, col_left, col_right, col_right, col_left);
    }

    ImGui::Dummy(ImVec2(bar_width, bar_height));
}

// Custom Left-Aligned Sidebar Nav Button
bool RenderNavButton(const char* label, const char* icon, bool is_active) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 38.0f;
    ImVec2 p1 = ImVec2(p0.x + width, p0.y + height);

    ImGui::InvisibleButton(label, ImVec2(width, height));
    bool is_hovered = ImGui::IsItemHovered();
    bool is_clicked = ImGui::IsItemClicked();

    if (is_active) {
        // Subtle teal/cyan fill (15% opacity) + sleek glowing border
        draw_list->AddRectFilled(p0, p1, IM_COL32(14, 165, 233, 38), 6.0f);
        draw_list->AddRect(p0, p1, IM_COL32(14, 165, 233, 140), 6.0f, 0, 1.0f);

        // Active left accent bar (3px)
        draw_list->AddRectFilled(ImVec2(p0.x, p0.y + 6.0f), ImVec2(p0.x + 3.0f, p1.y - 6.0f), IM_COL32(56, 189, 248, 255), 2.0f);
    } else if (is_hovered) {
        draw_list->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 10), 6.0f);
    }

    // Render Icon + Text left-aligned
    ImU32 text_col = is_active ? IM_COL32(56, 189, 248, 255) : (is_hovered ? IM_COL32(243, 244, 246, 255) : IM_COL32(156, 163, 175, 255));
    
    char full_text[128];
    snprintf(full_text, sizeof(full_text), "%s   %s", icon, label);

    ImVec2 text_pos = ImVec2(p0.x + 14.0f, p0.y + (height - ImGui::GetTextLineHeight()) * 0.5f);
    draw_list->AddText(text_pos, text_col, full_text);

    return is_clicked;
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

    // Load System Fonts (Segoe UI for sleek desktop feel, Consolas for raw numbers)
    if (std::filesystem::exists("C:\\Windows\\Fonts\\segoeui.ttf")) {
        g_font_regular = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f);
    }
    if (std::filesystem::exists("C:\\Windows\\Fonts\\segoeuib.ttf")) {
        g_font_bold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 16.0f);
        g_font_title = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 20.0f);
    }
    if (std::filesystem::exists("C:\\Windows\\Fonts\\consola.ttf")) {
        g_font_mono = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 15.0f);
    }

    clustershift::gui::apply_modern_theme();

    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    (void)clustershift::core::set_se_manage_volume_privilege();
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

        // Top App Bar
        if (g_font_title) ImGui::PushFont(g_font_title);
        ImGui::TextColored(clustershift::gui::Palette::TextPrimary, "ClusterShift");
        if (g_font_title) ImGui::PopFont();

        ImGui::SameLine();
        ImGui::TextColored(clustershift::gui::Palette::AccentLight, "v0.6.0");

        ImGui::SameLine();
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "| Architect: Joive");

        // Privilege Badge Pill
        ImGui::SameLine(ImGui::GetWindowWidth() - 210.0f);
        if (clustershift::core::is_user_elevated()) {
            ImGui::TextColored(clustershift::gui::Palette::SuccessEmerald, "[ ADMIN / ELEVATED ]");
        } else {
            ImGui::TextColored(clustershift::gui::Palette::WarningAmber, "[ STANDARD USER ]");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 2-Column Split: Navigation Sidebar (Left) + View Content (Right)
        ImGui::Columns(2, "MainLayoutColumns", false);
        ImGui::SetColumnWidth(0, 190.0f);

        // Sidebar Navigation
        ImGui::BeginChild("SidebarChild", ImVec2(175.0f, -1.0f), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::PushStyleColor(ImGuiCol_Text, clustershift::gui::Palette::TextMuted);
        ImGui::TextUnformatted("NAVIGATION");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        if (RenderNavButton("Dashboard", "[#]", g_state.active_tab == 0)) g_state.active_tab = 0;
        ImGui::Spacing();
        if (RenderNavButton("Benchmark", "[~]", g_state.active_tab == 1)) g_state.active_tab = 1;
        ImGui::Spacing();
        if (RenderNavButton("Space Hogs", "[*]", g_state.active_tab == 2)) g_state.active_tab = 2;
        ImGui::Spacing();
        if (RenderNavButton("Cluster Map", "[+]", g_state.active_tab == 3)) g_state.active_tab = 3;
        ImGui::Spacing();
        if (RenderNavButton("Maintenance", "[!]", g_state.active_tab == 4)) g_state.active_tab = 4;

        ImGui::EndChild();

        // Right Column: Main Content
        ImGui::NextColumn();
        ImGui::BeginChild("ContentChild", ImVec2(-1.0f, -1.0f), false);

        // ==========================================
        // TAB 0: DASHBOARD
        // ==========================================
        if (g_state.active_tab == 0) {
            RenderSectionHeader("STORAGE DASHBOARD & TELEMETRY");
            ImGui::Spacing();

            // Drive Selector (Left) + Capacity text (Right)
            float total_width = ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemWidth(180.0f);

            if (!g_state.available_drives.empty()) {
                std::vector<std::string> combo_items;
                for (wchar_t d : g_state.available_drives) {
                    combo_items.push_back(std::string("Drive ") + static_cast<char>(d) + ":\\");
                }
                const char* preview = combo_items[g_state.selected_drive_idx].c_str();
                if (ImGui::BeginCombo("##SelectDrive", preview)) {
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

            ImGui::SameLine();
            ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "Select Drive");

            // Capacity Text Placed ABOVE the Progress Bar (Right-Aligned)
            float fraction = 0.0f;
            if (g_state.volume_details.total_clusters > 0) {
                double total_gb = (double)(g_state.volume_details.total_clusters * g_state.volume_details.cluster_size) / (1024.0 * 1024.0 * 1024.0);
                double free_gb = (double)(g_state.volume_details.free_clusters * g_state.volume_details.cluster_size) / (1024.0 * 1024.0 * 1024.0);
                double used_gb = total_gb - free_gb;
                fraction = static_cast<float>(used_gb / total_gb);

                char capacity_str[128];
                snprintf(capacity_str, sizeof(capacity_str), "%.1f GB used / %.1f GB total (%.1f%%)", used_gb, total_gb, fraction * 100.0f);
                float cap_width = ImGui::CalcTextSize(capacity_str).x;
                ImGui::SameLine(total_width - cap_width);
                ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "%s", capacity_str);
            }

            ImGui::Spacing();

            // Sleek Pill Progress Bar (height 8px, gradient)
            RenderSleekProgressBar(fraction);

            ImGui::Spacing();
            ImGui::Spacing();

            // 2-Column Grid of Telemetry Cards
            ImGui::Columns(2, "TelemCardCols", false);

            // Card 1: Hardware Controller
            ImGui::BeginChild("HardwareCard", ImVec2(-1.0f, 130.0f), true);
            RenderCardHeader("HARDWARE CONTROLLER", "SETTINGS");
            ImGui::Spacing();
            RenderKeyValue("Bus:", clustershift::core::DiskTelemetry::bus_type_to_string_a(g_state.telemetry.bus_type));
            if (!g_state.telemetry.product_id.empty()) {
                RenderKeyValueW("Model:", g_state.telemetry.product_id.c_str());
            }
            if (!g_state.telemetry.serial_number.empty()) {
                RenderKeyValueW("S/N:", g_state.telemetry.serial_number.c_str(), true); // Monospace for serial
            }
            ImGui::EndChild();

            // Card 2: S.M.A.R.T. & Thermals
            ImGui::NextColumn();
            ImGui::BeginChild("SmartCard", ImVec2(-1.0f, 130.0f), true);
            RenderCardHeader("S.M.A.R.T. & THERMALS", "SENSOR");
            ImGui::Spacing();
            if (g_state.telemetry.is_failure_predicted) {
                ImGui::TextColored(clustershift::gui::Palette::ErrorRose, "● CRITICAL FAULT PREDICTED");
            } else {
                ImGui::TextColored(clustershift::gui::Palette::SuccessEmerald, "● HEALTHY - No Faults Flagged");
            }
            ImGui::Spacing();
            if (g_state.telemetry.temperature_celsius > 0) {
                ImGui::TextColored(clustershift::gui::Palette::TextMuted, "Controller Temp:");
                ImGui::SameLine(160.0f);
                ImGui::TextColored(clustershift::gui::Palette::TextPrimary, "%d °C (Normal)", g_state.telemetry.temperature_celsius);
            } else {
                RenderKeyValue("Controller Temp:", "N/A");
            }
            ImGui::EndChild();

            ImGui::Columns(1);
            ImGui::Spacing();

            // Card 3: DirectStorage / BypassIO Diagnostic
            ImGui::BeginChild("DirectStorageCard", ImVec2(-1.0f, 110.0f), true);
            RenderCardHeader("DIRECTSTORAGE / BYPASSIO DIAGNOSTIC", "INFO");
            ImGui::Spacing();
            if (g_state.telemetry.bypass_io.is_supported) {
                ImGui::TextColored(clustershift::gui::Palette::AccentLight, "● Active & Optimal - Direct GPU Asset Streaming Ready");
            } else if (g_state.telemetry.bypass_io.is_filter_blocked) {
                ImGui::TextColored(clustershift::gui::Palette::WarningAmber, "● Blocked by Filter Driver");
                if (!g_state.telemetry.bypass_io.failing_driver.empty()) {
                    RenderKeyValueW("Filter Driver:", g_state.telemetry.bypass_io.failing_driver.c_str());
                }
                if (!g_state.telemetry.bypass_io.failure_reason.empty()) {
                    RenderKeyValueW("Detail:", g_state.telemetry.bypass_io.failure_reason.c_str());
                }
            } else if (!g_state.telemetry.bypass_io.failure_reason.empty()) {
                RenderKeyValueW("Status:", g_state.telemetry.bypass_io.failure_reason.c_str());
            } else {
                RenderKeyValue("Status:", "Legacy volume stack (Not active on this volume)");
            }
            ImGui::EndChild();
        }

        // ==========================================
        // TAB 1: BENCHMARK
        // ==========================================
        else if (g_state.active_tab == 1) {
            RenderSectionHeader("UNBUFFERED HARDWARE STORAGE BENCHMARK");
            ImGui::TextColored(clustershift::gui::Palette::TextMuted, "Direct I/O (FILE_FLAG_NO_BUFFERING) | Parallel Multi-Threaded IOCP | Sub-Microsecond Latency");
            ImGui::Spacing();

            // Configuration Controls in Compact 2-Column Cards
            ImGui::Columns(2, "BenchCfgCols", false);

            ImGui::BeginChild("BenchParamsCard", ImVec2(-1.0f, 130.0f), true);
            RenderCardHeader("BENCHMARK CONFIGURATION");
            ImGui::Spacing();

            ImGui::TextColored(clustershift::gui::Palette::TextMuted, "Sample Size:");
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
            ImGui::SetNextItemWidth(180.0f);
            ImGui::SliderInt("Worker Threads", &g_state.bench_threads, 1, 32);
            ImGui::EndChild();

            ImGui::NextColumn();
            ImGui::BeginChild("BenchActionCard", ImVec2(-1.0f, 130.0f), true);
            RenderCardHeader("EXECUTION");
            ImGui::Spacing();

            if (!g_state.bench_running) {
                ImGui::PushStyleColor(ImGuiCol_Button, clustershift::gui::Palette::AccentDim);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, clustershift::gui::Palette::AccentActive);
                ImGui::PushStyleColor(ImGuiCol_Text, clustershift::gui::Palette::AccentLight);

                if (ImGui::Button("  START UNBUFFERED BENCHMARK  ", ImVec2(-1.0f, 44.0f))) {
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
                ImGui::PopStyleColor(3);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, clustershift::gui::Palette::SidebarBg);
                ImGui::Button("  BENCHMARK IN PROGRESS...  ", ImVec2(-1.0f, 44.0f));
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();
            ImGui::TextColored(clustershift::gui::Palette::TextMuted, "%s", g_state.bench_status_text.c_str());
            ImGui::EndChild();

            ImGui::Columns(1);
            ImGui::Spacing();

            // Metric Cards Grid (2x2 Balanced Layout)
            if (g_state.bench_has_results) {
                ImGui::Columns(2, "BenchMetricsGrid", false);

                // Card 1: Sequential Write
                ImGui::BeginChild("SeqWriteCard", ImVec2(-1.0f, 100.0f), true);
                RenderCardHeader("SEQUENTIAL WRITE (1 MB)");
                ImGui::Spacing();
                if (g_font_title) ImGui::PushFont(g_font_title);
                ImGui::TextColored(clustershift::gui::Palette::SuccessEmerald, "%.1f MB/s", g_state.bench_results.seq_write_mbps);
                if (g_font_title) ImGui::PopFont();
                ImGui::EndChild();

                // Card 2: Sequential Read
                ImGui::NextColumn();
                ImGui::BeginChild("SeqReadCard", ImVec2(-1.0f, 100.0f), true);
                RenderCardHeader("SEQUENTIAL READ (1 MB)");
                ImGui::Spacing();
                if (g_font_title) ImGui::PushFont(g_font_title);
                ImGui::TextColored(clustershift::gui::Palette::SuccessEmerald, "%.1f MB/s", g_state.bench_results.seq_read_mbps);
                if (g_font_title) ImGui::PopFont();
                ImGui::EndChild();

                ImGui::NextColumn();

                // Card 3: Random 4K Q32Tx Peak IOPS
                ImGui::BeginChild("Rnd4KQ32Card", ImVec2(-1.0f, 110.0f), true);
                char q32_title[64];
                snprintf(q32_title, sizeof(q32_title), "RANDOM 4KB READ (Q32T%u)", g_state.bench_results.thread_count);
                RenderCardHeader(q32_title);
                ImGui::Spacing();
                if (g_font_title) ImGui::PushFont(g_font_title);
                ImGui::TextColored(clustershift::gui::Palette::AccentLight, "%.0f IOPS", g_state.bench_results.rnd_4k_read_iops);
                if (g_font_title) ImGui::PopFont();
                ImGui::TextColored(clustershift::gui::Palette::TextMuted, "%.1f MB/s (Hardware Queue Saturation)", g_state.bench_results.rnd_4k_read_mbps);
                ImGui::EndChild();

                // Card 4: Random 4K Q1T1 Latency
                ImGui::NextColumn();
                ImGui::BeginChild("Rnd4KQ1T1Card", ImVec2(-1.0f, 110.0f), true);
                RenderCardHeader("RANDOM 4KB LATENCY (Q1T1)");
                ImGui::Spacing();
                if (g_font_title) ImGui::PushFont(g_font_title);
                ImGui::TextColored(clustershift::gui::Palette::WarningAmber, "%.1f us avg", g_state.bench_results.rnd_4k_q1t1_avg_lat_us);
                if (g_font_title) ImGui::PopFont();
                ImGui::TextColored(clustershift::gui::Palette::TextMuted, "Min: %.1f us  |  Max: %.1f us", 
                    g_state.bench_results.rnd_4k_q1t1_min_lat_us, g_state.bench_results.rnd_4k_q1t1_max_lat_us);
                ImGui::EndChild();

                ImGui::Columns(1);
            }
        }

        // ==========================================
        // TAB 2: SPACE HOGS
        // ==========================================
        else if (g_state.active_tab == 2) {
            RenderSectionHeader("SPACE-HOG DISK ANALYZER");
            ImGui::TextColored(clustershift::gui::Palette::TextMuted, "Hardware-Aware Multi-Core Crawl (SSD) / Sequential Anti-Thrash (HDD)");
            ImGui::Spacing();

            ImGui::SetNextItemWidth(360.0f);
            ImGui::InputText("##TargetDirectory", g_state.hogs_path_input, sizeof(g_state.hogs_path_input));
            ImGui::SameLine();

            if (!g_state.hogs_running) {
                ImGui::PushStyleColor(ImGuiCol_Button, clustershift::gui::Palette::AccentDim);
                ImGui::PushStyleColor(ImGuiCol_Text, clustershift::gui::Palette::AccentLight);

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
                ImGui::PopStyleColor(2);
            } else {
                ImGui::Button("Scanning...", ImVec2(140.0f, 32.0f));
            }

            ImGui::SameLine();
            ImGui::TextColored(clustershift::gui::Palette::TextMuted, "%s", g_state.hogs_status_text.c_str());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (g_state.hogs_has_results) {
                ImGui::TextColored(clustershift::gui::Palette::TextSecondary, 
                    "Scanned %llu files across %llu folders in %.2fs (Total Footprint: %.2f GB)",
                    g_state.hogs_results.total_files,
                    g_state.hogs_results.total_directories,
                    g_state.hogs_results.scan_duration_seconds,
                    (double)g_state.hogs_results.total_bytes / (1024.0 * 1024.0 * 1024.0));

                ImGui::Spacing();
                RenderCardHeader("TOP LARGEST FILES");

                if (ImGui::BeginTable("HogsFilesTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(-1.0f, 260.0f))) {
                    ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed, 45.0f);
                    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("File Path", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    int rank = 1;
                    for (const auto& item : g_state.hogs_results.top_files) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "%d", rank++);

                        ImGui::TableSetColumnIndex(1);
                        double sz_mb = (double)item.size_bytes / (1024.0 * 1024.0);
                        if (sz_mb >= 1024.0) {
                            ImGui::TextColored(clustershift::gui::Palette::WarningAmber, "%.2f GB", sz_mb / 1024.0);
                        } else {
                            ImGui::TextColored(clustershift::gui::Palette::TextPrimary, "%.1f MB", sz_mb);
                        }

                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "%ls", item.path.c_str());
                    }
                    ImGui::EndTable();
                }
            }
        }

        // ==========================================
        // TAB 3: CLUSTER MAP
        // ==========================================
        else if (g_state.active_tab == 3) {
            RenderSectionHeader("NTFS REAL-TIME CLUSTER ALLOCATION GRID");
            ImGui::TextColored(clustershift::gui::Palette::TextMuted, "Hardware Volume Cluster Representation | $MFT Protected Zone Lockout");
            ImGui::Spacing();

            if (!g_state.map_loading) {
                ImGui::PushStyleColor(ImGuiCol_Button, clustershift::gui::Palette::AccentDim);
                ImGui::PushStyleColor(ImGuiCol_Text, clustershift::gui::Palette::AccentLight);

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
                ImGui::PopStyleColor(2);
            } else {
                ImGui::Button("Reading Bitmap...", ImVec2(200.0f, 36.0f));
            }

            ImGui::SameLine();
            ImGui::TextColored(clustershift::gui::Palette::TextMuted, "%s", g_state.map_status_text.c_str());

            ImGui::Spacing();

            // Sleek Legend
            ImGui::TextColored(clustershift::gui::Palette::TextMuted, "Legend:");
            ImGui::SameLine();
            ImGui::TextColored(clustershift::gui::Palette::AccentLight, "[■] Allocated Space");
            ImGui::SameLine();
            ImGui::TextColored(clustershift::gui::Palette::SuccessEmerald, "[■] Free Space");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.4f, 0.9f, 1.0f), "[■] $MFT Reserved Zone");

            ImGui::Spacing();

            // 2D Grid Canvas
            ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
            ImVec2 canvas_sz = ImVec2(ImGui::GetContentRegionAvail().x, 260.0f);
            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            draw_list->AddRectFilled(canvas_p0, ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y), IM_COL32(17, 20, 29, 255), 6.0f);
            draw_list->AddRect(canvas_p0, ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y), IM_COL32(255, 255, 255, 18), 6.0f, 0, 1.0f);

            if (g_state.map_loaded && g_state.cluster_map_data) {
                const int cols = 64;
                const int rows = 16;
                const float cell_w = (canvas_sz.x - 4.0f) / cols;
                const float cell_h = (canvas_sz.y - 4.0f) / rows;
                const uint64_t total_c = g_state.cluster_map_data->total_clusters();
                const uint64_t clusters_per_cell = (std::max)(1ULL, total_c / (cols * rows));

                for (int r = 0; r < rows; ++r) {
                    for (int c = 0; c < cols; ++c) {
                        uint64_t lcn = (r * cols + c) * clusters_per_cell;
                        ImU32 col = IM_COL32(14, 165, 233, 200); // Allocated (Sky blue)

                        if (g_state.cluster_map_data->is_cluster_free(lcn)) {
                            col = IM_COL32(16, 185, 129, 200); // Free (Emerald)
                        }

                        ImVec2 p_min(canvas_p0.x + 2.0f + c * cell_w, canvas_p0.y + 2.0f + r * cell_h);
                        ImVec2 p_max(p_min.x + cell_w - 1.0f, p_min.y + cell_h - 1.0f);
                        draw_list->AddRectFilled(p_min, p_max, col, 2.0f);
                    }
                }
            } else {
                draw_list->AddText(ImVec2(canvas_p0.x + 20.0f, canvas_p0.y + 110.0f), IM_COL32(156, 163, 175, 255), "Cluster bitmap not loaded. Click 'LOAD CLUSTER BITMAP' above.");
            }

            ImGui::Dummy(canvas_sz);
        }

        // ==========================================
        // TAB 4: MAINTENANCE
        // ==========================================
        else if (g_state.active_tab == 4) {
            RenderSectionHeader("SYSTEM MAINTENANCE & CONTEXT INTEGRATION");
            ImGui::Spacing();

            // Card 1: SSD TRIM Engine
            ImGui::BeginChild("TrimSection", ImVec2(-1.0f, 130.0f), true);
            RenderCardHeader("FLASH STORAGE TRIM ENGINE", "FLASH");
            ImGui::Spacing();
            ImGui::TextColored(clustershift::gui::Palette::TextMuted, 
                "Issues instant kernel DeviceDsmAction_Trim to preserve flash endurance & restore write IOPS.");
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, clustershift::gui::Palette::AccentDim);
            ImGui::PushStyleColor(ImGuiCol_Text, clustershift::gui::Palette::AccentLight);
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
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "%s", g_state.trim_result_msg.c_str());
            ImGui::EndChild();

            ImGui::Spacing();

            // Card 2: Windows Explorer Context Menu
            ImGui::BeginChild("ShellSection", ImVec2(-1.0f, 130.0f), true);
            RenderCardHeader("WINDOWS EXPLORER CONTEXT MENU", "INTEGRATION");
            ImGui::Spacing();
            ImGui::TextColored(clustershift::gui::Palette::TextMuted, 
                "Integrates 1-Click ClusterShift actions directly into right-click folder context menu.");
            ImGui::Spacing();

            if (ImGui::Button("  INSTALL EXPLORER MENU  ", ImVec2(220.0f, 36.0f))) {
                auto ok = clustershift::core::ShellIntegration::install_context_menu();
                g_state.context_menu_installed = ok.has_value();
            }
            ImGui::SameLine();
            if (ImGui::Button("  REMOVE EXPLORER MENU  ", ImVec2(220.0f, 36.0f))) {
                (void)clustershift::core::ShellIntegration::uninstall_context_menu();
                g_state.context_menu_installed = false;
            }
            ImGui::EndChild();
        }

        ImGui::EndChild();
        ImGui::Columns(1);
        ImGui::End();

        // Rendering
        ImGui::Render();
        const float clear_color[4] = { 0.043f, 0.051f, 0.075f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
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
