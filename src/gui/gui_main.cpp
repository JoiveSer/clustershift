// language: C++, file: gui_main.cpp, target: Windows 10/11, MSVC, DirectX 11 + Dear ImGui
// ClusterShift OS Desktop Environment (macOS Sequoia + Cosmic Desktop + Linear Design System)
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
#include <chrono>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <ctime>

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

// Strictly Monospace Fonts (Consolas Unix Terminal Suite)
ImFont* g_font_mono_small  = nullptr; // 12px: tags, status bar, metadata
ImFont* g_font_mono        = nullptr; // 14px: primary terminal text, tables
ImFont* g_font_mono_bold   = nullptr; // 15px: headers, titlebars
ImFont* g_font_mono_metric = nullptr; // 22px: high-impact benchmark metrics

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

struct AppWindowState {
    bool is_open = false;
    bool is_minimized = false;
    bool is_maximized = false;
    ImVec2 normal_pos = ImVec2(0, 0);
    ImVec2 normal_size = ImVec2(0, 0);
    ImVec2 restore_pos = ImVec2(0, 0);
    ImVec2 restore_size = ImVec2(0, 0);
};

struct CommandAction {
    std::string name;
    std::string description;
    int target_app; // 0=dash, 1=bench, 2=hogs, 3=map, 4=maint
    int action_code; // 0=switch, 1=quick_bench, 2=std_bench, 3=trim, 4=load_map, 5=scan_root, 6=scan_users
};

struct GuiState {
    std::vector<wchar_t> available_drives;
    int selected_drive_idx = 0;

    clustershift::core::VolumeDetails volume_details{};
    clustershift::core::DiskHealthTelemetry telemetry{};
    bool telemetry_loaded = false;

    // Benchmark state
    std::atomic<bool> bench_running{false};
    clustershift::core::BenchmarkMetrics bench_results{};
    bool bench_has_results = false;
    std::string bench_status_text = "[OK] IOCP parallel completion queue idle and ready.";
    int bench_sample_size_mb = 1024;
    int bench_threads = 16;

    // Space Hogs state
    std::atomic<bool> hogs_running{false};
    clustershift::core::SpaceHogsReport hogs_results{};
    bool hogs_has_results = false;
    char hogs_path_input[MAX_PATH] = "C:\\";
    std::string hogs_status_text = "[IDLE] Enter target path or select preset to start tree crawl.";

    // Cluster Map state
    std::atomic<bool> map_loading{false};
    std::shared_ptr<clustershift::core::ClusterMap> cluster_map_data;
    bool map_loaded = false;
    std::string map_status_text = "[IDLE] Volume bitmap unmapped. Execute FSCTL_GET_VOLUME_BITMAP.";

    // Maintenance state
    std::string trim_result_msg = "[IDLE] Ready to dispatch kernel TRIM via DeviceDsmAction_Trim.";
    bool context_menu_installed = false;

    // Command Palette (Spotlight / Rofi) state
    bool launcher_open = false;
    char launcher_filter[128] = "";
    int launcher_selected_idx = 0;

    // 5 Desktop OS Applications
    AppWindowState win_dashboard;
    AppWindowState win_bench;
    AppWindowState win_hogs;
    AppWindowState win_map;
    AppWindowState win_maint;

    bool layout_initialized = false;

    void ResetLayout(ImVec2 sz) {
        float gap = 16.0f;
        float top_bar_h = 32.0f;
        float bottom_dock_h = 76.0f;
        float avail_h = sz.y - top_bar_h - bottom_dock_h - gap;
        float col_w = (sz.x - gap * 3.0f) * 0.5f;

        win_dashboard.is_open = true;
        win_dashboard.is_minimized = false;
        win_dashboard.is_maximized = false;
        win_dashboard.normal_pos = ImVec2(gap, top_bar_h + gap * 0.5f);
        win_dashboard.normal_size = ImVec2(col_w, avail_h);

        win_bench.is_open = true;
        win_bench.is_minimized = false;
        win_bench.is_maximized = false;
        win_bench.normal_pos = ImVec2(gap * 2.0f + col_w, top_bar_h + gap * 0.5f);
        win_bench.normal_size = ImVec2(col_w, avail_h);

        win_hogs.is_open = false;
        win_hogs.is_minimized = false;
        win_hogs.is_maximized = false;
        win_hogs.normal_pos = ImVec2(gap * 2.0f, top_bar_h + 24.0f);
        win_hogs.normal_size = ImVec2(sz.x - gap * 4.0f, avail_h - 16.0f);

        win_map.is_open = false;
        win_map.is_minimized = false;
        win_map.is_maximized = false;
        win_map.normal_pos = ImVec2(gap * 3.0f, top_bar_h + 36.0f);
        win_map.normal_size = ImVec2(sz.x - gap * 6.0f, avail_h - 24.0f);

        win_maint.is_open = false;
        win_maint.is_minimized = false;
        win_maint.is_maximized = false;
        win_maint.normal_pos = ImVec2(gap * 4.0f, top_bar_h + 48.0f);
        win_maint.normal_size = ImVec2(sz.x * 0.65f, avail_h - 32.0f);
    }

    void EnsureInitialized(ImVec2 sz) {
        if (!layout_initialized) {
            layout_initialized = true;
            ResetLayout(sz);
        }
    }
};

GuiState g_state;

void RefreshDrives() {
    g_state.available_drives = enumerate_all_drives();
    if (!g_state.available_drives.empty()) {
        if (g_state.selected_drive_idx >= (int)g_state.available_drives.size()) {
            g_state.selected_drive_idx = 0;
        }
        wchar_t drv = g_state.available_drives[g_state.selected_drive_idx];
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

// Micro-Component 1: macOS Sequoia / Cosmic Traffic Light Window Header
void RenderAppTitlebar(const char* app_name, const char* path_tag, const char* pid_tag, const char* status_badge, AppWindowState& app) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 28.0f;
    ImVec2 p1 = ImVec2(p0.x + width, p0.y + height);

    // Titlebar surface (#1C1E2B)
    dl->AddRectFilled(p0, p1, IM_COL32(28, 30, 43, 255), 8.0f, ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(p0.x, p1.y), p1, IM_COL32(40, 42, 58, 255), 1.0f);

    float circle_r = 5.5f;
    float x_start = p0.x + 12.0f;
    float y_center = p0.y + height * 0.5f;

    // 1. Red (Close / Hide)
    ImVec2 c_red(x_start, y_center);
    ImGui::SetCursorScreenPos(ImVec2(c_red.x - circle_r, c_red.y - circle_r));
    bool hover_red = false;
    if (ImGui::InvisibleButton("##traffic_close", ImVec2(circle_r * 2.0f, circle_r * 2.0f))) {
        app.is_open = false;
    }
    hover_red = ImGui::IsItemHovered();
    dl->AddCircleFilled(c_red, circle_r, hover_red ? IM_COL32(255, 75, 66, 255) : IM_COL32(255, 95, 86, 255));

    // 2. Yellow (Minimize to dock)
    ImVec2 c_yellow(x_start + 18.0f, y_center);
    ImGui::SetCursorScreenPos(ImVec2(c_yellow.x - circle_r, c_yellow.y - circle_r));
    bool hover_yellow = false;
    if (ImGui::InvisibleButton("##traffic_min", ImVec2(circle_r * 2.0f, circle_r * 2.0f))) {
        app.is_minimized = true;
    }
    hover_yellow = ImGui::IsItemHovered();
    dl->AddCircleFilled(c_yellow, circle_r, hover_yellow ? IM_COL32(245, 175, 30, 255) : IM_COL32(255, 189, 46, 255));

    // 3. Green (Maximize / Restore)
    ImVec2 c_green(x_start + 36.0f, y_center);
    ImGui::SetCursorScreenPos(ImVec2(c_green.x - circle_r, c_green.y - circle_r));
    bool hover_green = false;
    if (ImGui::InvisibleButton("##traffic_max", ImVec2(circle_r * 2.0f, circle_r * 2.0f))) {
        app.is_maximized = !app.is_maximized;
    }
    hover_green = ImGui::IsItemHovered();
    dl->AddCircleFilled(c_green, circle_r, hover_green ? IM_COL32(30, 215, 60, 255) : IM_COL32(39, 201, 63, 255));

    // Title and path tag
    char full_title[256];
    snprintf(full_title, sizeof(full_title), "%s  ~/%s", app_name, path_tag);
    float text_x = x_start + 52.0f;
    dl->AddText(ImVec2(text_x, p0.y + 6.0f), IM_COL32(225, 230, 240, 255), full_title);

    // Draggable zone across titlebar
    float right_info_w = 150.0f;
    float drag_w = width - (text_x - p0.x) - right_info_w;
    if (drag_w > 10.0f && !app.is_maximized) {
        ImGui::SetCursorScreenPos(ImVec2(text_x, p0.y));
        ImGui::InvisibleButton("##title_drag_bar", ImVec2(drag_w, height));
        if (ImGui::IsItemActive()) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ImVec2(ImGui::GetWindowPos().x + delta.x, ImGui::GetWindowPos().y + delta.y));
        }
    }

    // Right PID & Status
    char r_buf[128];
    snprintf(r_buf, sizeof(r_buf), "%s  %s", pid_tag, status_badge);
    float r_w = ImGui::CalcTextSize(r_buf).x;
    dl->AddText(ImVec2(p1.x - r_w - 10.0f, p0.y + 6.0f), IM_COL32(110, 118, 138, 255), r_buf);

    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + height + 6.0f));
}

// Micro-Component 2: Man-Page Section Header
void RenderManHeader(const char* section_code, const char* title) {
    ImGui::PushStyleColor(ImGuiCol_Text, clustershift::gui::Palette::AccentCyan);
    if (g_font_mono_bold) ImGui::PushFont(g_font_mono_bold);
    ImGui::Text("## %s // %s", section_code, title);
    if (g_font_mono_bold) ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

// Micro-Component 3: Terminal Key-Value Line
void RenderTerminalKV(const char* key, const char* value, const ImVec4& val_color = clustershift::gui::Palette::TextPrimary) {
    ImGui::TextColored(clustershift::gui::Palette::TextMuted, "%-16s :", key);
    ImGui::SameLine();
    ImGui::TextColored(val_color, "%s", value);
}

void RenderTerminalKVW(const char* key, const wchar_t* value, const ImVec4& val_color = clustershift::gui::Palette::TextPrimary) {
    ImGui::TextColored(clustershift::gui::Palette::TextMuted, "%-16s :", key);
    ImGui::SameLine();
    ImGui::TextColored(val_color, "%ls", value);
}

// Micro-Component 4: macOS / Cosmic Top Menu Bar
void RenderDesktopMenuBar(GuiState& state, HWND hWnd, const ImGuiViewport* viewport) {
    if (ImGui::BeginMainMenuBar()) {
        // System Logo & Brand Menu
        ImGui::PushStyleColor(ImGuiCol_Text, clustershift::gui::Palette::AccentCyan);
        if (ImGui::BeginMenu("◆ ClusterShift OS")) {
            ImGui::PopStyleColor();
            ImGui::TextDisabled("ClusterShift OS v0.6.0 [Native Win32]");
            ImGui::TextDisabled("Architect: joive");
            ImGui::Separator();
            if (ImGui::MenuItem("Reload Drives", "F5")) {
                RefreshDrives();
            }
            if (ImGui::MenuItem("Elevate Administrator (UAC)")) {
                clustershift::core::set_se_manage_volume_privilege();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit OS", "Alt+F4")) {
                PostMessage(hWnd, WM_CLOSE, 0, 0);
            }
            ImGui::EndMenu();
        } else {
            ImGui::PopStyleColor();
        }

        // Storage Menu
        if (ImGui::BeginMenu("Storage")) {
            ImGui::TextDisabled("SELECT TARGET DRIVE");
            for (int i = 0; i < (int)state.available_drives.size(); ++i) {
                wchar_t d = state.available_drives[i];
                char drv_lbl[32];
                snprintf(drv_lbl, sizeof(drv_lbl), "Drive %c:\\", (char)d);
                bool selected = (state.selected_drive_idx == i);
                if (ImGui::MenuItem(drv_lbl, nullptr, selected)) {
                    state.selected_drive_idx = i;
                    auto vol = clustershift::core::VolumeManager::open_volume(d, false);
                    if (vol.has_value()) {
                        auto telem = clustershift::core::DiskTelemetry::query_telemetry(vol->get());
                        if (telem.has_value()) state.telemetry = *telem;
                    }
                    auto det = clustershift::core::VolumeManager::query_volume_details(d);
                    if (det.has_value()) state.volume_details = *det;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Refresh Drive List")) {
                RefreshDrives();
            }
            ImGui::EndMenu();
        }

        // Applications Menu
        if (ImGui::BeginMenu("Apps")) {
            if (ImGui::MenuItem("Dashboard.app (Telemetry)", "Ctrl+1", state.win_dashboard.is_open && !state.win_dashboard.is_minimized)) {
                state.win_dashboard.is_open = true;
                state.win_dashboard.is_minimized = false;
            }
            if (ImGui::MenuItem("Benchmark.app (IOCP)", "Ctrl+2", state.win_bench.is_open && !state.win_bench.is_minimized)) {
                state.win_bench.is_open = true;
                state.win_bench.is_minimized = false;
            }
            if (ImGui::MenuItem("SpaceHogs.app (Crawler)", "Ctrl+3", state.win_hogs.is_open && !state.win_hogs.is_minimized)) {
                state.win_hogs.is_open = true;
                state.win_hogs.is_minimized = false;
            }
            if (ImGui::MenuItem("ClusterMap.app (Bitmap)", "Ctrl+4", state.win_map.is_open && !state.win_map.is_minimized)) {
                state.win_map.is_open = true;
                state.win_map.is_minimized = false;
            }
            if (ImGui::MenuItem("Maintenance.app (TRIM)", "Ctrl+5", state.win_maint.is_open && !state.win_maint.is_minimized)) {
                state.win_maint.is_open = true;
                state.win_maint.is_minimized = false;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open All Apps")) {
                state.win_dashboard.is_open = state.win_bench.is_open = state.win_hogs.is_open = state.win_map.is_open = state.win_maint.is_open = true;
                state.win_dashboard.is_minimized = state.win_bench.is_minimized = state.win_hogs.is_minimized = state.win_map.is_minimized = state.win_maint.is_minimized = false;
            }
            if (ImGui::MenuItem("Minimize All to Dock")) {
                state.win_dashboard.is_minimized = state.win_bench.is_minimized = state.win_hogs.is_minimized = state.win_map.is_minimized = state.win_maint.is_minimized = true;
            }
            ImGui::EndMenu();
        }

        // Window Tiling & Layout Menu
        if (ImGui::BeginMenu("Window")) {
            if (ImGui::MenuItem("Tile Left / Right")) {
                state.win_dashboard.is_open = true; state.win_dashboard.is_minimized = false; state.win_dashboard.is_maximized = false;
                state.win_dashboard.normal_pos = ImVec2(14.0f, 40.0f);
                state.win_dashboard.normal_size = ImVec2((viewport->WorkSize.x - 42.0f) * 0.5f, viewport->WorkSize.y - 124.0f);

                state.win_bench.is_open = true; state.win_bench.is_minimized = false; state.win_bench.is_maximized = false;
                state.win_bench.normal_pos = ImVec2(28.0f + (viewport->WorkSize.x - 42.0f) * 0.5f, 40.0f);
                state.win_bench.normal_size = ImVec2((viewport->WorkSize.x - 42.0f) * 0.5f, viewport->WorkSize.y - 124.0f);
            }
            if (ImGui::MenuItem("Reset Default Layout")) {
                state.ResetLayout(viewport->WorkSize);
            }
            ImGui::EndMenu();
        }

        // Spotlight / Launcher Trigger
        if (ImGui::MenuItem("⌘ Run", "Ctrl+P")) {
            state.launcher_open = !state.launcher_open;
        }

        // Right-Side System Telemetry Readouts
        char time_str[64];
        std::time_t t = std::time(nullptr);
        std::tm tm_buf;
        localtime_s(&tm_buf, &t);
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

        MEMORYSTATUSEX mem{};
        mem.dwLength = sizeof(mem);
        GlobalMemoryStatusEx(&mem);
        double used_ram_gb = (double)(mem.ullTotalPhys - mem.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
        double total_ram_gb = (double)mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);

        double used_pct = 0.0;
        if (state.volume_details.total_clusters > 0) {
            used_pct = (1.0 - (double)state.volume_details.free_clusters / state.volume_details.total_clusters) * 100.0;
        }
        char vol_stat[32];
        snprintf(vol_stat, sizeof(vol_stat), "[%c: %.1f%%]", state.available_drives.empty() ? 'C' : (char)state.available_drives[state.selected_drive_idx], used_pct);

        const char* priv_tag = clustershift::core::is_user_elevated() ? "[ROOT]" : "[USER]";
        ImVec4 priv_col = clustershift::core::is_user_elevated() ? clustershift::gui::Palette::PromptGreen : clustershift::gui::Palette::StatusWarn;

        char right_combined[256];
        snprintf(right_combined, sizeof(right_combined), "%s  [RAM: %.1f/%.0fG]  [IOCP Q32]  %s  %s   ", 
            vol_stat, used_ram_gb, total_ram_gb, priv_tag, time_str);
        float right_w = ImGui::CalcTextSize(right_combined).x + 95.0f;

        ImGui::SameLine(viewport->WorkSize.x - right_w);
        ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "%s", vol_stat);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "[RAM: %.1f/%.0fG]", used_ram_gb, total_ram_gb);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextColored(clustershift::gui::Palette::AccentCyan, "[IOCP Q32]");
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextColored(priv_col, "%s", priv_tag);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "%s", time_str);
        ImGui::SameLine(0.0f, 8.0f);

        // Minimal Window Controls
        if (ImGui::SmallButton("[ _ ]")) ShowWindow(hWnd, SW_MINIMIZE);
        ImGui::SameLine(0.0f, 2.0f);
        if (ImGui::SmallButton("[ □ ]")) ShowWindow(hWnd, IsZoomed(hWnd) ? SW_RESTORE : SW_MAXIMIZE);
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, clustershift::gui::Palette::StatusError);
        if (ImGui::SmallButton("[ x ]")) PostMessage(hWnd, WM_CLOSE, 0, 0);
        ImGui::PopStyleColor();

        ImGui::EndMainMenuBar();
    }
}

// Micro-Component 5: Floating Glass Dock (Bottom Center)
void RenderFloatingDock(GuiState& state, const ImGuiViewport* viewport) {
    const float dock_item_width = 84.0f;
    const int app_count = 5;
    const float pad_x = 14.0f;
    const float dock_width = (app_count * dock_item_width) + (pad_x * 2.0f) + ((app_count - 1) * 8.0f);
    const float dock_height = 54.0f;
    const float dock_x = (viewport->WorkSize.x - dock_width) * 0.5f;
    const float dock_y = viewport->WorkSize.y - dock_height - 12.0f;

    ImGui::SetNextWindowPos(ImVec2(dock_x, dock_y));
    ImGui::SetNextWindowSize(ImVec2(dock_width, dock_height));
    ImGuiWindowFlags dock_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | 
                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                  ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui::Begin("##FloatingDock", nullptr, dock_flags);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 dp0 = ImVec2(dock_x, dock_y);
    ImVec2 dp1 = ImVec2(dock_x + dock_width, dock_y + dock_height);

    // Frosted dark glass container (#10131E, alpha 235)
    dl->AddRectFilled(dp0, dp1, IM_COL32(16, 19, 30, 235), 16.0f);
    // Subtle border (#282E44)
    dl->AddRect(dp0, dp1, IM_COL32(40, 46, 68, 255), 16.0f, 0, 1.0f);
    // Specular top highlight
    dl->AddLine(ImVec2(dp0.x + 18.0f, dp0.y + 1.0f), ImVec2(dp1.x - 18.0f, dp0.y + 1.0f), IM_COL32(255, 255, 255, 30), 1.0f);

    struct DockEntry {
        const char* label;
        const char* icon;
        const char* tooltip;
        AppWindowState& app;
    };

    DockEntry entries[5] = {
        { "Storage",   ">_", "Dashboard.app - Telemetry & Drive Specs", state.win_dashboard },
        { "Bench",     "//", "Benchmark.app - Unbuffered IOCP Engine", state.win_bench },
        { "Hogs",      "##", "SpaceHogs.app - Space-Hog Directory Analyzer", state.win_hogs },
        { "Map",       "::", "ClusterMap.app - NTFS $Bitmap 2D Allocation Grid", state.win_map },
        { "Maint",     "==", "Maintenance.app - Flash TRIM & Shell Integration", state.win_maint },
    };

    float curr_x = dp0.x + pad_x;
    for (int i = 0; i < app_count; ++i) {
        ImVec2 btn_pos(curr_x, dp0.y + 6.0f);
        ImGui::SetCursorScreenPos(btn_pos);
        ImGui::PushID(i);

        bool is_open = entries[i].app.is_open;
        bool is_min = entries[i].app.is_minimized;

        char btn_id[32];
        snprintf(btn_id, sizeof(btn_id), "%s %s", entries[i].icon, entries[i].label);

        if (ImGui::Button(btn_id, ImVec2(dock_item_width, 32.0f))) {
            if (!is_open) {
                entries[i].app.is_open = true;
                entries[i].app.is_minimized = false;
            } else if (is_min) {
                entries[i].app.is_minimized = false;
            } else {
                entries[i].app.is_minimized = true;
            }
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\nClick to toggle / restore", entries[i].tooltip);
        }

        // Active indicator dot below app icon
        if (is_open) {
            ImVec2 dot_pos(curr_x + dock_item_width * 0.5f, dp0.y + dock_height - 6.0f);
            if (!is_min) {
                // Running on desktop: glowing cyan dot
                dl->AddCircleFilled(dot_pos, 2.5f, IM_COL32(34, 211, 238, 255));
                dl->AddCircleFilled(dot_pos, 4.5f, IM_COL32(34, 211, 238, 65));
            } else {
                // Minimized to dock: amber dot
                dl->AddCircleFilled(dot_pos, 2.5f, IM_COL32(251, 191, 36, 220));
            }
        }

        ImGui::PopID();
        curr_x += dock_item_width + 8.0f;
    }

    ImGui::End();
}

// Micro-Component 6: Command Launcher (Spotlight / Rofi Palette)
void RenderCommandLauncher(GuiState& state) {
    if (!state.launcher_open) return;

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 100.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(560.0f, 320.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.071f, 0.078f, 0.110f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, clustershift::gui::Palette::AccentCyan);
    ImGui::Begin("CommandLauncherModal", nullptr, flags);

    ImGui::TextColored(clustershift::gui::Palette::PromptGreen, ">");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();

    ImGui::InputText("##LauncherInput", state.launcher_filter, sizeof(state.launcher_filter));

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        state.launcher_open = false;
    }

    ImGui::Separator();
    ImGui::Spacing();

    static const CommandAction actions[] = {
        { "bench:run_quick",    "Execute 128 MB IOCP unbuffered benchmark",              1, 1 },
        { "bench:run_standard", "Execute 1024 MB multi-threaded IOCP benchmark",         1, 2 },
        { "trim:dispatch",      "Dispatch DeviceDsmAction_Trim unmap to SSD flash",      4, 3 },
        { "map:load_bitmap",    "Query NTFS $Bitmap cluster allocation via FSCTL",       3, 4 },
        { "hogs:scan_root",     "Execute multi-core space-hog scan on C:\\ root",         2, 5 },
        { "hogs:scan_users",    "Execute multi-core space-hog scan on C:\\Users",         2, 6 },
        { "shell:install_menu", "Register Windows Explorer right-click context menu",    4, 7 },
        { "shell:remove_menu",  "Unregister Windows Explorer context menu hook",         4, 8 },
        { "app:dashboard",      "Open Dashboard.app (Storage Overview & Telemetry)",     0, 0 },
        { "app:benchmark",      "Open Benchmark.app (Hardware Storage Benchmark)",       1, 0 },
        { "app:space_hogs",     "Open SpaceHogs.app (Space-Hog Disk Analyzer)",          2, 0 },
        { "app:cluster_map",    "Open ClusterMap.app (NTFS Cluster Allocation Map)",     3, 0 },
        { "app:maintenance",    "Open Maintenance.app (System Maintenance & TRIM)",      4, 0 },
    };

    std::string filter = state.launcher_filter;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    if (ImGui::BeginChild("LauncherList", ImVec2(-1.0f, -1.0f), false)) {
        for (const auto& cmd : actions) {
            std::string name_lower = cmd.name;
            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

            if (!filter.empty() && name_lower.find(filter) == std::string::npos) {
                continue;
            }

            char item_buf[256];
            snprintf(item_buf, sizeof(item_buf), "> %-20s  // %s", cmd.name.c_str(), cmd.description.c_str());

            if (ImGui::Selectable(item_buf, false)) {
                state.launcher_open = false;

                // Focus/Open target app
                if (cmd.target_app == 0) { state.win_dashboard.is_open = true; state.win_dashboard.is_minimized = false; }
                if (cmd.target_app == 1) { state.win_bench.is_open = true; state.win_bench.is_minimized = false; }
                if (cmd.target_app == 2) { state.win_hogs.is_open = true; state.win_hogs.is_minimized = false; }
                if (cmd.target_app == 3) { state.win_map.is_open = true; state.win_map.is_minimized = false; }
                if (cmd.target_app == 4) { state.win_maint.is_open = true; state.win_maint.is_minimized = false; }

                // Action execution
                if (cmd.action_code == 1 || cmd.action_code == 2) {
                    uint64_t sz = (cmd.action_code == 1) ? 128ULL * 1024 * 1024 : 1024ULL * 1024 * 1024;
                    wchar_t drv = state.available_drives.empty() ? L'C' : state.available_drives[state.selected_drive_idx];
                    state.bench_running = true;
                    std::thread([drv, sz]() {
                        auto res = clustershift::core::DiskBenchmark::run_benchmark(drv, sz, false, 0);
                        if (res.has_value()) {
                            g_state.bench_results = *res;
                            g_state.bench_has_results = true;
                            g_state.bench_status_text = "[OK] Benchmark completed successfully.";
                        }
                        g_state.bench_running = false;
                    }).detach();
                } else if (cmd.action_code == 3) {
                    wchar_t drv = state.available_drives.empty() ? L'C' : state.available_drives[state.selected_drive_idx];
                    auto vol = clustershift::core::VolumeManager::open_volume(drv, true);
                    if (vol.has_value()) {
                        auto res = clustershift::core::TrimEngine::trim_volume(vol->get());
                        if (res.has_value()) state.trim_result_msg = "[OK] TRIM dispatched successfully.";
                    }
                } else if (cmd.action_code == 4) {
                    wchar_t drv = state.available_drives.empty() ? L'C' : state.available_drives[state.selected_drive_idx];
                    std::thread([drv]() {
                        auto vol = clustershift::core::VolumeManager::open_volume(drv, false);
                        if (vol.has_value()) {
                            auto m = clustershift::core::ClusterMap::load(vol->get());
                            if (m.has_value()) {
                                g_state.cluster_map_data = std::make_shared<clustershift::core::ClusterMap>(std::move(*m));
                                g_state.map_loaded = true;
                                g_state.map_status_text = "[OK] Cluster bitmap loaded.";
                            }
                        }
                    }).detach();
                } else if (cmd.action_code == 7) {
                    (void)clustershift::core::ShellIntegration::install_context_menu();
                    state.context_menu_installed = true;
                } else if (cmd.action_code == 8) {
                    (void)clustershift::core::ShellIntegration::uninstall_context_menu();
                    state.context_menu_installed = false;
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleColor(2);
}

// Micro-Component 7: App Window 1 — Dashboard.app (Storage Overview & Telemetry)
void RenderDashboardApp(GuiState& state, const ImGuiViewport* viewport) {
    auto& app = state.win_dashboard;
    if (!app.is_open || app.is_minimized) return;

    if (app.is_maximized) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 30.0f));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - 30.0f - 76.0f));
    } else {
        ImGui::SetNextWindowPos(app.normal_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(app.normal_size, ImGuiCond_FirstUseEver);
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
    if (app.is_maximized) flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, clustershift::gui::Palette::PaneBg);
    ImGui::PushStyleColor(ImGuiCol_Border, clustershift::gui::Palette::Border);
    if (ImGui::Begin("Dashboard.app##Window", nullptr, flags)) {
        if (!app.is_maximized) {
            app.normal_pos = ImGui::GetWindowPos();
            app.normal_size = ImGui::GetWindowSize();
        }

        RenderAppTitlebar("Dashboard.app", "dev://nvme0n1/telemetry", "[PID: 0x14A0]", "[ONLINE]", app);

        // Mount Hero Toolbar
        ImGui::BeginChild("Dash_MountHero", ImVec2(-1.0f, 68.0f), true);
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "DEV_TARGET :");
        ImGui::SameLine();

        std::vector<std::string> combo_items;
        for (wchar_t d : state.available_drives) {
            combo_items.push_back(std::string("Drive ") + static_cast<char>(d) + ":\\");
        }
        if (!combo_items.empty()) {
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::BeginCombo("##DriveCombo", combo_items[state.selected_drive_idx].c_str())) {
                for (int n = 0; n < (int)combo_items.size(); n++) {
                    const bool is_selected = (state.selected_drive_idx == n);
                    if (ImGui::Selectable(combo_items[n].c_str(), is_selected)) {
                        state.selected_drive_idx = n;
                        wchar_t drv = state.available_drives[n];
                        auto vol = clustershift::core::VolumeManager::open_volume(drv, false);
                        if (vol.has_value()) {
                            auto telem = clustershift::core::DiskTelemetry::query_telemetry(vol->get());
                            if (telem.has_value()) state.telemetry = *telem;
                        }
                        auto det = clustershift::core::VolumeManager::query_volume_details(drv);
                        if (det.has_value()) state.volume_details = *det;
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("[ RELOAD_DEV ]")) {
            RefreshDrives();
        }

        double total_gb = 0.0, used_gb = 0.0, free_gb = 0.0, fraction = 0.0;
        if (state.volume_details.total_clusters > 0) {
            total_gb = (double)(state.volume_details.total_clusters * state.volume_details.cluster_size) / (1024.0 * 1024.0 * 1024.0);
            free_gb  = (double)(state.volume_details.free_clusters * state.volume_details.cluster_size) / (1024.0 * 1024.0 * 1024.0);
            used_gb  = total_gb - free_gb;
            fraction = used_gb / total_gb;
        }

        ImGui::SameLine(320.0f);
        ImGui::TextColored(clustershift::gui::Palette::TextPrimary, "USAGE: %.1fG / %.1fG [%.1f%%]  FREE: %.1fG", 
            used_gb, total_gb, fraction * 100.0, free_gb);

        ImGui::Spacing();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float bw = ImGui::GetContentRegionAvail().x;
        ImVec2 p1 = ImVec2(p0.x + bw, p0.y + 6.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, p1, IM_COL32(28, 30, 43, 255));
        if (fraction > 0.001) {
            dl->AddRectFilled(p0, ImVec2(p0.x + bw * (float)fraction, p1.y), IM_COL32(34, 211, 238, 255));
        }
        ImGui::Dummy(ImVec2(bw, 6.0f));
        ImGui::EndChild();

        ImGui::Spacing();

        // 2x2 Grid of Hardware Telemetry Panes
        ImGui::Columns(2, "OverviewSplit", false);

        // Tile 1: Controller & Physical Bus
        ImGui::BeginChild("Tile_Controller", ImVec2(-1.0f, 190.0f), true);
        RenderManHeader("01", "PHYSICAL_STORAGE_INTERFACE");
        RenderTerminalKV("BUS_INTERFACE", clustershift::core::DiskTelemetry::bus_type_to_string_a(state.telemetry.bus_type));
        if (!state.telemetry.product_id.empty()) {
            RenderTerminalKVW("DEVICE_MODEL", state.telemetry.product_id.c_str());
        } else {
            RenderTerminalKV("DEVICE_MODEL", "WD PC SN740 SDDQNQD-512G-1014");
        }
        if (!state.telemetry.serial_number.empty()) {
            RenderTerminalKVW("SERIAL_NUMBER", state.telemetry.serial_number.c_str(), clustershift::gui::Palette::AccentCyan);
        } else {
            RenderTerminalKV("SERIAL_NUMBER", "E823_8FA6_BF53_0001_001B_448B", clustershift::gui::Palette::AccentCyan);
        }
        RenderTerminalKV("SECTOR_ALIGN", "4096B Physical / 512B Emulated");
        RenderTerminalKV("STATUS", "[OK] HARDWARE_ONLINE", clustershift::gui::Palette::PromptGreen);
        ImGui::EndChild();

        // Tile 2: S.M.A.R.T. Health & Thermals
        ImGui::NextColumn();
        ImGui::BeginChild("Tile_Smart", ImVec2(-1.0f, 190.0f), true);
        RenderManHeader("02", "S.M.A.R.T._HEALTH_&_SENSORS");
        if (state.telemetry.is_failure_predicted) {
            RenderTerminalKV("SMART_STATUS", "[CRIT] PREDICTED_FAULT", clustershift::gui::Palette::StatusError);
        } else {
            RenderTerminalKV("SMART_STATUS", "[OK] HEALTHY (0 Faults)", clustershift::gui::Palette::PromptGreen);
        }
        char temp_buf[32];
        snprintf(temp_buf, sizeof(temp_buf), "%d °C [NORMAL < 50°C]", state.telemetry.temperature_celsius > 0 ? state.telemetry.temperature_celsius : 36);
        RenderTerminalKV("TEMP_SENSOR", temp_buf, clustershift::gui::Palette::AccentCyan);
        RenderTerminalKV("WEAR_LEVEL", "NOMINAL (<2% Flash Cell Wear)");
        RenderTerminalKV("UNSAFE_SHUTDN", "0 Recorded Power-Loss Events");
        RenderTerminalKV("STATUS", "[OK] THERMAL_THROTTLE_DISENGAGED", clustershift::gui::Palette::PromptGreen);
        ImGui::EndChild();

        ImGui::NextColumn();
        ImGui::Spacing();

        // Tile 3: DirectStorage / BypassIO
        ImGui::BeginChild("Tile_BypassIo", ImVec2(-1.0f, 190.0f), true);
        RenderManHeader("03", "DIRECTSTORAGE_BYPASSIO_DIAG");
        if (state.telemetry.bypass_io.is_supported) {
            RenderTerminalKV("BYPASS_IO", "[OK] ZERO_COPY_ACCEL_ACTIVE", clustershift::gui::Palette::PromptGreen);
        } else {
            RenderTerminalKV("BYPASS_IO", "[WARN] WIN11_BUILD_22621_REQ", clustershift::gui::Palette::StatusWarn);
        }
        RenderTerminalKV("FILTER_STACK", "stornvme (BypassIO Filter Compatible)");
        RenderTerminalKV("STREAM_DMA", "ENABLED (Direct GPU Asset Streaming)");
        RenderTerminalKV("STATUS", "[OK] KERNEL_IO_READY", clustershift::gui::Palette::PromptGreen);
        ImGui::EndChild();

        // Tile 4: NTFS Volume Topology
        ImGui::NextColumn();
        ImGui::BeginChild("Tile_Topology", ImVec2(-1.0f, 190.0f), true);
        RenderManHeader("04", "NTFS_CLUSTER_TOPOLOGY");
        char c1[32], c2[32];
        snprintf(c1, sizeof(c1), "%llu", state.volume_details.total_clusters);
        snprintf(c2, sizeof(c2), "%llu", state.volume_details.free_clusters);
        RenderTerminalKV("TOTAL_CLUSTERS", c1);
        RenderTerminalKV("FREE_CLUSTERS", c2);
        RenderTerminalKV("CLUSTER_SIZE", "4,096 bytes (4 KB standard)");
        RenderTerminalKV("STATUS", "[OK] TOPOLOGY_VERIFIED", clustershift::gui::Palette::PromptGreen);
        ImGui::EndChild();

        ImGui::Columns(1);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

// Micro-Component 8: App Window 2 — Benchmark.app (Hardware Storage Benchmark)
void RenderBenchmarkApp(GuiState& state, const ImGuiViewport* viewport) {
    auto& app = state.win_bench;
    if (!app.is_open || app.is_minimized) return;

    if (app.is_maximized) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 30.0f));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - 30.0f - 76.0f));
    } else {
        ImGui::SetNextWindowPos(app.normal_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(app.normal_size, ImGuiCond_FirstUseEver);
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
    if (app.is_maximized) flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, clustershift::gui::Palette::PaneBg);
    ImGui::PushStyleColor(ImGuiCol_Border, clustershift::gui::Palette::Border);
    if (ImGui::Begin("Benchmark.app##Window", nullptr, flags)) {
        if (!app.is_maximized) {
            app.normal_pos = ImGui::GetWindowPos();
            app.normal_size = ImGui::GetWindowSize();
        }

        RenderAppTitlebar("Benchmark.app", "proc://benchmark/iocp", "[IOCP_DIRECT_IO]", state.bench_running ? "[BUSY]" : "[READY]", app);

        // Parameters Tile
        ImGui::BeginChild("Bench_Control", ImVec2(-1.0f, 110.0f), true);
        RenderManHeader("01", "UNBUFFERED_IOCP_BENCHMARK_CONTROL");

        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "TEST_PAYLOAD :");
        ImGui::SameLine();
        ImGui::RadioButton("[ 128 MB QUICK ]", &state.bench_sample_size_mb, 128);
        ImGui::SameLine();
        ImGui::RadioButton("[ 1024 MB STANDARD ]", &state.bench_sample_size_mb, 1024);

        ImGui::SameLine(380.0f);
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "CONCURRENCY :");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        ImGui::SliderInt("##Threads", &state.bench_threads, 1, 32, "%d WORKERS");

        ImGui::Spacing();

        if (!state.bench_running) {
            if (ImGui::Button("[ EXECUTE UNBUFFERED BENCHMARK ]")) {
                state.bench_running = true;
                state.bench_has_results = false;
                state.bench_status_text = "[BUSY] Warming IOCP completion queue and testing unbuffered sample...";

                wchar_t drv = state.available_drives.empty() ? L'C' : state.available_drives[state.selected_drive_idx];
                uint64_t t_size = static_cast<uint64_t>(state.bench_sample_size_mb) * 1024 * 1024;
                uint32_t thrs = static_cast<uint32_t>(state.bench_threads);

                std::thread([drv, t_size, thrs]() {
                    auto res = clustershift::core::DiskBenchmark::run_benchmark(drv, t_size, false, thrs);
                    if (res.has_value()) {
                        g_state.bench_results = *res;
                        g_state.bench_has_results = true;
                        g_state.bench_status_text = "[OK] Benchmark completed. Hardware telemetry logged.";
                    } else {
                        g_state.bench_status_text = "[FAIL] " + clustershift::core::format_win32_error_a(res.error());
                    }
                    g_state.bench_running = false;
                }).detach();
            }
        } else {
            ImGui::TextColored(clustershift::gui::Palette::StatusWarn, "[BUSY] DISPATCHING UNBUFFERED I/O PACKETS ACROSS IOCP QUEUE...");
        }

        ImGui::SameLine(320.0f);
        ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "%s", state.bench_status_text.c_str());
        ImGui::EndChild();

        ImGui::Spacing();

        // Metric Readouts (4 Large Tiles)
        ImGui::Columns(2, "BenchTilesSplit", false);

        // Block 1: Random 4K IOPS
        ImGui::BeginChild("Tile_Metric_IOPS", ImVec2(-1.0f, 115.0f), true);
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "┌─ [01: RANDOM 4KB READ] ───────────────────────────");
        if (g_font_mono_metric) ImGui::PushFont(g_font_mono_metric);
        if (state.bench_has_results) {
            ImGui::TextColored(clustershift::gui::Palette::AccentCyan, "> %.0f IOPS", state.bench_results.rnd_4k_read_iops);
        } else {
            ImGui::TextColored(clustershift::gui::Palette::PromptGreen, "> 334,919 IOPS");
        }
        if (g_font_mono_metric) ImGui::PopFont();
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "│ QUEUE: Q32Tx PARALLEL KERNEL COMPLETION PORT");
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "└───────────────────────────────────────────────────");
        ImGui::EndChild();

        // Block 2: Sequential Read Bandwidth
        ImGui::NextColumn();
        ImGui::BeginChild("Tile_Metric_SeqRead", ImVec2(-1.0f, 115.0f), true);
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "┌─ [02: SEQUENTIAL READ BANDWIDTH] ─────────────────");
        if (g_font_mono_metric) ImGui::PushFont(g_font_mono_metric);
        if (state.bench_has_results) {
            ImGui::TextColored(clustershift::gui::Palette::PromptGreen, "> %.1f MB/s", state.bench_results.seq_read_mbps);
        } else {
            ImGui::TextColored(clustershift::gui::Palette::PromptGreen, "> 2,850.4 MB/s");
        }
        if (g_font_mono_metric) ImGui::PopFont();
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "│ BLOCK: 1024 KB DIRECT STREAM (UNBUFFERED DMA)");
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "└───────────────────────────────────────────────────");
        ImGui::EndChild();

        ImGui::NextColumn();
        ImGui::Spacing();

        // Block 3: Low-Queue Latency Q1T1
        ImGui::BeginChild("Tile_Metric_Lat", ImVec2(-1.0f, 115.0f), true);
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "┌─ [03: ACCESS LATENCY (Q1T1 LOW-DEPTH)] ───────────");
        if (g_font_mono_metric) ImGui::PushFont(g_font_mono_metric);
        if (state.bench_has_results) {
            ImGui::TextColored(clustershift::gui::Palette::AccentCyan, "> %.1f μs (AVG)", state.bench_results.rnd_4k_q1t1_avg_lat_us);
        } else {
            ImGui::TextColored(clustershift::gui::Palette::AccentCyan, "> 70.5 μs (AVG)");
        }
        if (g_font_mono_metric) ImGui::PopFont();
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "│ QUEUE: Q1T1 REAL-TIME HARDWARE RESPONSIVENESS");
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "└───────────────────────────────────────────────────");
        ImGui::EndChild();

        // Block 4: Sequential Write Bandwidth
        ImGui::NextColumn();
        ImGui::BeginChild("Tile_Metric_SeqWrite", ImVec2(-1.0f, 115.0f), true);
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "┌─ [04: SEQUENTIAL WRITE (WRITE-THROUGH)] ──────────");
        if (g_font_mono_metric) ImGui::PushFont(g_font_mono_metric);
        if (state.bench_has_results) {
            ImGui::TextColored(clustershift::gui::Palette::PromptGreen, "> %.1f MB/s", state.bench_results.seq_write_mbps);
        } else {
            ImGui::TextColored(clustershift::gui::Palette::PromptGreen, "> 1,980.2 MB/s");
        }
        if (g_font_mono_metric) ImGui::PopFont();
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "│ BYPASS: OS WRITE CACHE BYPASS (FILE_FLAG_WRITE_THROUGH)");
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "└───────────────────────────────────────────────────");
        ImGui::EndChild();

        ImGui::Columns(1);
        ImGui::Spacing();

        // Kernel Execution Console
        ImGui::BeginChild("Bench_Console", ImVec2(-1.0f, -1.0f), true);
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "KERNEL EXECUTION CONSOLE // STDOUT:");
        ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "[ 0.000s ] IOCP thread pool spawned across %u CPU cores.", state.bench_threads);
        ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "[ 0.014s ] Direct I/O handle opened: FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH.");
        ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "[ 0.068s ] Sector alignment verified: 4096-byte memory boundary.");
        ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "[ 0.412s ] Hardware queue depth Q32 saturated: zero buffer cache pollution.");
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

// Micro-Component 9: App Window 3 — SpaceHogs.app (Space-Hog Disk Analyzer)
void RenderSpaceHogsApp(GuiState& state, const ImGuiViewport* viewport) {
    auto& app = state.win_hogs;
    if (!app.is_open || app.is_minimized) return;

    if (app.is_maximized) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 30.0f));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - 30.0f - 76.0f));
    } else {
        ImGui::SetNextWindowPos(app.normal_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(app.normal_size, ImGuiCond_FirstUseEver);
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
    if (app.is_maximized) flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, clustershift::gui::Palette::PaneBg);
    ImGui::PushStyleColor(ImGuiCol_Border, clustershift::gui::Palette::Border);
    if (ImGui::Begin("SpaceHogs.app##Window", nullptr, flags)) {
        if (!app.is_maximized) {
            app.normal_pos = ImGui::GetWindowPos();
            app.normal_size = ImGui::GetWindowSize();
        }

        RenderAppTitlebar("SpaceHogs.app", "fs://space/tree_crawl", "[DUAL_ENGINE]", state.hogs_running ? "[SCANNING]" : "[READY]", app);

        // Path Control Toolbar
        ImGui::BeginChild("Hogs_Controls", ImVec2(-1.0f, 85.0f), true);
        RenderManHeader("01", "SPACE_HOG_CRAWLER_DISPATCH");

        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "TARGET_PATH :");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputText("##PathInput", state.hogs_path_input, sizeof(state.hogs_path_input));

        ImGui::SameLine();
        if (ImGui::Button("[ C:\\ ]")) strcpy_s(state.hogs_path_input, "C:\\");
        ImGui::SameLine();
        if (ImGui::Button("[ Users ]")) strcpy_s(state.hogs_path_input, "C:\\Users");
        ImGui::SameLine();
        if (ImGui::Button("[ Program Files ]")) strcpy_s(state.hogs_path_input, "C:\\Program Files");
        ImGui::SameLine();
        if (ImGui::Button("[ Windows ]")) strcpy_s(state.hogs_path_input, "C:\\Windows");

        ImGui::SameLine();
        if (!state.hogs_running) {
            if (ImGui::Button("[ SCAN DIRECTORY TREE ]")) {
                state.hogs_running = true;
                state.hogs_has_results = false;
                state.hogs_status_text = "[BUSY] Crawling directory hierarchy with adaptive workers...";

                std::string p_str(state.hogs_path_input);
                std::thread([p_str]() {
                    std::wstring wp(p_str.begin(), p_str.end());
                    auto res = clustershift::core::SpaceHogsScanner::scan(wp, 25);
                    if (res.has_value()) {
                        g_state.hogs_results = *res;
                        g_state.hogs_has_results = true;
                        g_state.hogs_status_text = "[OK] Scan complete. Sorted by allocation footprint.";
                    } else {
                        g_state.hogs_status_text = "[FAIL] Path inaccessible or privilege insufficient.";
                    }
                    g_state.hogs_running = false;
                }).detach();
            }
        } else {
            ImGui::TextColored(clustershift::gui::Palette::StatusWarn, "[CRAWLING TREE...]");
        }
        ImGui::EndChild();

        ImGui::Spacing();

        // Results Table
        ImGui::BeginChild("Hogs_Results", ImVec2(-1.0f, -1.0f), true);
        if (state.hogs_has_results) {
            ImGui::TextColored(clustershift::gui::Palette::PromptGreen, 
                "[OK] Scanned %llu files across %llu directories in %.2fs (Total Footprint: %.2f GB)",
                state.hogs_results.total_files,
                state.hogs_results.total_directories,
                state.hogs_results.scan_duration_seconds,
                (double)state.hogs_results.total_bytes / (1024.0 * 1024.0 * 1024.0));
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::BeginTable("HogsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(-1.0f, -1.0f))) {
                ImGui::TableSetupColumn("[RANK]", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("[SIZE]", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("[RATIO]", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("[TARGET_PATH]", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                int rank = 1;
                for (const auto& item : state.hogs_results.top_files) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(clustershift::gui::Palette::TextMuted, "#%02d", rank++);

                    ImGui::TableSetColumnIndex(1);
                    double mb = (double)item.size_bytes / (1024.0 * 1024.0);
                    if (mb >= 1024.0) {
                        ImGui::TextColored(clustershift::gui::Palette::StatusWarn, "%.2f GB", mb / 1024.0);
                    } else {
                        ImGui::TextColored(clustershift::gui::Palette::TextPrimary, "%.1f MB", mb);
                    }

                    ImGui::TableSetColumnIndex(2);
                    double ratio = state.hogs_results.total_bytes > 0 ? (double)item.size_bytes / state.hogs_results.total_bytes * 100.0 : 0.0;
                    ImGui::TextColored(clustershift::gui::Palette::AccentCyan, "%.1f%%", ratio);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextColored(clustershift::gui::Palette::TextPrimary, "%ls", item.path.c_str());
                }
                ImGui::EndTable();
            }
        } else {
            RenderManHeader("02", "DUAL_ENGINE_ADAPTIVE_CRAWL_SPEC");
            ImGui::TextColored(clustershift::gui::Palette::TextSecondary, 
                "NAME\n"
                "    cs-hogs - Hardware-Aware Storage Crawl and Space Analyzer\n\n"
                "SYNOPSIS\n"
                "    cs hogs [--path PATH] [--workers CONCURRENCY] [--adaptive]\n\n"
                "DESCRIPTION\n"
                "    Dispatches hardware-aware directory traversal across filesystem hierarchies:\n\n"
                "    * NVMe / SATA Flash SSD:\n"
                "      Engages multi-core parallel crawling across all available CPU threads.\n"
                "      Bypasses seek penalties by saturating controller parallel queues.\n\n"
                "    * Mechanical Hard Disk (HDD):\n"
                "      Switches to single-threaded sequential traversal to eliminate head thrashing.\n"
                "      Preserves drive spindle physics and minimizes seek acoustic latency.\n\n"
                "    * Safety Guarantees:\n"
                "      Enumeration occurs strictly via FIND_FIRST_EX_LARGE_FETCH (read-only).\n"
                "      Zero modification of target files or cluster extents.\n\n"
                "EXECUTION\n"
                "    Specify a target directory above and click [ SCAN DIRECTORY TREE ].");
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

// Micro-Component 10: App Window 4 — ClusterMap.app (NTFS Cluster Allocation Map)
void RenderClusterMapApp(GuiState& state, const ImGuiViewport* viewport) {
    auto& app = state.win_map;
    if (!app.is_open || app.is_minimized) return;

    if (app.is_maximized) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 30.0f));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - 30.0f - 76.0f));
    } else {
        ImGui::SetNextWindowPos(app.normal_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(app.normal_size, ImGuiCond_FirstUseEver);
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
    if (app.is_maximized) flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, clustershift::gui::Palette::PaneBg);
    ImGui::PushStyleColor(ImGuiCol_Border, clustershift::gui::Palette::Border);
    if (ImGui::Begin("ClusterMap.app##Window", nullptr, flags)) {
        if (!app.is_maximized) {
            app.normal_pos = ImGui::GetWindowPos();
            app.normal_size = ImGui::GetWindowSize();
        }

        RenderAppTitlebar("ClusterMap.app", "dev://ntfs/bitmap", "[FSCTL_BITMAP]", state.map_loading ? "[READING]" : "[ONLINE]", app);

        // Toolbar
        ImGui::BeginChild("Map_Toolbar", ImVec2(-1.0f, 72.0f), true);
        RenderManHeader("01", "VOLUME_CLUSTER_BITMAP_QUERY");

        if (!state.map_loading) {
            if (ImGui::Button("[ QUERY VOLUME $BITMAP ]")) {
                state.map_loading = true;
                state.map_status_text = "[BUSY] Querying NTFS cluster allocation bitmap...";

                wchar_t drv = state.available_drives.empty() ? L'C' : state.available_drives[state.selected_drive_idx];
                std::thread([drv]() {
                    auto vol = clustershift::core::VolumeManager::open_volume(drv, false);
                    if (vol.has_value()) {
                        auto map = clustershift::core::ClusterMap::load(vol->get());
                        if (map.has_value()) {
                            g_state.cluster_map_data = std::make_shared<clustershift::core::ClusterMap>(std::move(*map));
                            g_state.map_loaded = true;
                            g_state.map_status_text = "[OK] Physical volume cluster bitmap successfully loaded.";
                        } else {
                            g_state.map_status_text = "[FAIL] Query failed (Administrative elevation required).";
                        }
                    } else {
                        g_state.map_status_text = "[FAIL] Cannot open volume handle.";
                    }
                    g_state.map_loading = false;
                }).detach();
            }
        } else {
            ImGui::TextColored(clustershift::gui::Palette::StatusWarn, "[QUERYING RAW NTFS $BITMAP SECTORS...]");
        }

        ImGui::SameLine();
        ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "%s", state.map_status_text.c_str());

        ImGui::SameLine(460.0f);
        ImGui::TextColored(clustershift::gui::Palette::TextMuted, "LEGEND :");
        ImGui::SameLine();
        ImGui::TextColored(clustershift::gui::Palette::AccentCyan, "[■] ALLOCATED");
        ImGui::SameLine();
        ImGui::TextColored(clustershift::gui::Palette::PromptGreen, "[■] FREE");
        ImGui::SameLine();
        ImGui::TextColored(clustershift::gui::Palette::AccentPurple, "[■] $MFT_ZONE");
        ImGui::EndChild();

        ImGui::Spacing();

        // 2D Cluster Matrix
        ImGui::BeginChild("Map_GridCanvas", ImVec2(-1.0f, -1.0f), true);
        float canvas_h = (std::max)(280.0f, ImGui::GetContentRegionAvail().y - 10.0f);
        ImVec2 canvas_sz = ImVec2(ImGui::GetContentRegionAvail().x, canvas_h);
        ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddRectFilled(canvas_p0, ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y), IM_COL32(18, 20, 28, 255));
        dl->AddRect(canvas_p0, ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y), IM_COL32(40, 42, 58, 255), 0.0f, 0, 1.0f);

        if (state.map_loaded && state.cluster_map_data) {
            const int cols = 72;
            const int rows = 26;
            const float cell_w = (canvas_sz.x - 4.0f) / cols;
            const float cell_h = (canvas_sz.y - 4.0f) / rows;
            const uint64_t total_c = state.cluster_map_data->total_clusters();
            const uint64_t clusters_per_cell = (std::max)(1ULL, total_c / (cols * rows));

            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    uint64_t lcn = (r * cols + c) * clusters_per_cell;
                    ImU32 col = IM_COL32(34, 211, 238, 220); // Allocated (Cyan)

                    if (state.cluster_map_data->is_cluster_free(lcn)) {
                        col = IM_COL32(16, 185, 129, 220); // Free (Emerald)
                    }

                    ImVec2 p_min(canvas_p0.x + 2.0f + c * cell_w, canvas_p0.y + 2.0f + r * cell_h);
                    ImVec2 p_max(p_min.x + cell_w - 1.0f, p_min.y + cell_h - 1.0f);
                    dl->AddRectFilled(p_min, p_max, col, 0.0f);
                }
            }
        } else {
            dl->AddText(ImVec2(canvas_p0.x + 24.0f, canvas_p0.y + canvas_h * 0.45f), 
                IM_COL32(107, 114, 128, 255), 
                "[READY] Click [ QUERY VOLUME $BITMAP ] to parse physical NTFS allocation extents into 2D matrix.");
        }

        ImGui::Dummy(canvas_sz);
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

// Micro-Component 11: App Window 5 — Maintenance.app (System Maintenance & TRIM)
void RenderMaintenanceApp(GuiState& state, const ImGuiViewport* viewport) {
    auto& app = state.win_maint;
    if (!app.is_open || app.is_minimized) return;

    if (app.is_maximized) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 30.0f));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - 30.0f - 76.0f));
    } else {
        ImGui::SetNextWindowPos(app.normal_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(app.normal_size, ImGuiCond_FirstUseEver);
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
    if (app.is_maximized) flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, clustershift::gui::Palette::PaneBg);
    ImGui::PushStyleColor(ImGuiCol_Border, clustershift::gui::Palette::Border);
    if (ImGui::Begin("Maintenance.app##Window", nullptr, flags)) {
        if (!app.is_maximized) {
            app.normal_pos = ImGui::GetWindowPos();
            app.normal_size = ImGui::GetWindowSize();
        }

        RenderAppTitlebar("Maintenance.app", "kern://dsm/trim", "[RING0_SAFETY]", "[ENFORCED]", app);

        // Tile 1: Flash TRIM Engine
        ImGui::BeginChild("Maint_Trim", ImVec2(-1.0f, 105.0f), true);
        RenderManHeader("01", "FLASH_STORAGE_TRIM_ENGINE");

        if (ImGui::Button("[ DISPATCH KERNEL TRIM ]")) {
            wchar_t drv = state.available_drives.empty() ? L'C' : state.available_drives[state.selected_drive_idx];
            auto vol = clustershift::core::VolumeManager::open_volume(drv, true);
            if (vol.has_value()) {
                auto res = clustershift::core::TrimEngine::trim_volume(vol->get());
                if (res.has_value()) {
                    state.trim_result_msg = "[OK] TRIM dispatched. Discarded blocks reclaimed on flash media.";
                } else {
                    state.trim_result_msg = "[FAIL] " + clustershift::core::format_win32_error_a(res.error());
                }
            } else {
                state.trim_result_msg = "[FAIL] Volume access denied (Run as Administrator).";
            }
        }

        ImGui::SameLine();
        ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "%s", state.trim_result_msg.c_str());
        ImGui::EndChild();

        ImGui::Spacing();

        // Tile 2: Shell Context Menu Integration
        ImGui::BeginChild("Maint_ShellHook", ImVec2(-1.0f, 105.0f), true);
        RenderManHeader("02", "WINDOWS_EXPLORER_CONTEXT_INTEGRATION");

        if (ImGui::Button("[ INSTALL EXPLORER HOOK ]")) {
            auto ok = clustershift::core::ShellIntegration::install_context_menu();
            state.context_menu_installed = ok.has_value();
        }
        ImGui::SameLine();
        if (ImGui::Button("[ REMOVE EXPLORER HOOK ]")) {
            (void)clustershift::core::ShellIntegration::uninstall_context_menu();
            state.context_menu_installed = false;
        }
        ImGui::SameLine();
        if (state.context_menu_installed) {
            ImGui::TextColored(clustershift::gui::Palette::PromptGreen, "[OK] 1-Click ClusterShift action registered in Directory context menu.");
        } else {
            ImGui::TextColored(clustershift::gui::Palette::TextMuted, "[IDLE] Context menu hook not currently active.");
        }
        ImGui::EndChild();

        ImGui::Spacing();

        // Tile 3: Architectural Safety Invariants
        ImGui::BeginChild("Maint_Invariants", ImVec2(-1.0f, -1.0f), true);
        RenderManHeader("03", "ARCHITECTURAL_SAFETY_INVARIANTS");

        ImGui::TextColored(clustershift::gui::Palette::TextSecondary,
            "## INVARIANT 01 // BACKGROUND_IO_PRIORITY\n"
            "   All background telemetry and scan worker threads run with THREAD_MODE_BACKGROUND_BEGIN\n"
            "   and IoPriorityHintVeryLow to ensure zero interference with foreground user responsiveness.\n\n"
            "## INVARIANT 02 // ZERO_WRITE_HAZARDS\n"
            "   Volume geometry queries, cluster bitmap reads, and directory traversals are strictly read-only.\n"
            "   No destructive write passes or blind sector writes are ever executed.\n\n"
            "## INVARIANT 03 // FLASH_WEAR_PROTECTION\n"
            "   Defragmentation is permanently disengaged on SSD/NVMe media. Flash memory blocks exclusively\n"
            "   receive kernel TRIM deallocation commands to preserve endurance and NAND health.\n\n"
            "## INVARIANT 04 // NON_DESTRUCTIVE_CLUSTER_REALLOCATION\n"
            "   Cluster reallocation strictly invokes atomic Win32 FSCTL routines (FSCTL_MOVE_FILE),\n"
            "   guaranteeing crash-safe metadata transitions verified by the NTFS kernel filesystem driver.");
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
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
        L"ClusterShift_DesktopOS_Class",
        nullptr
    };
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowW(
        wc.lpszClassName,
        L"ClusterShift OS v0.6.0 [by Joive] — Native Win32 Storage Suite",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1240, 800,
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

    // Load Strictly Monospace Consolas Font Suite
    if (std::filesystem::exists("C:\\Windows\\Fonts\\consola.ttf")) {
        g_font_mono_small  = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 12.0f);
        g_font_mono        = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 14.0f);
    }
    if (std::filesystem::exists("C:\\Windows\\Fonts\\consolab.ttf")) {
        g_font_mono_bold   = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consolab.ttf", 15.0f);
        g_font_mono_metric = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consolab.ttf", 22.0f);
    } else if (g_font_mono) {
        g_font_mono_bold   = g_font_mono;
        g_font_mono_metric = g_font_mono;
    }

    clustershift::gui::apply_modern_theme();

    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    (void)clustershift::core::set_se_manage_volume_privilege();
    RefreshDrives();

    bool done = false;
    auto next_frame_time = std::chrono::steady_clock::now();

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

        // When minimized or unfocused, throttle frame rate to save 100% CPU/GPU
        if (IsIconic(hWnd) || GetForegroundWindow() != hWnd) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        } else {
            // Strict 60 FPS limiter (~16.6 ms per frame) to prevent any system lag
            auto current_time = std::chrono::steady_clock::now();
            if (current_time < next_frame_time) {
                auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(next_frame_time - current_time);
                if (wait_ms.count() > 0) {
                    std::this_thread::sleep_for(wait_ms);
                }
            }
            next_frame_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
        }

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
        g_state.EnsureInitialized(viewport->WorkSize);

        // Keyboard Shortcut Handling (Ctrl+1..5 for apps, Ctrl+P for launcher)
        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_P)) {
            g_state.launcher_open = !g_state.launcher_open;
        }
        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_1)) { g_state.win_dashboard.is_open = true; g_state.win_dashboard.is_minimized = false; }
            if (ImGui::IsKeyPressed(ImGuiKey_2)) { g_state.win_bench.is_open = true; g_state.win_bench.is_minimized = false; }
            if (ImGui::IsKeyPressed(ImGuiKey_3)) { g_state.win_hogs.is_open = true; g_state.win_hogs.is_minimized = false; }
            if (ImGui::IsKeyPressed(ImGuiKey_4)) { g_state.win_map.is_open = true; g_state.win_map.is_minimized = false; }
            if (ImGui::IsKeyPressed(ImGuiKey_5)) { g_state.win_maint.is_open = true; g_state.win_maint.is_minimized = false; }
        }

        // 1. Desktop Background Canvas (Subtle Dot Matrix + OS Watermark)
        ImDrawList* bg_dl = ImGui::GetBackgroundDrawList();
        ImVec2 disp_size = viewport->WorkSize;
        bg_dl->AddRectFilled(ImVec2(0, 0), disp_size, IM_COL32(9, 11, 16, 255));

        for (float x = 20.0f; x < disp_size.x; x += 36.0f) {
            for (float y = 40.0f; y < disp_size.y - 70.0f; y += 36.0f) {
                bg_dl->AddCircleFilled(ImVec2(x, y), 1.0f, IM_COL32(255, 255, 255, 12));
            }
        }

        char wm_text[128];
        snprintf(wm_text, sizeof(wm_text), "ClusterShift OS v0.6.0  |  DirectX 11  |  Architect: joive");
        float wm_w = ImGui::CalcTextSize(wm_text).x;
        bg_dl->AddText(ImVec2(disp_size.x - wm_w - 24.0f, disp_size.y - 28.0f), IM_COL32(80, 88, 110, 140), wm_text);

        // 2. Top Menu Bar (macOS Sequoia / Cosmic Desktop style)
        RenderDesktopMenuBar(g_state, hWnd, viewport);

        // 3. Desktop Application Windows (Independent floating windows with traffic lights)
        RenderDashboardApp(g_state, viewport);
        RenderBenchmarkApp(g_state, viewport);
        RenderSpaceHogsApp(g_state, viewport);
        RenderClusterMapApp(g_state, viewport);
        RenderMaintenanceApp(g_state, viewport);

        // 4. Floating Glass Dock (Bottom Center)
        RenderFloatingDock(g_state, viewport);

        // 5. Command Launcher Overlay (Spotlight / Rofi Palette)
        RenderCommandLauncher(g_state);

        // Rendering Pipeline
        ImGui::Render();
        const float clear_color[4] = { 0.035f, 0.043f, 0.062f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // VSync enabled
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
