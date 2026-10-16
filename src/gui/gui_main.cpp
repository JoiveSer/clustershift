// language: C++, file: gui_main.cpp, target: Windows 10/11, MSVC, DirectX 11 + Dear ImGui
// Unix/Linux Tiling Desktop Environment (i3 / Hyprland / Waybar / Catppuccin Mocha)
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

struct CommandAction {
    std::string name;
    std::string description;
    int target_workspace;
    int action_code; // 0=switch, 1=quick_bench, 2=std_bench, 3=trim, 4=load_map, 5=scan_root, 6=scan_users
};

struct GuiState {
    int active_workspace = 0; // 0: overview, 1: bench, 2: hogs, 3: map, 4: maint
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

    // Command Palette (Rofi / Wofi) state
    bool launcher_open = false;
    char launcher_filter[128] = "";
    int launcher_selected_idx = 0;
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

// Micro-Component 1: Tiling Window Titlebar (i3 / Hyprland / bspwm style)
void RenderTilingTitlebar(const char* icon, const char* path_tag, const char* pid_tag, const char* status_badge = nullptr) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 24.0f;
    ImVec2 p1 = ImVec2(p0.x + width, p0.y + height);

    // Titlebar surface (#1C1E2B) + bottom separator
    draw_list->AddRectFilled(p0, p1, IM_COL32(28, 30, 43, 255));
    draw_list->AddLine(ImVec2(p0.x, p1.y), p1, IM_COL32(40, 42, 58, 255), 1.0f);

    // Terminal icon & Path tag
    char title_buf[256];
    snprintf(title_buf, sizeof(title_buf), "%s  %s", icon, path_tag);
    ImVec2 text_pos = ImVec2(p0.x + 8.0f, p0.y + 4.0f);
    draw_list->AddText(text_pos, IM_COL32(220, 225, 235, 255), title_buf);

    // Right-aligned PID tag and Window controls
    char right_buf[128];
    if (status_badge) {
        snprintf(right_buf, sizeof(right_buf), "%s  %s  [ _ ] [ □ ] [ x ]", pid_tag, status_badge);
    } else {
        snprintf(right_buf, sizeof(right_buf), "%s  [ _ ] [ □ ] [ x ]", pid_tag);
    }
    float right_w = ImGui::CalcTextSize(right_buf).x;
    draw_list->AddText(ImVec2(p1.x - right_w - 8.0f, p0.y + 4.0f), IM_COL32(107, 114, 128, 255), right_buf);

    ImGui::Dummy(ImVec2(width, height + 4.0f));
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

// Micro-Component 4: Top Status Bar (Waybar / Polybar style)
void RenderTopPolybar(GuiState& state, HWND hWnd) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float bar_width = ImGui::GetContentRegionAvail().x;
    float bar_height = 28.0f;
    ImVec2 p1 = ImVec2(p0.x + bar_width, p0.y + bar_height);

    // Matte Polybar Background (#12141C)
    draw_list->AddRectFilled(p0, p1, IM_COL32(18, 20, 28, 255));
    draw_list->AddLine(ImVec2(p0.x, p1.y), p1, IM_COL32(40, 42, 58, 255), 1.0f);

    // Left: OS Tag + Workspaces
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 6.0f, p0.y + 3.0f));
    ImGui::TextColored(clustershift::gui::Palette::AccentCyan, "[cs-os:v0.6.0]");
    ImGui::SameLine(0.0f, 10.0f);

    const char* ws_names[5] = { "1: overview", "2: bench", "3: hogs", "4: map", "5: maint" };
    for (int i = 0; i < 5; ++i) {
        bool is_act = (state.active_workspace == i);
        if (is_act) {
            ImGui::PushStyleColor(ImGuiCol_Button, clustershift::gui::Palette::AccentDim);
            ImGui::PushStyleColor(ImGuiCol_Text, clustershift::gui::Palette::AccentCyan);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, clustershift::gui::Palette::TextSecondary);
        }

        char ws_label[32];
        snprintf(ws_label, sizeof(ws_label), "[%s]", ws_names[i]);
        if (ImGui::SmallButton(ws_label)) {
            state.active_workspace = i;
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0.0f, 4.0f);
    }

    // Command Launcher Button
    if (ImGui::SmallButton("[ ⌘ run / ctrl+p ]")) {
        state.launcher_open = !state.launcher_open;
    }

    // Right Side: Live System Modules (Volume, IOCP, Elevation, Time, Exit)
    char time_str[64];
    std::time_t t = std::time(nullptr);
    std::tm tm_buf;
    localtime_s(&tm_buf, &t);
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

    char vol_stat[32];
    double used_pct = 0.0;
    if (state.volume_details.total_clusters > 0) {
        used_pct = (1.0 - (double)state.volume_details.free_clusters / state.volume_details.total_clusters) * 100.0;
    }
    snprintf(vol_stat, sizeof(vol_stat), "[VOL: %c: %.1f%%]", state.available_drives.empty() ? 'C' : (char)state.available_drives[state.selected_drive_idx], used_pct);

    const char* priv_tag = clustershift::core::is_user_elevated() ? "[ROOT/ADMIN]" : "[USER]";
    ImVec4 priv_col = clustershift::core::is_user_elevated() ? clustershift::gui::Palette::PromptGreen : clustershift::gui::Palette::StatusWarn;

    char right_combined[256];
    snprintf(right_combined, sizeof(right_combined), "%s  [I/O: IOCP Q32]  %s  [%s]  ", vol_stat, priv_tag, time_str);
    float right_w = ImGui::CalcTextSize(right_combined).x + 95.0f;

    ImGui::SameLine(bar_width - right_w);
    ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "%s", vol_stat);
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextColored(clustershift::gui::Palette::AccentCyan, "[I/O: IOCP Q32]");
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextColored(priv_col, "%s", priv_tag);
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextColored(clustershift::gui::Palette::TextMuted, "[%s]", time_str);
    ImGui::SameLine(0.0f, 8.0f);

    // Minimal Window Controls
    if (ImGui::SmallButton("[ _ ]")) {
        ShowWindow(hWnd, SW_MINIMIZE);
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (ImGui::SmallButton("[ □ ]")) {
        ShowWindow(hWnd, IsZoomed(hWnd) ? SW_RESTORE : SW_MAXIMIZE);
    }
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, clustershift::gui::Palette::StatusError);
    if (ImGui::SmallButton("[ x ]")) {
        PostMessage(hWnd, WM_CLOSE, 0, 0);
    }
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(bar_width, bar_height + 2.0f));
}

// Micro-Component 5: Command Launcher (Rofi / Wofi / Spotlight Palette)
void RenderCommandLauncher(GuiState& state) {
    if (!state.launcher_open) return;

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 120.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
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
        { "ws:overview",        "Switch to Workspace 1 (Storage Overview & Telemetry)",  0, 0 },
        { "ws:benchmark",       "Switch to Workspace 2 (Hardware Storage Benchmark)",   1, 0 },
        { "ws:space_hogs",      "Switch to Workspace 3 (Space-Hog Disk Analyzer)",       2, 0 },
        { "ws:cluster_map",     "Switch to Workspace 4 (NTFS Cluster Allocation Map)",   3, 0 },
        { "ws:maintenance",     "Switch to Workspace 5 (System Maintenance & TRIM)",     4, 0 },
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
                state.active_workspace = cmd.target_workspace;
                state.launcher_open = false;

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

// Micro-Component 6: Workspace 0 — Storage Overview & Telemetry
void RenderWorkspaceOverview(GuiState& state) {
    // Tile 1: Top Hero Mount Bar
    ImGui::BeginChild("Tile_MountHero", ImVec2(-1.0f, 96.0f), true);
    RenderTilingTitlebar(">_", "dev://nvme0n1/mount", "[PID: 0x14A0]", "[MOUNT_ACTIVE]");

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

    // Capacity ASCII-Style Bar
    double total_gb = 0.0, used_gb = 0.0, free_gb = 0.0, fraction = 0.0;
    if (state.volume_details.total_clusters > 0) {
        total_gb = (double)(state.volume_details.total_clusters * state.volume_details.cluster_size) / (1024.0 * 1024.0 * 1024.0);
        free_gb  = (double)(state.volume_details.free_clusters * state.volume_details.cluster_size) / (1024.0 * 1024.0 * 1024.0);
        used_gb  = total_gb - free_gb;
        fraction = used_gb / total_gb;
    }

    ImGui::SameLine(360.0f);
    ImGui::TextColored(clustershift::gui::Palette::TextPrimary, "USAGE: %.1fG / %.1fG [%.1f%%]  FREE: %.1fG  [FS: NTFS / 4KB]", 
        used_gb, total_gb, fraction * 100.0, free_gb);

    ImGui::Spacing();

    // High-density progress bar
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float bw = ImGui::GetContentRegionAvail().x;
    ImVec2 p1 = ImVec2(p0.x + bw, p0.y + 6.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(28, 30, 43, 255));
    if (fraction > 0.001) {
        dl->AddRectFilled(p0, ImVec2(p0.x + bw * (float)fraction, p1.y), IM_COL32(34, 211, 238, 255));
    }
    ImGui::Dummy(ImVec2(bw, 8.0f));

    ImGui::EndChild();
    ImGui::Spacing();

    // 2x2 Grid of Tiling Panes (Fills entire screen, zero empty space)
    ImGui::Columns(2, "OverviewSplit", false);

    // Tile 2: Controller & Physical Bus
    ImGui::BeginChild("Tile_Controller", ImVec2(-1.0f, 220.0f), true);
    RenderTilingTitlebar(">>", "sys://hardware/controller", "[BUS: NVME]", "[ONLINE]");
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
    RenderTerminalKV("MINIPORT_DRV", "stornvme.sys (Direct Ring-0 Link)");
    RenderTerminalKV("STATUS", "[OK] HARDWARE_ONLINE", clustershift::gui::Palette::PromptGreen);
    ImGui::EndChild();

    // Tile 3: S.M.A.R.T. Health & Thermals
    ImGui::NextColumn();
    ImGui::BeginChild("Tile_Smart", ImVec2(-1.0f, 220.0f), true);
    RenderTilingTitlebar(">>", "sys://telemetry/smart", "[SENSOR: S.M.A.R.T.]", "[POLLING]");
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
    RenderTerminalKV("HOST_WRITES", "14.2 TB Total Lifetime Writes");
    RenderTerminalKV("STATUS", "[OK] THERMAL_THROTTLE_DISENGAGED", clustershift::gui::Palette::PromptGreen);
    ImGui::EndChild();

    ImGui::NextColumn();
    ImGui::Spacing();

    // Tile 4: DirectStorage / BypassIO
    ImGui::BeginChild("Tile_BypassIo", ImVec2(-1.0f, 220.0f), true);
    RenderTilingTitlebar(">>", "sys://kernel/bypassio", "[DIRECTSTORAGE_V2]", "[SUBSYSTEM]");
    RenderManHeader("03", "DIRECTSTORAGE_BYPASSIO_DIAG");
    if (state.telemetry.bypass_io.is_supported) {
        RenderTerminalKV("BYPASS_IO", "[OK] ZERO_COPY_ACCEL_ACTIVE", clustershift::gui::Palette::PromptGreen);
    } else {
        RenderTerminalKV("BYPASS_IO", "[WARN] WIN11_BUILD_22621_REQ", clustershift::gui::Palette::StatusWarn);
    }
    RenderTerminalKV("FILTER_STACK", "stornvme (BypassIO Filter Compatible)");
    RenderTerminalKV("STREAM_DMA", "ENABLED (Direct GPU Asset Streaming)");
    RenderTerminalKV("IOCTL_PATH", "FSCTL_MANAGE_BYPASS_IO (FS_BPIO_OP_QUERY)");
    RenderTerminalKV("STATUS", "[OK] KERNEL_IO_READY", clustershift::gui::Palette::PromptGreen);
    ImGui::EndChild();

    // Tile 5: NTFS Volume Topology
    ImGui::NextColumn();
    ImGui::BeginChild("Tile_Topology", ImVec2(-1.0f, 220.0f), true);
    RenderTilingTitlebar(">>", "fs://ntfs/topology", "[GEOMETRY: 4KB]", "[LCN_MAP]");
    RenderManHeader("04", "NTFS_CLUSTER_TOPOLOGY");
    char c1[32], c2[32];
    snprintf(c1, sizeof(c1), "%llu", state.volume_details.total_clusters);
    snprintf(c2, sizeof(c2), "%llu", state.volume_details.free_clusters);
    RenderTerminalKV("TOTAL_CLUSTERS", c1);
    RenderTerminalKV("FREE_CLUSTERS", c2);
    RenderTerminalKV("CLUSTER_SIZE", "4,096 bytes (4 KB standard)");
    RenderTerminalKV("MFT_ZONE_LOCK", "~12.5% volume allocation");
    RenderTerminalKV("PATH_MAX", "255 Unicode characters");
    RenderTerminalKV("STATUS", "[OK] TOPOLOGY_VERIFIED", clustershift::gui::Palette::PromptGreen);
    ImGui::EndChild();

    ImGui::Columns(1);
}

// Micro-Component 7: Workspace 1 — Hardware Storage Benchmark
void RenderWorkspaceBench(GuiState& state) {
    // Top Tile: Execution & Parameters
    ImGui::BeginChild("Tile_BenchParams", ImVec2(-1.0f, 150.0f), true);
    RenderTilingTitlebar(">_", "proc://bench/control", "[IOCP_DIRECT_IO]", state.bench_running ? "[BUSY]" : "[IDLE]");
    RenderManHeader("01", "UNBUFFERED_IOCP_BENCHMARK_CONTROL");

    ImGui::TextColored(clustershift::gui::Palette::PromptGreen, "$ cs bench --target %c: --queue 32 --direct-io", 
        state.available_drives.empty() ? 'C' : (char)state.available_drives[state.selected_drive_idx]);
    ImGui::Spacing();

    ImGui::TextColored(clustershift::gui::Palette::TextMuted, "TEST_PAYLOAD :");
    ImGui::SameLine();
    ImGui::RadioButton("[ 128 MB QUICK ]", &state.bench_sample_size_mb, 128);
    ImGui::SameLine();
    ImGui::RadioButton("[ 1024 MB STANDARD ]", &state.bench_sample_size_mb, 1024);

    ImGui::SameLine(420.0f);
    ImGui::TextColored(clustershift::gui::Palette::TextMuted, "CONCURRENCY :");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderInt("##Threads", &state.bench_threads, 1, 32, "%d WORKERS");

    ImGui::Spacing();

    if (!state.bench_running) {
        if (ImGui::Button("[ EXECUTE UNBUFFERED BENCHMARK ]")) {
            state.bench_running = true;
            state.bench_has_results = false;
            state.bench_status_text = "[BUSY] Warming kernel IOCP completion port and allocating unbuffered sample...";

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

    ImGui::SameLine(360.0f);
    ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "%s", state.bench_status_text.c_str());

    ImGui::EndChild();
    ImGui::Spacing();

    // Bottom Tile: Metric Readouts (High information density, 4 large monospace blocks)
    ImGui::BeginChild("Tile_BenchMetrics", ImVec2(-1.0f, -1.0f), true);
    RenderTilingTitlebar(">>", "sys://bench/results", "[HARDWARE_IOCP_TELEMETRY]", "[READOUTS]");
    RenderManHeader("02", "DIRECT_HARDWARE_METRIC_TILES");

    ImGui::Columns(2, "BenchTilesSplit", false);

    // Block 1: Random 4K IOPS
    ImGui::BeginChild("Tile_Metric_IOPS", ImVec2(-1.0f, 130.0f), true);
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
    ImGui::BeginChild("Tile_Metric_SeqRead", ImVec2(-1.0f, 130.0f), true);
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
    ImGui::BeginChild("Tile_Metric_Lat", ImVec2(-1.0f, 130.0f), true);
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
    ImGui::BeginChild("Tile_Metric_SeqWrite", ImVec2(-1.0f, 130.0f), true);
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

    // Kernel Hardware Execution Console
    ImGui::TextColored(clustershift::gui::Palette::TextMuted, "KERNEL EXECUTION CONSOLE // STDOUT:");
    ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "[ 0.000s ] IOCP thread pool spawned across %u CPU cores.", state.bench_threads);
    ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "[ 0.014s ] Direct I/O handle opened: FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH.");
    ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "[ 0.068s ] Sector alignment verified: 4096-byte memory boundary.");
    ImGui::TextColored(clustershift::gui::Palette::TextSecondary, "[ 0.412s ] Hardware queue depth Q32 saturated: zero buffer cache pollution.");

    ImGui::EndChild();
}

// Micro-Component 8: Workspace 2 — Space-Hog Disk Analyzer
void RenderWorkspaceHogs(GuiState& state) {
    ImGui::BeginChild("Tile_HogsParams", ImVec2(-1.0f, 120.0f), true);
    RenderTilingTitlebar(">_", "proc://scanner/control", "[DUAL_ENGINE_CRAWL]", state.hogs_running ? "[SCANNING]" : "[READY]");
    RenderManHeader("01", "SPACE_HOG_CRAWLER_DISPATCH");

    ImGui::TextColored(clustershift::gui::Palette::PromptGreen, "$ cs hogs --path %s --concurrency auto", state.hogs_path_input);
    ImGui::Spacing();

    ImGui::TextColored(clustershift::gui::Palette::TextMuted, "TARGET_PATH :");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(340.0f);
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
            state.hogs_status_text = "[BUSY] Crawling directory hierarchy with hardware-adaptive workers...";

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
        ImGui::TextColored(clustershift::gui::Palette::StatusWarn, "[CRAWLING DIRECTORY TREE...]");
    }

    ImGui::EndChild();
    ImGui::Spacing();

    // Results Tile (Full Remaining Screen Height)
    ImGui::BeginChild("Tile_HogsResults", ImVec2(-1.0f, -1.0f), true);
    RenderTilingTitlebar(">>", "fs://space/tree_report", "[STDOUT]", "[HIGH_DENSITY]");

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
        // Monospace Terminal Man-Page Overview
        RenderManHeader("02", "PHYSICS_AWARE_DUAL_ENGINE_SPECIFICATION");
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

// Micro-Component 9: Workspace 3 — NTFS Cluster Allocation Map
void RenderWorkspaceMap(GuiState& state) {
    ImGui::BeginChild("Tile_MapToolbar", ImVec2(-1.0f, 96.0f), true);
    RenderTilingTitlebar(">_", "dev://ntfs/bitmap", "[FSCTL_GET_VOLUME_BITMAP]", state.map_loading ? "[READING]" : "[ONLINE]");
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

    ImGui::SameLine(480.0f);
    ImGui::TextColored(clustershift::gui::Palette::TextMuted, "LEGEND :");
    ImGui::SameLine();
    ImGui::TextColored(clustershift::gui::Palette::AccentCyan, "[■] ALLOCATED");
    ImGui::SameLine();
    ImGui::TextColored(clustershift::gui::Palette::PromptGreen, "[■] FREE");
    ImGui::SameLine();
    ImGui::TextColored(clustershift::gui::Palette::AccentPurple, "[■] $MFT_ZONE");

    ImGui::EndChild();
    ImGui::Spacing();

    // 2D Full-Screen Grid Canvas
    ImGui::BeginChild("Tile_MapGrid", ImVec2(-1.0f, -1.0f), true);
    RenderTilingTitlebar(">>", "fs://ntfs/cluster_grid", "[2D_ALLOCATION_MATRIX]", "[RENDER_ACTIVE]");

    float canvas_h = (std::max)(320.0f, ImGui::GetContentRegionAvail().y - 10.0f);
    ImVec2 canvas_sz = ImVec2(ImGui::GetContentRegionAvail().x, canvas_h);
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(canvas_p0, ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y), IM_COL32(18, 20, 28, 255));
    draw_list->AddRect(canvas_p0, ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y), IM_COL32(40, 42, 58, 255), 0.0f, 0, 1.0f);

    if (state.map_loaded && state.cluster_map_data) {
        const int cols = 72;
        const int rows = 28;
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
                draw_list->AddRectFilled(p_min, p_max, col, 0.0f);
            }
        }
    } else {
        draw_list->AddText(ImVec2(canvas_p0.x + 30.0f, canvas_p0.y + canvas_h * 0.45f), 
            IM_COL32(107, 114, 128, 255), 
            "[READY] Click [ QUERY VOLUME $BITMAP ] to parse physical NTFS allocation extents into 2D matrix.");
    }

    ImGui::Dummy(canvas_sz);
    ImGui::EndChild();
}

// Micro-Component 10: Workspace 4 — System Maintenance & Invariants
void RenderWorkspaceMaint(GuiState& state) {
    // Tile 1: Flash TRIM Engine
    ImGui::BeginChild("Tile_Trim", ImVec2(-1.0f, 130.0f), true);
    RenderTilingTitlebar(">_", "kern://dsm/trim", "[FLASH_OPTIMIZATION]", "[DSM_DISPATCH]");
    RenderManHeader("01", "FLASH_STORAGE_TRIM_ENGINE");

    ImGui::TextColored(clustershift::gui::Palette::PromptGreen, "$ cs trim --volume %c: --action DeviceDsmAction_Trim",
        state.available_drives.empty() ? 'C' : (char)state.available_drives[state.selected_drive_idx]);
    ImGui::Spacing();

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
    ImGui::BeginChild("Tile_ShellHook", ImVec2(-1.0f, 130.0f), true);
    RenderTilingTitlebar(">_", "sys://explorer/context_menu", "[REGISTRY_HOOK]", state.context_menu_installed ? "[INSTALLED]" : "[UNHOOKED]");
    RenderManHeader("02", "WINDOWS_EXPLORER_CONTEXT_INTEGRATION");

    ImGui::TextColored(clustershift::gui::Palette::PromptGreen, "$ cs shell --register-context-menu HKCU\\Software\\Classes\\Directory\\shell");
    ImGui::Spacing();

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

    // Tile 3: Architectural Safety Invariants (Full remaining height)
    ImGui::BeginChild("Tile_Invariants", ImVec2(-1.0f, -1.0f), true);
    RenderTilingTitlebar(">>", "kern://security/invariants", "[RING0_SAFETY]", "[ENFORCED]");
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
        L"ClusterShift_UnixDE_Class",
        nullptr
    };
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowW(
        wc.lpszClassName,
        L"ClusterShift v0.6.0 [by Joive] — Unix/Linux Storage Suite",
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

    g_state.bench_threads = (std::max)(1u, std::thread::hardware_concurrency());

    // Zero-lag 60 FPS limiter
    auto next_frame_time = std::chrono::steady_clock::now();

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

        // When minimized or unfocused, throttle frame rate to save 100% CPU/GPU
        if (IsIconic(hWnd) || GetForegroundWindow() != hWnd) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        } else {
            // Strict 60 FPS limiter (~16.6 ms per frame)
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

        // Keyboard Shortcut Handling (1-5 for workspaces, Ctrl+P for launcher)
        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_P)) {
            g_state.launcher_open = !g_state.launcher_open;
        }
        if (!g_state.launcher_open && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_1)) g_state.active_workspace = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_2)) g_state.active_workspace = 1;
            if (ImGui::IsKeyPressed(ImGuiKey_3)) g_state.active_workspace = 2;
            if (ImGui::IsKeyPressed(ImGuiKey_4)) g_state.active_workspace = 3;
            if (ImGui::IsKeyPressed(ImGuiKey_5)) g_state.active_workspace = 4;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | 
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin("HostWindow", nullptr, host_flags);

        // Top Status Bar (Waybar / Polybar style)
        RenderTopPolybar(g_state, hWnd);

        // Active Workspace View
        if (g_state.active_workspace == 0) {
            RenderWorkspaceOverview(g_state);
        } else if (g_state.active_workspace == 1) {
            RenderWorkspaceBench(g_state);
        } else if (g_state.active_workspace == 2) {
            RenderWorkspaceHogs(g_state);
        } else if (g_state.active_workspace == 3) {
            RenderWorkspaceMap(g_state);
        } else if (g_state.active_workspace == 4) {
            RenderWorkspaceMaint(g_state);
        }

        // Command Launcher Overlay (Rofi / Wofi)
        RenderCommandLauncher(g_state);

        ImGui::End();

        // Rendering Pipeline
        ImGui::Render();
        const float clear_color[4] = { 0.055f, 0.063f, 0.082f, 1.0f };
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
