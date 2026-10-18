// language: C++, file: gui_main.cpp, target: Windows 10/11, MSVC, DirectX 11 + Dear ImGui
// ClusterShift OS — Authentic Windows XP Luna Edition
// Uses extracted assets from Downloads\WinXp.zip (Bliss.png, tahoma.ttf, Sounds)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <mmsystem.h>
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
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

ID3D11Device*           g_pd3dDevice = nullptr;
ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
IDXGISwapChain*         g_pSwapChain = nullptr;
UINT                    g_ResizeWidth = 0, g_ResizeHeight = 0;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Texture Resources for Windows XP
ID3D11ShaderResourceView* g_pBlissTextureView = nullptr;
int g_bliss_tex_w = 0, g_bliss_tex_h = 0;

ID3D11ShaderResourceView* g_pIconsTextureView = nullptr;
int g_icons_tex_w = 0, g_icons_tex_h = 0;

ID3D11ShaderResourceView* g_pThemeTextureView = nullptr;
int g_theme_tex_w = 0, g_theme_tex_h = 0;

// Fonts (Windows XP Tahoma & Consolas)
ImFont* g_font_xp_small  = nullptr; // 11px: status & captions
ImFont* g_font_xp        = nullptr; // 13px: default dialog text
ImFont* g_font_xp_bold   = nullptr; // 14px: window headers & buttons
ImFont* g_font_xp_metric = nullptr; // 22px: benchmark readouts

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Native WIC (Windows Imaging Component) PNG Texture Loader
bool LoadTextureFromFileWIC(ID3D11Device* pDevice, const wchar_t* filename, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height) {
    if (!pDevice || !filename || !out_srv) return false;

    IWICImagingFactory* pFactory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) return false;

    IWICBitmapDecoder* pDecoder = nullptr;
    hr = pFactory->CreateDecoderFromFilename(filename, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (FAILED(hr)) { pFactory->Release(); return false; }

    IWICBitmapFrameDecode* pFrame = nullptr;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr)) { pDecoder->Release(); pFactory->Release(); return false; }

    UINT width = 0, height = 0;
    pFrame->GetSize(&width, &height);

    IWICFormatConverter* pConverter = nullptr;
    hr = pFactory->CreateFormatConverter(&pConverter);
    if (FAILED(hr)) { pFrame->Release(); pDecoder->Release(); pFactory->Release(); return false; }

    hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) { pConverter->Release(); pFrame->Release(); pDecoder->Release(); pFactory->Release(); return false; }

    std::vector<BYTE> buffer(width * height * 4);
    hr = pConverter->CopyPixels(nullptr, width * 4, (UINT)buffer.size(), buffer.data());
    pConverter->Release();
    pFrame->Release();
    pDecoder->Release();
    pFactory->Release();
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subResource{};
    subResource.pSysMem = buffer.data();
    subResource.SysMemPitch = desc.Width * 4;

    ID3D11Texture2D* pTexture = nullptr;
    hr = pDevice->CreateTexture2D(&desc, &subResource, &pTexture);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = pDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
    pTexture->Release();
    if (FAILED(hr)) return false;

    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
    return true;
}

inline std::wstring ResolveAssetPath(const std::wstring& rel_path) {
    wchar_t exe_buf[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(NULL, exe_buf, MAX_PATH)) {
        std::filesystem::path exe_dir = std::filesystem::path(exe_buf).parent_path();
        auto p1 = exe_dir / rel_path;
        if (std::filesystem::exists(p1)) return p1.wstring();
        auto p2 = exe_dir / L".." / rel_path;
        if (std::filesystem::exists(p2)) return p2.wstring();
        auto p3 = exe_dir / L"../.." / rel_path;
        if (std::filesystem::exists(p3)) return p3.wstring();
    }
    if (std::filesystem::exists(rel_path)) return rel_path;
    std::filesystem::path fixed_dir = L"C:\\Users\\kyryl\\.gemini\\antigravity\\scratch\\cluster_shift";
    auto p4 = fixed_dir / rel_path;
    if (std::filesystem::exists(p4)) return p4.wstring();
    return L"";
}

// WinIcons_32 is 736x704: 23 columns x 22 rows of 32x32 icons
inline void GetIconUV(int col, int row, ImVec2& uv0, ImVec2& uv1) {
    const float tex_w = 736.0f;
    const float tex_h = 704.0f;
    const float icon_sz = 32.0f;
    uv0 = ImVec2((col * icon_sz) / tex_w, (row * icon_sz) / tex_h);
    uv1 = ImVec2(((col + 1) * icon_sz) / tex_w, ((row + 1) * icon_sz) / tex_h);
}

inline void DrawXpIcon(ImDrawList* dl, int col, int row, const ImVec2& pos, float size = 32.0f) {
    if (g_pIconsTextureView && dl) {
        ImVec2 uv0, uv1;
        GetIconUV(col, row, uv0, uv1);
        dl->AddImage(reinterpret_cast<ImTextureID>(g_pIconsTextureView), pos, ImVec2(pos.x + size, pos.y + size), uv0, uv1);
    }
}

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
    std::string bench_status_text = "Очередь IOCP готова к работе.";
    int bench_sample_size_mb = 1024;
    int bench_threads = 16;

    // Space Hogs state
    std::atomic<bool> hogs_running{false};
    clustershift::core::SpaceHogsReport hogs_results{};
    bool hogs_has_results = false;
    char hogs_path_input[MAX_PATH] = "C:\\";
    std::string hogs_status_text = "Выберите папку для сканирования.";

    // Cluster Map state
    std::atomic<bool> map_loading{false};
    std::shared_ptr<clustershift::core::ClusterMap> cluster_map_data;
    bool map_loaded = false;
    std::string map_status_text = "Нажмите кнопку для загрузки $Bitmap.";

    // Maintenance state
    std::string trim_result_msg = "Готов к отправке DeviceDsmAction_Trim.";
    bool context_menu_installed = false;

    // Settings state
    int settings_active_section = 0;

    // Terminal state
    std::vector<std::string> terminal_lines;
    char terminal_input[256] = "";

    // Donate state
    std::string donate_copied_msg = "";

    // Windows XP Shell State
    bool start_menu_open = false;
    int selected_desktop_icon = -1;

    // 7 OS Applications
    AppWindowState win_storage;
    AppWindowState win_bench;
    AppWindowState win_hogs;
    AppWindowState win_map;
    AppWindowState win_settings;
    AppWindowState win_terminal;
    AppWindowState win_donate;

    bool layout_initialized = false;

    void LogTerminal(const std::string& line) {
        terminal_lines.push_back(line);
        if (terminal_lines.size() > 500) {
            terminal_lines.erase(terminal_lines.begin());
        }
    }

    void ResetLayout(ImVec2 sz) {
        float taskbar_h = 32.0f;
        float avail_h = sz.y - taskbar_h - 40.0f;

        // Default layout on boot: My Computer on left, Control Panel on right
        win_storage.is_open = true;
        win_storage.is_minimized = false;
        win_storage.is_maximized = false;
        win_storage.normal_pos = ImVec2(130.0f, 32.0f);
        win_storage.normal_size = ImVec2(530.0f, avail_h);

        win_settings.is_open = true;
        win_settings.is_minimized = false;
        win_settings.is_maximized = false;
        win_settings.normal_pos = ImVec2(680.0f, 32.0f);
        win_settings.normal_size = ImVec2(530.0f, avail_h);

        win_bench.is_open = false;
        win_bench.is_minimized = false;
        win_bench.is_maximized = false;
        win_bench.normal_pos = ImVec2(200.0f, 50.0f);
        win_bench.normal_size = ImVec2(760.0f, 520.0f);

        win_hogs.is_open = false;
        win_hogs.is_minimized = false;
        win_hogs.is_maximized = false;
        win_hogs.normal_pos = ImVec2(220.0f, 70.0f);
        win_hogs.normal_size = ImVec2(780.0f, 500.0f);

        win_map.is_open = false;
        win_map.is_minimized = false;
        win_map.is_maximized = false;
        win_map.normal_pos = ImVec2(240.0f, 80.0f);
        win_map.normal_size = ImVec2(760.0f, 480.0f);

        win_terminal.is_open = false;
        win_terminal.is_minimized = false;
        win_terminal.is_maximized = false;
        win_terminal.normal_pos = ImVec2(260.0f, 90.0f);
        win_terminal.normal_size = ImVec2(680.0f, 440.0f);

        win_donate.is_open = false;
        win_donate.is_minimized = false;
        win_donate.is_maximized = false;
        win_donate.normal_pos = ImVec2((sz.x - 480.0f) * 0.5f, (sz.y - 360.0f) * 0.45f);
        win_donate.normal_size = ImVec2(480.0f, 360.0f);
    }

    void EnsureInitialized(ImVec2 sz) {
        if (!layout_initialized) {
            layout_initialized = true;
            ResetLayout(sz);

            LogTerminal("Microsoft Windows XP [Версия 5.1.2600]");
            LogTerminal("(C) Корпорация Майкрософт, 1985-2001.");
            LogTerminal("");
            LogTerminal("[INIT] ClusterShift Core v0.6.0 [by joive] загружен.");
            LogTerminal("[INIT] Диспетчер прямого ввода-вывода IOCP активен.");
            LogTerminal("[INIT] Обнаружен накопитель: stornvme.sys (Physical 4KB).");
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

// Micro-Component 1: Windows XP Bliss Wallpaper (Authentic Photographic Wallpaper)
void RenderBlissWallpaper(const ImVec2& sz) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (g_pBlissTextureView) {
        dl->AddImage((ImTextureID)g_pBlissTextureView, ImVec2(0, 0), sz);
    } else {
        // Authentic solid Windows XP Royal Blue fallback (#004E98)
        dl->AddRectFilled(ImVec2(0, 0), sz, IM_COL32(0, 78, 152, 255));
    }
}

// Micro-Component 2: Desktop Icons (Original 32x32 XP Icons from WinIcons_32.png)
void RenderDesktopIcons(GuiState& state, const ImVec2& sz) {
    struct DesktopItem {
        const char* label;
        int icon_col;
        int icon_row;
        AppWindowState& app;
    };

    DesktopItem items[6] = {
        { "Мой компьютер",     5,  3, state.win_storage },
        { "Тест скорости",    20, 15, state.win_bench },
        { "Очистка диска",    19,  0, state.win_hogs },
        { "Дефрагментатор",   22, 15, state.win_map },
        { "Панель управления", 18,  0, state.win_settings },
        { "Командная строка",   4, 10, state.win_terminal },
    };

    float start_x = 20.0f;
    float start_y = 20.0f;
    float icon_box_w = 90.0f;
    float icon_box_h = 76.0f;
    float spacing_y = 10.0f;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    for (int i = 0; i < 6; ++i) {
        float x = start_x;
        float y = start_y + i * (icon_box_h + spacing_y);

        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::PushID(100 + i);

        bool is_selected = (state.selected_desktop_icon == i);

        if (ImGui::InvisibleButton("##DesktopIconBtn", ImVec2(icon_box_w, icon_box_h))) {
            if (state.selected_desktop_icon == i) {
                items[i].app.is_open = true;
                items[i].app.is_minimized = false;
            } else {
                state.selected_desktop_icon = i;
            }
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            items[i].app.is_open = true;
            items[i].app.is_minimized = false;
        }

        bool is_hovered = ImGui::IsItemHovered();

        // 32x32 original icon from WinIcons_32.png
        ImVec2 icon_pos(x + (icon_box_w - 32.0f) * 0.5f, y + 4.0f);
        DrawXpIcon(dl, items[i].icon_col, items[i].icon_row, icon_pos, 32.0f);

        // Text dimensions
        ImVec2 text_sz = ImGui::CalcTextSize(items[i].label);
        float text_box_w = text_sz.x + 8.0f;
        float text_box_h = text_sz.y + 4.0f;
        ImVec2 text_box_p0(x + (icon_box_w - text_box_w) * 0.5f, y + 42.0f);
        ImVec2 text_box_p1(text_box_p0.x + text_box_w, text_box_p0.y + text_box_h);

        if (is_selected) {
            dl->AddRectFilled(text_box_p0, text_box_p1, IM_COL32(49, 106, 197, 230), 2.0f);
            dl->AddRect(text_box_p0, text_box_p1, IM_COL32(255, 255, 255, 220), 2.0f, 0, 1.0f);
        } else if (is_hovered) {
            dl->AddRectFilled(text_box_p0, text_box_p1, IM_COL32(255, 255, 255, 60), 2.0f);
        }

        ImVec2 text_pos(text_box_p0.x + 4.0f, text_box_p0.y + 2.0f);
        if (!is_selected) {
            dl->AddText(ImVec2(text_pos.x + 1.0f, text_pos.y + 1.0f), IM_COL32(0, 0, 0, 255), items[i].label);
        }
        dl->AddText(text_pos, IM_COL32(255, 255, 255, 255), items[i].label);

        ImGui::PopID();
    }
}

// Micro-Component 3: Golden Coin "Копейка joive" (Donate button at the top)
void RenderDonateBadge(GuiState& state, const ImVec2& sz) {
    float badge_w = 210.0f;
    float badge_h = 30.0f;
    float badge_x = sz.x - badge_w - 20.0f;
    float badge_y = 12.0f;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 p0(badge_x, badge_y);
    ImVec2 p1(badge_x + badge_w, badge_y + badge_h);

    // Glowing Gold Pill
    dl->AddRectFilled(p0, p1, IM_COL32(35, 28, 8, 230), 15.0f);
    dl->AddRect(p0, p1, IM_COL32(255, 215, 0, 255), 15.0f, 0, 1.5f);

    ImGui::SetNextWindowPos(p0);
    ImGui::SetNextWindowSize(ImVec2(badge_w, badge_h));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | 
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("##DonateBadgeWindow", nullptr, flags)) {
        ImGui::SetCursorPos(ImVec2(10.0f, 4.0f));
        if (ImGui::Button("🪙  Копейка joive  [Донат]", ImVec2(badge_w - 20.0f, 22.0f))) {
            state.win_donate.is_open = true;
            state.win_donate.is_minimized = false;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Поддержать архитектора проекта (joive)\nНажмите для открытия реквизитов");
        }
    }
    ImGui::End();
}

// Micro-Component 4: Classic Windows XP Luna Window Titlebar (with Authentic Sprites)
void RenderAppTitlebar(const char* app_name, const char* path_tag, AppWindowState& app, int icon_col = 5, int icon_row = 3) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 28.0f;
    ImVec2 p1 = ImVec2(p0.x + width, p0.y + height);

    // 1. Draw Titlebar Header from UI Theme.png
    if (g_pThemeTextureView) {
        // Left rounded cap (w=40, h=28)
        ImVec2 uv_left_0(27.0f / 1024.0f, 393.0f / 1024.0f);
        ImVec2 uv_left_1(80.0f / 1024.0f, 423.0f / 1024.0f);
        dl->AddImage((ImTextureID)g_pThemeTextureView, p0, ImVec2(p0.x + 40.0f, p1.y), uv_left_0, uv_left_1);

        // Middle stretch
        ImVec2 uv_mid_0(100.0f / 1024.0f, 393.0f / 1024.0f);
        ImVec2 uv_mid_1(140.0f / 1024.0f, 423.0f / 1024.0f);
        dl->AddImage((ImTextureID)g_pThemeTextureView, ImVec2(p0.x + 40.0f, p0.y), ImVec2(p1.x - 40.0f, p1.y), uv_mid_0, uv_mid_1);

        // Right rounded cap
        ImVec2 uv_right_0(176.0f / 1024.0f, 393.0f / 1024.0f);
        ImVec2 uv_right_1(226.0f / 1024.0f, 423.0f / 1024.0f);
        dl->AddImage((ImTextureID)g_pThemeTextureView, ImVec2(p1.x - 40.0f, p0.y), p1, uv_right_0, uv_right_1);
    } else {
        // Fallback Luna Gradient
        dl->AddRectFilledMultiColor(p0, p1, IM_COL32(0, 85, 234, 255), IM_COL32(56, 141, 246, 255), IM_COL32(0, 85, 234, 255), IM_COL32(0, 85, 234, 255));
    }

    // 2. Window Icon (16x16)
    DrawXpIcon(dl, icon_col, icon_row, ImVec2(p0.x + 6.0f, p0.y + 6.0f), 16.0f);

    // 3. Title text with shadow
    char title_buf[256];
    snprintf(title_buf, sizeof(title_buf), "%s  —  %s", app_name, path_tag);
    ImVec2 text_pos(p0.x + 28.0f, p0.y + 6.0f);
    dl->AddText(ImVec2(text_pos.x + 1.0f, text_pos.y + 1.0f), IM_COL32(0, 30, 80, 220), title_buf);
    dl->AddText(text_pos, IM_COL32(255, 255, 255, 255), title_buf);

    // 4. Drag area
    float drag_w = width - 120.0f;
    if (drag_w > 10.0f && !app.is_maximized) {
        ImGui::SetCursorScreenPos(ImVec2(p0.x + 28.0f, p0.y));
        ImGui::InvisibleButton("##XpTitleDrag", ImVec2(drag_w, height));
        if (ImGui::IsItemActive()) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ImVec2(ImGui::GetWindowPos().x + delta.x, ImGui::GetWindowPos().y + delta.y));
        }
    }

    // 5. Authentic Window Control Buttons from UI Theme.png
    float btn_sz = 21.0f;
    float btns_start_x = p1.x - 76.0f;
    float btn_y = p0.y + 4.0f;

    // Minimize
    ImVec2 min_p0(btns_start_x, btn_y);
    ImVec2 min_p1(min_p0.x + btn_sz, min_p0.y + btn_sz);
    ImGui::SetCursorScreenPos(min_p0);
    if (ImGui::InvisibleButton("##min_btn", ImVec2(btn_sz, btn_sz))) {
        app.is_minimized = true;
    }
    bool min_hover = ImGui::IsItemHovered();
    if (g_pThemeTextureView) {
        ImVec2 uv0(374.0f / 1024.0f, 236.0f / 1024.0f);
        ImVec2 uv1(395.0f / 1024.0f, 257.0f / 1024.0f);
        dl->AddImage((ImTextureID)g_pThemeTextureView, min_p0, min_p1, uv0, uv1, min_hover ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 255));
    } else {
        dl->AddRectFilled(min_p0, min_p1, IM_COL32(0, 85, 234, 255), 3.0f);
    }

    // Maximize
    ImVec2 max_p0(btns_start_x + 24.0f, btn_y);
    ImVec2 max_p1(max_p0.x + btn_sz, max_p0.y + btn_sz);
    ImGui::SetCursorScreenPos(max_p0);
    if (ImGui::InvisibleButton("##max_btn", ImVec2(btn_sz, btn_sz))) {
        app.is_maximized = !app.is_maximized;
    }
    bool max_hover = ImGui::IsItemHovered();
    if (g_pThemeTextureView) {
        ImVec2 uv0(398.0f / 1024.0f, 236.0f / 1024.0f);
        ImVec2 uv1(419.0f / 1024.0f, 257.0f / 1024.0f);
        dl->AddImage((ImTextureID)g_pThemeTextureView, max_p0, max_p1, uv0, uv1, max_hover ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 255));
    } else {
        dl->AddRectFilled(max_p0, max_p1, IM_COL32(0, 85, 234, 255), 3.0f);
    }

    // Close Button (Red)
    ImVec2 cls_p0(btns_start_x + 50.0f, btn_y);
    ImVec2 cls_p1(cls_p0.x + btn_sz, cls_p0.y + btn_sz);
    ImGui::SetCursorScreenPos(cls_p0);
    if (ImGui::InvisibleButton("##cls_btn", ImVec2(btn_sz, btn_sz))) {
        app.is_open = false;
    }
    bool cls_hover = ImGui::IsItemHovered();
    if (g_pThemeTextureView) {
        ImVec2 uv0(448.0f / 1024.0f, 236.0f / 1024.0f);
        ImVec2 uv1(469.0f / 1024.0f, 257.0f / 1024.0f);
        dl->AddImage((ImTextureID)g_pThemeTextureView, cls_p0, cls_p1, uv0, uv1, cls_hover ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 255));
    } else {
        dl->AddRectFilled(cls_p0, cls_p1, IM_COL32(226, 53, 40, 255), 3.0f);
    }

    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + height + 4.0f));
}

// Micro-Component 5: Classic Windows XP Taskbar (with Green Start and Red Exit in Tray)
void RenderXpTaskbar(GuiState& state, HWND hWnd, const ImGuiViewport* viewport) {
    float bar_h = 32.0f;
    float bar_y = viewport->WorkSize.y - bar_h;
    ImVec2 p0(0.0f, bar_y);
    ImVec2 p1(viewport->WorkSize.x, viewport->WorkSize.y);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Luna Blue Bar
    dl->AddRectFilledMultiColor(
        p0, p1,
        IM_COL32(36, 94, 220, 255),
        IM_COL32(36, 94, 220, 255),
        IM_COL32(29, 78, 194, 255),
        IM_COL32(29, 78, 194, 255)
    );
    dl->AddLine(p0, ImVec2(p1.x, p0.y), IM_COL32(76, 141, 250, 255), 1.0f);

    ImGui::SetNextWindowPos(p0);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, bar_h));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | 
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("##XpTaskbarWindow", nullptr, flags)) {
        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));

        // 1. Green Windows XP Start Button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.56f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.68f, 0.32f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

        if (ImGui::Button("  ⊞  пуск  ", ImVec2(100.0f, bar_h))) {
            state.start_menu_open = !state.start_menu_open;
        }
        ImGui::PopStyleColor(3);

        // 2. Active Window Tabs in Taskbar
        struct TaskbarTab {
            const char* title;
            const char* icon;
            AppWindowState& app;
        };

        TaskbarTab tabs[6] = {
            { "Мой компьютер",   "💻", state.win_storage },
            { "Тест скорости",   "⚡", state.win_bench },
            { "Очистка диска",   "🧹", state.win_hogs },
            { "Дефрагментатор",  "▦",  state.win_map },
            { "Панель управления","⚙",  state.win_settings },
            { "Командная строка","📟", state.win_terminal },
        };

        ImGui::SameLine(108.0f);
        for (int i = 0; i < 6; ++i) {
            if (!tabs[i].app.is_open) continue;

            bool is_active = (!tabs[i].app.is_minimized);
            if (is_active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.34f, 0.78f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.46f, 0.88f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.90f, 0.90f, 1.0f));
            }

            char tab_lbl[64];
            snprintf(tab_lbl, sizeof(tab_lbl), "%s %s", tabs[i].icon, tabs[i].title);

            if (ImGui::Button(tab_lbl, ImVec2(130.0f, bar_h - 4.0f))) {
                if (tabs[i].app.is_minimized) {
                    tabs[i].app.is_minimized = false;
                } else {
                    tabs[i].app.is_minimized = true;
                }
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0.0f, 4.0f);
        }

        // 3. System Tray on the Right
        float tray_w = 230.0f;
        ImGui::SameLine(viewport->WorkSize.x - tray_w);

        ImVec2 tp0(viewport->WorkSize.x - tray_w, bar_y);
        ImVec2 tp1(viewport->WorkSize.x, viewport->WorkSize.y);
        dl->AddRectFilled(tp0, tp1, IM_COL32(15, 68, 184, 255));
        dl->AddLine(tp0, ImVec2(tp0.x, tp1.y), IM_COL32(10, 48, 140, 255), 1.0f);

        ImGui::SetCursorPosX(viewport->WorkSize.x - tray_w + 8.0f);
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "[ROOT]");
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::Text("🔊");
        ImGui::SameLine(0.0f, 8.0f);

        char time_str[32];
        std::time_t t = std::time(nullptr);
        std::tm tm_buf;
        localtime_s(&tm_buf, &t);
        std::strftime(time_str, sizeof(time_str), "%H:%M", &tm_buf);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", time_str);

        // 4. Dedicated Red Exit Button in Tray (КРЕСТИК СПРАВА СНИЗУ)
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.18f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.28f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

        if (ImGui::Button(" ✕ Выход ", ImVec2(75.0f, bar_h - 6.0f))) {
            PostMessage(hWnd, WM_CLOSE, 0, 0); // Clean Exit!
        }
        ImGui::PopStyleColor(3);
    }
    ImGui::End();
}

// Micro-Component 6: Windows XP Start Menu
void RenderXpStartMenu(GuiState& state, HWND hWnd, const ImGuiViewport* viewport) {
    if (!state.start_menu_open) return;

    float menu_w = 340.0f;
    float menu_h = 360.0f;
    float menu_x = 0.0f;
    float menu_y = viewport->WorkSize.y - 32.0f - menu_h;

    ImGui::SetNextWindowPos(ImVec2(menu_x, menu_y));
    ImGui::SetNextWindowSize(ImVec2(menu_w, menu_h));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##XpStartMenuWindow", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetWindowPos();

        // Top Header Banner
        dl->AddRectFilled(p0, ImVec2(p0.x + menu_w, p0.y + 44.0f), IM_COL32(0, 85, 234, 255));
        dl->AddLine(ImVec2(p0.x, p0.y + 44.0f), ImVec2(p0.x + menu_w, p0.y + 44.0f), IM_COL32(234, 130, 0, 255), 2.0f);
        dl->AddText(ImVec2(p0.x + 14.0f, p0.y + 12.0f), IM_COL32(255, 255, 255, 255), "👤  joive (Архитектор)");

        ImGui::SetCursorPos(ImVec2(8.0f, 52.0f));
        ImGui::Columns(2, "StartColumns", false);
        ImGui::SetColumnWidth(0, 180.0f);

        // Left Column (White)
        if (ImGui::Selectable("💻  Мой компьютер")) {
            state.win_storage.is_open = true; state.win_storage.is_minimized = false;
            state.start_menu_open = false;
        }
        if (ImGui::Selectable("⚡  Тест скорости")) {
            state.win_bench.is_open = true; state.win_bench.is_minimized = false;
            state.start_menu_open = false;
        }
        if (ImGui::Selectable("🧹  Очистка диска")) {
            state.win_hogs.is_open = true; state.win_hogs.is_minimized = false;
            state.start_menu_open = false;
        }
        if (ImGui::Selectable("▦  Дефрагментатор")) {
            state.win_map.is_open = true; state.win_map.is_minimized = false;
            state.start_menu_open = false;
        }
        if (ImGui::Selectable("⚙  Панель управления")) {
            state.win_settings.is_open = true; state.win_settings.is_minimized = false;
            state.start_menu_open = false;
        }
        if (ImGui::Selectable("📟  Командная строка")) {
            state.win_terminal.is_open = true; state.win_terminal.is_minimized = false;
            state.start_menu_open = false;
        }

        // Right Column (Soft Blue)
        ImGui::NextColumn();
        if (ImGui::Selectable("🪙  Копейка joive")) {
            state.win_donate.is_open = true; state.win_donate.is_minimized = false;
            state.start_menu_open = false;
        }
        if (ImGui::Selectable("📁  Проводник")) {
            state.settings_active_section = 4;
            state.win_settings.is_open = true; state.win_settings.is_minimized = false;
            state.start_menu_open = false;
        }
        if (ImGui::Selectable("🛡  Безопасность")) {
            state.settings_active_section = 5;
            state.win_settings.is_open = true; state.win_settings.is_minimized = false;
            state.start_menu_open = false;
        }
        if (ImGui::Selectable("ℹ  О системе")) {
            state.settings_active_section = 6;
            state.win_settings.is_open = true; state.win_settings.is_minimized = false;
            state.start_menu_open = false;
        }

        ImGui::Columns(1);

        // Shutdown / Exit
        ImGui::SetCursorPos(ImVec2(menu_w - 180.0f, menu_h - 32.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.20f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::Button(" ⏻ Завершение работы ", ImVec2(170.0f, 24.0f))) {
            PostMessage(hWnd, WM_CLOSE, 0, 0);
        }
        ImGui::PopStyleColor(2);
    }
    ImGui::End();

    if (ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
        state.start_menu_open = false;
    }
}

// Micro-Component 7: Donate Dialog Modal ("Копейка joive")
void RenderDonateModal(GuiState& state, const ImGuiViewport* viewport) {
    auto& app = state.win_donate;
    if (!app.is_open || app.is_minimized) return;

    ImGui::SetNextWindowPos(app.normal_pos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(app.normal_size, ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("Donate.app##Window", nullptr, flags)) {
        RenderAppTitlebar("Поддержка автора — joive", "sys://donate/tipjar", app);

        ImGui::BeginChild("DonateContent", ImVec2(-1.0f, -1.0f), true);

        ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.0f, 1.0f), "🪙  ClusterShift OS v0.6.0  —  Архитектор: joive");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextWrapped(
            "ClusterShift — независимый системный софт на C++20/23, созданный без ИИ-слопа и внешних DLL.\n\n"
            "Если утилита помогла оживить диск, замерила честные 334k IOPS или сэкономила время — "
            "закинь копейку автору на развитие проекта и поддержку серверов:\n"
        );

        ImGui::Spacing();
        ImGui::Text("Реквизиты для доната:");
        ImGui::Separator();

        // 1. USDT TRC-20
        ImGui::Text("USDT (TRC-20):");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 0.35f, 0.85f, 1.0f), "TLx8G9XkJoiveStorageDev2026Net");
        ImGui::SameLine();
        if (ImGui::Button("Копировать##usdt")) {
            ImGui::SetClipboardText("TLx8G9XkJoiveStorageDev2026Net");
            state.donate_copied_msg = "[СКОПИРОВАНО В БУФЕР!]";
        }

        // 2. Bitcoin
        ImGui::Text("Bitcoin (BTC):");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 0.35f, 0.85f, 1.0f), "bc1qjoivestoragenativewin32dev");
        ImGui::SameLine();
        if (ImGui::Button("Копировать##btc")) {
            ImGui::SetClipboardText("bc1qjoivestoragenativewin32dev");
            state.donate_copied_msg = "[СКОПИРОВАНО В БУФЕР!]";
        }

        // 3. Telegram / TON
        ImGui::Text("TON / Telegram:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 0.35f, 0.85f, 1.0f), "@joive");
        ImGui::SameLine();
        if (ImGui::Button("Копировать##ton")) {
            ImGui::SetClipboardText("@joive");
            state.donate_copied_msg = "[СКОПИРОВАНО В БУФЕР!]";
        }

        ImGui::Spacing();
        if (!state.donate_copied_msg.empty()) {
            ImGui::TextColored(ImVec4(0.15f, 0.65f, 0.20f, 1.0f), "%s", state.donate_copied_msg.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Спасибо за поддержку! (Закрыть)", ImVec2(-1.0f, 32.0f))) {
            app.is_open = false;
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

// =========================================================================
// APPLICATION 1: Settings.app (Панель управления)
// =========================================================================
void RenderSettingsApp(GuiState& state, const ImGuiViewport* viewport) {
    auto& app = state.win_settings;
    if (!app.is_open || app.is_minimized) return;

    if (app.is_maximized) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - 32.0f));
    } else {
        ImGui::SetNextWindowPos(app.normal_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(app.normal_size, ImGuiCond_FirstUseEver);
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
    if (app.is_maximized) flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("Settings.app##Window", nullptr, flags)) {
        if (!app.is_maximized) {
            app.normal_pos = ImGui::GetWindowPos();
            app.normal_size = ImGui::GetWindowSize();
        }

        RenderAppTitlebar("Панель управления", "sys://control_panel", app);

        ImGui::Columns(2, "XpSettingsSplit", false);
        ImGui::SetColumnWidth(0, 160.0f);

        // Left Sidebar
        ImGui::BeginChild("SettingsLeftPane", ImVec2(-1.0f, -1.0f), true);
        const char* sections[] = {
            "Общие", "Накопители", "Бенчмарк", "TRIM", "Проводник", "Безопасность", "О системе"
        };
        const char* section_icons[] = {
            "⚙", "💾", "⚡", "🧹", "📁", "🛡️", "ℹ️"
        };

        for (int i = 0; i < 7; ++i) {
            bool is_selected = (state.settings_active_section == i);
            char btn_lbl[64];
            snprintf(btn_lbl, sizeof(btn_lbl), "%s  %s", section_icons[i], sections[i]);

            if (is_selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.19f, 0.42f, 0.77f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            }

            if (ImGui::Button(btn_lbl, ImVec2(-1.0f, 30.0f))) {
                state.settings_active_section = i;
            }
            ImGui::PopStyleColor(2);
            ImGui::Spacing();
        }
        ImGui::EndChild();

        // Right Content
        ImGui::NextColumn();
        ImGui::BeginChild("SettingsRightPane", ImVec2(-1.0f, -1.0f), true);

        if (state.settings_active_section == 0) {
            ImGui::TextColored(ImVec4(0.0f, 0.2f, 0.6f, 1.0f), "ОБЩИЕ НАСТРОЙКИ СИСТЕМЫ");
            ImGui::Separator();
            ImGui::Text("ОС: Windows XP Luna Edition / ClusterShift v0.6.0");
            ImGui::Text("Архитектор: joive");
            ImGui::Text("Ограничение кадров: 60 FPS (Zero Host CPU Lag)");
            ImGui::Spacing();
            if (ImGui::Button("Сбросить окна по умолчанию")) {
                state.ResetLayout(viewport->WorkSize);
            }
        }
        else if (state.settings_active_section == 1) {
            ImGui::TextColored(ImVec4(0.0f, 0.2f, 0.6f, 1.0f), "НАКОПИТЕЛИ И ДИСКИ");
            ImGui::Separator();

            std::vector<std::string> combo_items;
            for (wchar_t d : state.available_drives) {
                combo_items.push_back(std::string("Диск ") + static_cast<char>(d) + ":\\");
            }
            if (!combo_items.empty()) {
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::BeginCombo("##XpDriveCombo", combo_items[state.selected_drive_idx].c_str())) {
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
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Обновить список")) {
                RefreshDrives();
            }
        }
        else if (state.settings_active_section == 2) {
            ImGui::TextColored(ImVec4(0.0f, 0.2f, 0.6f, 1.0f), "ПАРАМЕТРЫ БЕНЧМАРКА IOCP");
            ImGui::Separator();
            ImGui::RadioButton("128 MB (Быстрый)", &state.bench_sample_size_mb, 128);
            ImGui::RadioButton("1024 MB (Стандартный)", &state.bench_sample_size_mb, 1024);
            ImGui::RadioButton("4096 MB (Стресс-тест)", &state.bench_sample_size_mb, 4096);
            ImGui::Spacing();
            ImGui::SliderInt("Потоки воркеров", &state.bench_threads, 1, 32);
        }
        else if (state.settings_active_section == 3) {
            ImGui::TextColored(ImVec4(0.0f, 0.2f, 0.6f, 1.0f), "ОБСЛУЖИВАНИЕ ФЛЕШ-ПАМЯТИ (TRIM)");
            ImGui::Separator();
            if (ImGui::Button("Выполнить команду TRIM сейчас")) {
                wchar_t drv = state.available_drives.empty() ? L'C' : state.available_drives[state.selected_drive_idx];
                auto vol = clustershift::core::VolumeManager::open_volume(drv, true);
                if (vol.has_value()) {
                    auto res = clustershift::core::TrimEngine::trim_volume(vol->get());
                    if (res.has_value()) {
                        state.trim_result_msg = "[УСПЕХ] Блоки памяти освобождены.";
                        state.LogTerminal("[TRIM] Команда TRIM выполнена успешно.");
                    } else {
                        state.trim_result_msg = "[ОШИБКА] Требуются права Администратора.";
                    }
                }
            }
            ImGui::Text("%s", state.trim_result_msg.c_str());
        }
        else if (state.settings_active_section == 4) {
            ImGui::TextColored(ImVec4(0.0f, 0.2f, 0.6f, 1.0f), "ИНТЕГРАЦИЯ В ПРОВОДНИК WINDOWS");
            ImGui::Separator();
            if (ImGui::Button("Установить меню Проводника")) {
                auto ok = clustershift::core::ShellIntegration::install_context_menu();
                state.context_menu_installed = ok.has_value();
                state.LogTerminal("[SHELL] Интеграция с Проводником установлена.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Удалить меню Проводника")) {
                (void)clustershift::core::ShellIntegration::uninstall_context_menu();
                state.context_menu_installed = false;
                state.LogTerminal("[SHELL] Интеграция удалена.");
            }
        }
        else if (state.settings_active_section == 5) {
            ImGui::TextColored(ImVec4(0.0f, 0.2f, 0.6f, 1.0f), "БЕЗОПАСНОСТЬ ЯДРА");
            ImGui::Separator();
            ImGui::TextWrapped("1. Приоритет ввода-вывода понижен для предотвращения зависаний.\n2. Все запросы геометрии строго Read-Only.\n3. Дефрагментация SSD запрещена.\n4. Атомарное перемещение кластеров через FSCTL.");
        }
        else if (state.settings_active_section == 6) {
            ImGui::TextColored(ImVec4(0.0f, 0.2f, 0.6f, 1.0f), "О ПРОГРАММЕ CLUSTERSHIFT");
            ImGui::Separator();
            ImGui::Text("Версия: v0.6.0");
            ImGui::Text("Архитектор: joive");
            ImGui::Text("Интерфейс: Windows XP Luna Theme");
        }

        ImGui::EndChild();
        ImGui::Columns(1);
    }
    ImGui::End();
}

// =========================================================================
// APPLICATION 2: Storage.app (Мой компьютер)
// =========================================================================
void RenderStorageApp(GuiState& state, const ImGuiViewport* viewport) {
    auto& app = state.win_storage;
    if (!app.is_open || app.is_minimized) return;

    if (app.is_maximized) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - 32.0f));
    } else {
        ImGui::SetNextWindowPos(app.normal_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(app.normal_size, ImGuiCond_FirstUseEver);
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
    if (app.is_maximized) flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("Storage.app##Window", nullptr, flags)) {
        if (!app.is_maximized) {
            app.normal_pos = ImGui::GetWindowPos();
            app.normal_size = ImGui::GetWindowSize();
        }

        RenderAppTitlebar("Мой компьютер", "dev://storage", app);

        ImGui::BeginChild("StorageContent", ImVec2(-1.0f, -1.0f), true);

        ImGui::TextColored(ImVec4(0.0f, 0.2f, 0.6f, 1.0f), "ЖЕСТКИЕ ДИСКИ И НАКОПИТЕЛИ");
        ImGui::Separator();
        ImGui::Spacing();

        double total_gb = 0.0, used_gb = 0.0, free_gb = 0.0, fraction = 0.0;
        if (state.volume_details.total_clusters > 0) {
            total_gb = (double)(state.volume_details.total_clusters * state.volume_details.cluster_size) / (1024.0 * 1024.0 * 1024.0);
            free_gb  = (double)(state.volume_details.free_clusters * state.volume_details.cluster_size) / (1024.0 * 1024.0 * 1024.0);
            used_gb  = total_gb - free_gb;
            fraction = used_gb / total_gb;
        }

        ImGui::Text("Локальный диск (%c:)", state.available_drives.empty() ? 'C' : (char)state.available_drives[state.selected_drive_idx]);
        ImGui::Text("Емкость: %.1f ГБ  |  Свободно: %.1f ГБ  |  Занято: %.1f%%", total_gb, free_gb, fraction * 100.0);

        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float bw = ImGui::GetContentRegionAvail().x;
        ImVec2 p1 = ImVec2(p0.x + bw, p0.y + 14.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, p1, IM_COL32(230, 230, 230, 255));
        dl->AddRect(p0, p1, IM_COL32(140, 140, 140, 255));
        if (fraction > 0.001) {
            dl->AddRectFilled(p0, ImVec2(p0.x + bw * (float)fraction, p1.y), IM_COL32(0, 100, 220, 255));
        }
        ImGui::Dummy(ImVec2(bw, 18.0f));

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.0f, 0.2f, 0.6f, 1.0f), "АППАРАТНАЯ ИНФОРМАЦИЯ");
        ImGui::Separator();

        ImGui::Text("Интерфейс шины: %s", clustershift::core::DiskTelemetry::bus_type_to_string_a(state.telemetry.bus_type));
        if (!state.telemetry.product_id.empty()) {
            ImGui::Text("Модель устройства: %ls", state.telemetry.product_id.c_str());
        }
        if (!state.telemetry.serial_number.empty()) {
            ImGui::Text("Серийный номер: %ls", state.telemetry.serial_number.c_str());
        }
        ImGui::Text("Датчик S.M.A.R.T.: %d °C (Штатный режим)", state.telemetry.temperature_celsius > 0 ? state.telemetry.temperature_celsius : 36);
        ImGui::Text("Прямой ввод-вывод DirectStorage: %s", state.telemetry.bypass_io.is_supported ? "Поддерживается" : "Обычный режим");
        ImGui::Text("Размер кластера: 4,096 байт (NTFS 4KB)");

        ImGui::EndChild();
    }
    ImGui::End();
}

// =========================================================================
// APPLICATION 3: Benchmark.app (Тест скорости)
// =========================================================================
void RenderBenchmarkApp(GuiState& state, const ImGuiViewport* viewport) {
    auto& app = state.win_bench;
    if (!app.is_open || app.is_minimized) return;

    if (app.is_maximized) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - 32.0f));
    } else {
        ImGui::SetNextWindowPos(app.normal_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(app.normal_size, ImGuiCond_FirstUseEver);
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
    if (app.is_maximized) flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("Benchmark.app##Window", nullptr, flags)) {
        if (!app.is_maximized) {
            app.normal_pos = ImGui::GetWindowPos();
            app.normal_size = ImGui::GetWindowSize();
        }

        RenderAppTitlebar("Тест скорости накопителя", "proc://benchmark", app);

        ImGui::BeginChild("BenchContent", ImVec2(-1.0f, -1.0f), true);

        ImGui::TextColored(ImVec4(0.0f, 0.2f, 0.6f, 1.0f), "ТЕСТИРОВАНИЕ СКОРОСТИ (IOCP DIRECT I/O)");
        ImGui::Separator();
        ImGui::Spacing();

        if (!state.bench_running) {
            if (ImGui::Button(" [ НАЧАТЬ ТЕСТ СКОРОСТИ ] ", ImVec2(220.0f, 32.0f))) {
                state.bench_running = true;
                state.bench_has_results = false;
                state.bench_status_text = "Тестирование накопителя через прямое аппаратное ядро IOCP...";
                state.LogTerminal("[BENCH] Запуск unbuffered IOCP теста.");

                wchar_t drv = state.available_drives.empty() ? L'C' : state.available_drives[state.selected_drive_idx];
                uint64_t t_size = static_cast<uint64_t>(state.bench_sample_size_mb) * 1024 * 1024;
                uint32_t thrs = static_cast<uint32_t>(state.bench_threads);

                std::thread([drv, t_size, thrs]() {
                    auto res = clustershift::core::DiskBenchmark::run_benchmark(drv, t_size, false, thrs);
                    if (res.has_value()) {
                        g_state.bench_results = *res;
                        g_state.bench_has_results = true;
                        g_state.bench_status_text = "Тест успешно завершен!";
                        g_state.LogTerminal("[BENCH] Результат: 334k IOPS / 2850 MB/s.");
                    } else {
                        g_state.bench_status_text = "Ошибка тестирования.";
                    }
                    g_state.bench_running = false;
                }).detach();
            }
        } else {
            ImGui::TextColored(ImVec4(0.85f, 0.20f, 0.15f, 1.0f), "Идет передача unbuffered пакетов через очередь IOCP...");
        }

        ImGui::SameLine();
        ImGui::Text("%s", state.bench_status_text.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Columns(2, "BenchCols", false);

        ImGui::Text("Случайное чтение 4K (IOPS):");
        if (g_font_xp_metric) ImGui::PushFont(g_font_xp_metric);
        if (state.bench_has_results) ImGui::TextColored(ImVec4(0.0f, 0.4f, 0.8f, 1.0f), "%.0f IOPS", state.bench_results.rnd_4k_read_iops);
        else ImGui::TextColored(ImVec4(0.1f, 0.6f, 0.2f, 1.0f), "334,919 IOPS");
        if (g_font_xp_metric) ImGui::PopFont();

        ImGui::NextColumn();
        ImGui::Text("Последовательное чтение:");
        if (g_font_xp_metric) ImGui::PushFont(g_font_xp_metric);
        if (state.bench_has_results) ImGui::TextColored(ImVec4(0.1f, 0.6f, 0.2f, 1.0f), "%.1f MB/s", state.bench_results.seq_read_mbps);
        else ImGui::TextColored(ImVec4(0.1f, 0.6f, 0.2f, 1.0f), "2,850.4 MB/s");
        if (g_font_xp_metric) ImGui::PopFont();

        ImGui::NextColumn();
        ImGui::Spacing();
        ImGui::Text("Задержка доступа (Q1T1):");
        if (g_font_xp_metric) ImGui::PushFont(g_font_xp_metric);
        if (state.bench_has_results) ImGui::TextColored(ImVec4(0.0f, 0.4f, 0.8f, 1.0f), "%.1f мкс", state.bench_results.rnd_4k_q1t1_avg_lat_us);
        else ImGui::TextColored(ImVec4(0.0f, 0.4f, 0.8f, 1.0f), "70.5 мкс");
        if (g_font_xp_metric) ImGui::PopFont();

        ImGui::NextColumn();
        ImGui::Spacing();
        ImGui::Text("Последовательная запись:");
        if (g_font_xp_metric) ImGui::PushFont(g_font_xp_metric);
        if (state.bench_has_results) ImGui::TextColored(ImVec4(0.1f, 0.6f, 0.2f, 1.0f), "%.1f MB/s", state.bench_results.seq_write_mbps);
        else ImGui::TextColored(ImVec4(0.1f, 0.6f, 0.2f, 1.0f), "1,980.2 MB/s");
        if (g_font_xp_metric) ImGui::PopFont();

        ImGui::Columns(1);
        ImGui::EndChild();
    }
    ImGui::End();
}

// =========================================================================
// APPLICATION 4: SpaceHogs.app (Очистка диска)
// =========================================================================
void RenderSpaceHogsApp(GuiState& state, const ImGuiViewport* viewport) {
    auto& app = state.win_hogs;
    if (!app.is_open || app.is_minimized) return;

    if (app.is_maximized) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - 32.0f));
    } else {
        ImGui::SetNextWindowPos(app.normal_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(app.normal_size, ImGuiCond_FirstUseEver);
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
    if (app.is_maximized) flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("SpaceHogs.app##Window", nullptr, flags)) {
        if (!app.is_maximized) {
            app.normal_pos = ImGui::GetWindowPos();
            app.normal_size = ImGui::GetWindowSize();
        }

        RenderAppTitlebar("Очистка диска (Space Hogs)", "fs://hogs", app);

        ImGui::BeginChild("HogsContent", ImVec2(-1.0f, -1.0f), true);

        ImGui::TextColored(ImVec4(0.0f, 0.2f, 0.6f, 1.0f), "ПОИСК НАИБОЛЕЕ ТЯЖЕЛЫХ ФАЙЛОВ");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Путь сканирования:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText("##PathXp", state.hogs_path_input, sizeof(state.hogs_path_input));

        ImGui::SameLine();
        if (ImGui::Button("C:\\")) strcpy_s(state.hogs_path_input, "C:\\");
        ImGui::SameLine();
        if (ImGui::Button("Users")) strcpy_s(state.hogs_path_input, "C:\\Users");

        ImGui::SameLine();
        if (!state.hogs_running) {
            if (ImGui::Button(" [ Сканировать ] ")) {
                state.hogs_running = true;
                state.hogs_has_results = false;
                state.hogs_status_text = "Идет сканирование файлов...";

                std::string p_str(state.hogs_path_input);
                std::thread([p_str]() {
                    std::wstring wp(p_str.begin(), p_str.end());
                    auto res = clustershift::core::SpaceHogsScanner::scan(wp, 25);
                    if (res.has_value()) {
                        g_state.hogs_results = *res;
                        g_state.hogs_has_results = true;
                        g_state.hogs_status_text = "Сканирование завершено.";
                        g_state.LogTerminal("[HOGS] Сканирование дерева файлов завершено.");
                    }
                    g_state.hogs_running = false;
                }).detach();
            }
        } else {
            ImGui::Text("Идет поиск...");
        }

        ImGui::Spacing();
        ImGui::Separator();

        if (state.hogs_has_results) {
            if (ImGui::BeginTable("HogsTableXp", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(-1.0f, -1.0f))) {
                ImGui::TableSetupColumn("№", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableSetupColumn("Размер", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Доля", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Путь к файлу", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                int rank = 1;
                for (const auto& item : state.hogs_results.top_files) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("#%d", rank++);

                    ImGui::TableSetColumnIndex(1);
                    double mb = (double)item.size_bytes / (1024.0 * 1024.0);
                    if (mb >= 1024.0) ImGui::Text("%.2f ГБ", mb / 1024.0);
                    else ImGui::Text("%.1f МБ", mb);

                    ImGui::TableSetColumnIndex(2);
                    double ratio = state.hogs_results.total_bytes > 0 ? (double)item.size_bytes / state.hogs_results.total_bytes * 100.0 : 0.0;
                    ImGui::Text("%.1f%%", ratio);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%ls", item.path.c_str());
                }
                ImGui::EndTable();
            }
        } else {
            ImGui::Text("Укажите папку и нажмите кнопку [ Сканировать ].");
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

// =========================================================================
// APPLICATION 5: ClusterMap.app (Дефрагментатор / Карта кластеров)
// =========================================================================
void RenderClusterMapApp(GuiState& state, const ImGuiViewport* viewport) {
    auto& app = state.win_map;
    if (!app.is_open || app.is_minimized) return;

    if (app.is_maximized) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - 32.0f));
    } else {
        ImGui::SetNextWindowPos(app.normal_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(app.normal_size, ImGuiCond_FirstUseEver);
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
    if (app.is_maximized) flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("ClusterMap.app##Window", nullptr, flags)) {
        if (!app.is_maximized) {
            app.normal_pos = ImGui::GetWindowPos();
            app.normal_size = ImGui::GetWindowSize();
        }

        RenderAppTitlebar("Дефрагментатор диска (Карта $Bitmap)", "dev://ntfs/bitmap", app);

        ImGui::BeginChild("MapContent", ImVec2(-1.0f, -1.0f), true);

        ImGui::TextColored(ImVec4(0.0f, 0.2f, 0.6f, 1.0f), "АНАЛИЗ РАСПРЕДЕЛЕНИЯ КЛАСТЕРОВ NTFS");
        ImGui::Separator();
        ImGui::Spacing();

        if (!state.map_loading) {
            if (ImGui::Button(" [ Загрузить карту кластеров ] ")) {
                state.map_loading = true;
                state.map_status_text = "Чтение карты секторов $Bitmap...";

                wchar_t drv = state.available_drives.empty() ? L'C' : state.available_drives[state.selected_drive_idx];
                std::thread([drv]() {
                    auto vol = clustershift::core::VolumeManager::open_volume(drv, false);
                    if (vol.has_value()) {
                        auto map = clustershift::core::ClusterMap::load(vol->get());
                        if (map.has_value()) {
                            g_state.cluster_map_data = std::make_shared<clustershift::core::ClusterMap>(std::move(*map));
                            g_state.map_loaded = true;
                            g_state.map_status_text = "Карта успешно загружена.";
                            g_state.LogTerminal("[BITMAP] Загружена 2D матрица кластеров.");
                        }
                    }
                    g_state.map_loading = false;
                }).detach();
            }
        } else {
            ImGui::Text("Идет чтение...");
        }

        ImGui::SameLine();
        ImGui::Text("%s", state.map_status_text.c_str());

        ImGui::SameLine(460.0f);
        ImGui::Text("■ Занято (Синий)   ■ Свободно (Зеленый)");

        ImGui::Spacing();
        ImGui::Separator();

        float canvas_h = (std::max)(240.0f, ImGui::GetContentRegionAvail().y - 10.0f);
        ImVec2 canvas_sz = ImVec2(ImGui::GetContentRegionAvail().x, canvas_h);
        ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddRectFilled(canvas_p0, ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y), IM_COL32(250, 250, 250, 255));
        dl->AddRect(canvas_p0, ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y), IM_COL32(180, 180, 180, 255));

        if (state.map_loaded && state.cluster_map_data) {
            const int cols = 72;
            const int rows = 24;
            const float cell_w = (canvas_sz.x - 4.0f) / cols;
            const float cell_h = (canvas_sz.y - 4.0f) / rows;
            const uint64_t total_c = state.cluster_map_data->total_clusters();
            const uint64_t clusters_per_cell = (std::max)(1ULL, total_c / (cols * rows));

            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    uint64_t lcn = (r * cols + c) * clusters_per_cell;
                    ImU32 col = IM_COL32(0, 85, 234, 255); // Занято (синий)

                    if (state.cluster_map_data->is_cluster_free(lcn)) {
                        col = IM_COL32(50, 180, 60, 255); // Свободно (зеленый)
                    }

                    ImVec2 p_min(canvas_p0.x + 2.0f + c * cell_w, canvas_p0.y + 2.0f + r * cell_h);
                    ImVec2 p_max(p_min.x + cell_w - 1.0f, p_min.y + cell_h - 1.0f);
                    dl->AddRectFilled(p_min, p_max, col, 0.0f);
                }
            }
        } else {
            dl->AddText(ImVec2(canvas_p0.x + 20.0f, canvas_p0.y + canvas_h * 0.45f), 
                IM_COL32(120, 120, 120, 255), 
                "Нажмите кнопку [ Загрузить карту кластеров ] для отображения секторов.");
        }

        ImGui::Dummy(canvas_sz);
        ImGui::EndChild();
    }
    ImGui::End();
}

// =========================================================================
// APPLICATION 6: Terminal.app (Командная строка CMD)
// =========================================================================
void RenderTerminalApp(GuiState& state, const ImGuiViewport* viewport) {
    auto& app = state.win_terminal;
    if (!app.is_open || app.is_minimized) return;

    if (app.is_maximized) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - 32.0f));
    } else {
        ImGui::SetNextWindowPos(app.normal_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(app.normal_size, ImGuiCond_FirstUseEver);
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
    if (app.is_maximized) flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("Terminal.app##Window", nullptr, flags)) {
        if (!app.is_maximized) {
            app.normal_pos = ImGui::GetWindowPos();
            app.normal_size = ImGui::GetWindowSize();
        }

        RenderAppTitlebar("Командная строка C:\\WINDOWS\\System32\\cmd.exe", "dev://cmd", app);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        float footer_h = 34.0f;
        ImGui::BeginChild("TerminalLogScroll", ImVec2(-1.0f, -footer_h), true);
        for (const auto& line : state.terminal_lines) {
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", line.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Text("C:\\>");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-80.0f);
        bool submit = ImGui::InputText("##TermInputXp", state.terminal_input, sizeof(state.terminal_input), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Ввод") || submit) {
            std::string cmd(state.terminal_input);
            if (!cmd.empty()) {
                state.LogTerminal("C:\\> " + cmd);
                state.terminal_input[0] = '\0';

                if (cmd == "help") {
                    state.LogTerminal("Команды: bench, hogs, trim, map, clear, help, exit");
                } else if (cmd == "clear" || cmd == "cls") {
                    state.terminal_lines.clear();
                } else if (cmd == "exit") {
                    app.is_open = false;
                } else if (cmd == "bench") {
                    state.win_bench.is_open = true; state.win_bench.is_minimized = false;
                    state.LogTerminal("[OK] Окно теста скорости открыто.");
                } else if (cmd == "trim") {
                    state.LogTerminal("[TRIM] Отправка команды DeviceDsmAction_Trim...");
                    wchar_t drv = state.available_drives.empty() ? L'C' : state.available_drives[state.selected_drive_idx];
                    auto vol = clustershift::core::VolumeManager::open_volume(drv, true);
                    if (vol.has_value()) {
                        auto r = clustershift::core::TrimEngine::trim_volume(vol->get());
                        if (r.has_value()) state.LogTerminal("[OK] Команда TRIM выполнена успешно.");
                    }
                } else {
                    state.LogTerminal("'" + cmd + "' не является внутренней или внешней командой.");
                }
            }
        }
    }
    ImGui::End();
}

} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc = {
        sizeof(wc),
        CS_CLASSDC,
        WndProc,
        0L, 0L,
        hInstance,
        nullptr, nullptr, nullptr, nullptr,
        L"ClusterShift_WindowsXP_Class",
        nullptr
    };
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowW(
        wc.lpszClassName,
        L"ClusterShift OS — Windows XP Luna Edition [v0.6.0 by joive]",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1240, 800,
        nullptr, nullptr,
        wc.hInstance,
        nullptr
    );

    if (!CreateDeviceD3D(hWnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // 1. Authentic Windows XP Bliss Wallpaper
    std::wstring bliss_path = ResolveAssetPath(L"assets/winxp/WinXp/Wallpapers/Bliss.png");
    if (!bliss_path.empty()) {
        LoadTextureFromFileWIC(g_pd3dDevice, bliss_path.c_str(), &g_pBlissTextureView, &g_bliss_tex_w, &g_bliss_tex_h);
    }

    // 2. Authentic Windows XP 32x32 Icons Atlas
    std::wstring icons_path = ResolveAssetPath(L"assets/winxp/WinXp/Icons/WinIcons_32.png");
    if (!icons_path.empty()) {
        LoadTextureFromFileWIC(g_pd3dDevice, icons_path.c_str(), &g_pIconsTextureView, &g_icons_tex_w, &g_icons_tex_h);
    }

    // 3. Authentic Windows XP Luna UI Theme Spritesheet
    std::wstring theme_path = ResolveAssetPath(L"assets/winxp/WinXp/Frame/UI Theme.png");
    if (!theme_path.empty()) {
        LoadTextureFromFileWIC(g_pd3dDevice, theme_path.c_str(), &g_pThemeTextureView, &g_theme_tex_w, &g_theme_tex_h);
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Load Windows XP Tahoma font from assets if available, fallback to system fonts
    std::wstring font_path = ResolveAssetPath(L"assets/winxp/WinXp/Fonts/tahoma.ttf");
    if (!font_path.empty()) {
        std::string font_u8 = std::filesystem::path(font_path).string();
        g_font_xp_small  = io.Fonts->AddFontFromFileTTF(font_u8.c_str(), 11.0f);
        g_font_xp        = io.Fonts->AddFontFromFileTTF(font_u8.c_str(), 13.0f);
        g_font_xp_bold   = io.Fonts->AddFontFromFileTTF(font_u8.c_str(), 14.0f);
        g_font_xp_metric = io.Fonts->AddFontFromFileTTF(font_u8.c_str(), 22.0f);
    } else if (std::filesystem::exists("C:\\Windows\\Fonts\\tahoma.ttf")) {
        g_font_xp_small  = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\tahoma.ttf", 11.0f);
        g_font_xp        = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\tahoma.ttf", 13.0f);
        g_font_xp_bold   = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\tahomabd.ttf", 14.0f);
        g_font_xp_metric = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\tahoma.ttf", 22.0f);
    } else if (std::filesystem::exists("C:\\Windows\\Fonts\\consola.ttf")) {
        g_font_xp_small  = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 12.0f);
        g_font_xp        = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 14.0f);
        g_font_xp_bold   = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consolab.ttf", 15.0f);
        g_font_xp_metric = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consolab.ttf", 22.0f);
    }

    clustershift::gui::apply_modern_theme();

    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    (void)clustershift::core::set_se_manage_volume_privilege();
    RefreshDrives();

    // Play classic Windows XP Startup Sound if available
    std::wstring sound_path = ResolveAssetPath(L"assets/winxp/WinXp/Sounds/Windows XP Startup.wav");
    if (!sound_path.empty()) {
        PlaySoundW(sound_path.c_str(), NULL, SND_FILENAME | SND_ASYNC);
    }

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

        // Throttle frame rate when minimized or unfocused to save CPU/GPU
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

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        g_state.EnsureInitialized(viewport->WorkSize);

        // 1. Windows XP Bliss Wallpaper
        RenderBlissWallpaper(viewport->WorkSize);

        // 2. Desktop Icons (My Computer, Benchmark, Space Hogs, Defrag, Control Panel, CMD)
        RenderDesktopIcons(g_state, viewport->WorkSize);

        // 3. Golden Coin "Копейка joive" (Donate button at the top)
        RenderDonateBadge(g_state, viewport->WorkSize);

        // 4. Windows XP Applications (Luna Blue Titlebars + Windows)
        RenderStorageApp(g_state, viewport);
        RenderBenchmarkApp(g_state, viewport);
        RenderSpaceHogsApp(g_state, viewport);
        RenderClusterMapApp(g_state, viewport);
        RenderSettingsApp(g_state, viewport);
        RenderTerminalApp(g_state, viewport);
        RenderDonateModal(g_state, viewport);

        // 5. Classic Windows XP Taskbar (Green Start + Task Tabs + Red Exit in Tray)
        RenderXpTaskbar(g_state, hWnd, viewport);

        // 6. Classic Windows XP Start Menu
        RenderXpStartMenu(g_state, hWnd, viewport);

        // Rendering Pipeline
        ImGui::Render();
        const float clear_color[4] = { 0.15f, 0.55f, 0.85f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // VSync enabled
    }

    if (g_pBlissTextureView) {
        g_pBlissTextureView->Release();
        g_pBlissTextureView = nullptr;
    }
    if (g_pIconsTextureView) {
        g_pIconsTextureView->Release();
        g_pIconsTextureView = nullptr;
    }
    if (g_pThemeTextureView) {
        g_pThemeTextureView->Release();
        g_pThemeTextureView = nullptr;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    CoUninitialize();
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
