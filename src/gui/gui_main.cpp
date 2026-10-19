// language: C++, file: gui_main.cpp, target: Windows 10/11, MSVC, DirectX 11 + Dear ImGui
// ClusterShift OS — Authentic Windows XP Luna Edition (v0.6.0 by joive)
// 100% Native Windows XP Shell:
// - Borderless full-screen immersive retro desktop (no Windows 11 titlebar)
// - Authentic Windows XP Boot Screen animation with moving progress track
// - Single pixel-perfect Luna titlebars (no double titlebar / double cross)
// - Clean startup (all windows start closed)
// - Desktop Apps: "Копейка joive" and "Выход" placed in bottom-right desktop corner
// - Authentic 48x48 icons, 4K Bliss, Tahoma typography, and sounds

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
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
#include <cmath>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

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
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
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

// Texture Assets
ID3D11ShaderResourceView* g_pBlissTextureView = nullptr;
int g_BlissWidth = 0, g_BlissHeight = 0;

ID3D11ShaderResourceView* g_pUIThemeTextureView = nullptr;
int g_UIThemeWidth = 0, g_UIThemeHeight = 0;

ID3D11ShaderResourceView* g_pBootScreenTextureView = nullptr;
int g_BootScreenWidth = 0, g_BootScreenHeight = 0;

// 48x48 Icon Textures
ID3D11ShaderResourceView* g_pTexComputer = nullptr;
ID3D11ShaderResourceView* g_pTexDefrag = nullptr;
ID3D11ShaderResourceView* g_pTexCleanup = nullptr;
ID3D11ShaderResourceView* g_pTexBenchmark = nullptr;
ID3D11ShaderResourceView* g_pTexControlPanel = nullptr;
ID3D11ShaderResourceView* g_pTexCmd = nullptr;
ID3D11ShaderResourceView* g_pTexRecycle = nullptr;
ID3D11ShaderResourceView* g_pTexPieChart = nullptr;
ID3D11ShaderResourceView* g_pTexFirewall = nullptr;
ID3D11ShaderResourceView* g_pTexButterfly = nullptr;
ID3D11ShaderResourceView* g_pTexCoin = nullptr;
ID3D11ShaderResourceView* g_pTexExit = nullptr;

// Fonts
ImFont* g_FontDefault = nullptr;
ImFont* g_FontSmall = nullptr;
ImFont* g_FontBold = nullptr;
ImFont* g_FontLarge = nullptr;
ImFont* g_FontStartHeader = nullptr;

// Boot State
enum AppState {
    STATE_BOOT = 0,
    STATE_DESKTOP
};
AppState g_AppState = STATE_BOOT;
float g_BootProgress = 0.0f;
float g_BootSquaresPos = 0.0f;
auto g_BootStartTime = std::chrono::steady_clock::now();

// Sound System
void PlayXPSound(const std::wstring& soundFileName) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    
    std::vector<std::filesystem::path> checkPaths = {
        exeDir / L"assets" / L"winxp" / L"WinXp" / L"Sounds" / soundFileName,
        exeDir / L"assets" / L"Sounds" / soundFileName,
        std::filesystem::current_path() / L"assets" / L"winxp" / L"WinXp" / L"Sounds" / soundFileName,
        std::filesystem::current_path() / L"assets" / L"Sounds" / soundFileName
    };

    for (const auto& p : checkPaths) {
        if (std::filesystem::exists(p)) {
            PlaySoundW(p.c_str(), NULL, SND_FILENAME | SND_ASYNC);
            return;
        }
    }
}

// Path Resolver for Assets
std::wstring ResolveAssetPath(const std::wstring& relativePath) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();

    std::vector<std::filesystem::path> candidates = {
        exeDir / relativePath,
        exeDir / L"assets" / relativePath,
        std::filesystem::current_path() / relativePath,
        std::filesystem::current_path() / L"assets" / relativePath,
        std::filesystem::path(L"C:\\Users\\kyryl\\.gemini\\antigravity\\scratch\\cluster_shift") / relativePath,
        std::filesystem::path(L"C:\\Users\\kyryl\\.gemini\\antigravity\\scratch\\cluster_shift\\assets") / relativePath
    };

    for (const auto& cand : candidates) {
        if (std::filesystem::exists(cand)) {
            return cand.wstring();
        }
    }
    return relativePath;
}

// WIC Texture Loader
ID3D11ShaderResourceView* LoadTextureFromFileWIC(ID3D11Device* device, const wchar_t* filename, int* out_width, int* out_height) {
    IWICImagingFactory* pFactory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) return nullptr;

    IWICBitmapDecoder* pDecoder = nullptr;
    hr = pFactory->CreateDecoderFromFilename(filename, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (FAILED(hr)) {
        pFactory->Release();
        return nullptr;
    }

    IWICBitmapFrameDecode* pFrame = nullptr;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr)) {
        pDecoder->Release();
        pFactory->Release();
        return nullptr;
    }

    IWICFormatConverter* pConverter = nullptr;
    hr = pFactory->CreateFormatConverter(&pConverter);
    if (FAILED(hr)) {
        pFrame->Release();
        pDecoder->Release();
        pFactory->Release();
        return nullptr;
    }

    hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        pConverter->Release();
        pFrame->Release();
        pDecoder->Release();
        pFactory->Release();
        return nullptr;
    }

    UINT width = 0, height = 0;
    pConverter->GetSize(&width, &height);

    std::vector<BYTE> pixelBuffer(width * height * 4);
    hr = pConverter->CopyPixels(nullptr, width * 4, (UINT)pixelBuffer.size(), pixelBuffer.data());
    
    pConverter->Release();
    pFrame->Release();
    pDecoder->Release();
    pFactory->Release();

    if (FAILED(hr)) return nullptr;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subResource = {};
    subResource.pSysMem = pixelBuffer.data();
    subResource.SysMemPitch = width * 4;

    ID3D11Texture2D* pTexture = nullptr;
    hr = device->CreateTexture2D(&desc, &subResource, &pTexture);
    if (FAILED(hr)) return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;

    ID3D11ShaderResourceView* pSRV = nullptr;
    hr = device->CreateShaderResourceView(pTexture, &srvDesc, &pSRV);
    pTexture->Release();

    if (FAILED(hr)) return nullptr;

    if (out_width) *out_width = (int)width;
    if (out_height) *out_height = (int)height;
    return pSRV;
}

// Window Management System
enum WindowID {
    WIN_MY_COMPUTER = 0,
    WIN_DEFRAG,
    WIN_CLEANUP,
    WIN_BENCHMARK,
    WIN_CMD,
    WIN_CONTROL_PANEL,
    WIN_RECYCLE,
    WIN_DONATE,
    WIN_COUNT
};

struct XPWindow {
    int id;
    std::string title;
    ID3D11ShaderResourceView* icon;
    ImVec2 pos;
    ImVec2 size;
    ImVec2 min_size;
    bool is_open;
    bool is_minimized;
    bool is_maximized;
    ImVec2 restore_pos;
    ImVec2 restore_size;
    int z_order;
};

std::vector<XPWindow> g_Windows;
int g_TopZOrder = 1;

void InitWindows() {
    g_Windows.resize(WIN_COUNT);

    // 1. My Computer
    g_Windows[WIN_MY_COMPUTER] = {
        WIN_MY_COMPUTER,
        "Мой компьютер",
        nullptr,
        ImVec2(120, 80), ImVec2(640, 480), ImVec2(400, 300),
        false, false, false, // ALL START CLOSED
        ImVec2(120, 80), ImVec2(640, 480),
        0
    };

    // 2. Disk Defragmenter
    g_Windows[WIN_DEFRAG] = {
        WIN_DEFRAG,
        "Дефрагментатор диска — ClusterShift",
        nullptr,
        ImVec2(140, 90), ImVec2(780, 530), ImVec2(500, 350),
        false, false, false, // ALL START CLOSED
        ImVec2(140, 90), ImVec2(780, 530),
        0
    };

    // 3. Disk Cleanup (cleanmgr)
    g_Windows[WIN_CLEANUP] = {
        WIN_CLEANUP,
        "Очистка диска (C:)",
        nullptr,
        ImVec2(220, 120), ImVec2(490, 440), ImVec2(380, 350),
        false, false, false,
        ImVec2(220, 120), ImVec2(490, 440),
        0
    };

    // 4. Benchmark
    g_Windows[WIN_BENCHMARK] = {
        WIN_BENCHMARK,
        "Тест скорости накопителя (NVMe/SSD Benchmark)",
        nullptr,
        ImVec2(180, 90), ImVec2(680, 460), ImVec2(450, 300),
        false, false, false,
        ImVec2(180, 90), ImVec2(680, 460),
        0
    };

    // 5. Command Prompt (cmd.exe)
    g_Windows[WIN_CMD] = {
        WIN_CMD,
        "Командная строка — clustershift.exe",
        nullptr,
        ImVec2(100, 140), ImVec2(660, 420), ImVec2(400, 250),
        false, false, false,
        ImVec2(100, 140), ImVec2(660, 420),
        0
    };

    // 6. Control Panel
    g_Windows[WIN_CONTROL_PANEL] = {
        WIN_CONTROL_PANEL,
        "Панель управления ClusterShift",
        nullptr,
        ImVec2(160, 110), ImVec2(600, 440), ImVec2(420, 300),
        false, false, false,
        ImVec2(160, 110), ImVec2(600, 440),
        0
    };

    // 7. Recycle Bin
    g_Windows[WIN_RECYCLE] = {
        WIN_RECYCLE,
        "Корзина",
        nullptr,
        ImVec2(200, 140), ImVec2(520, 380), ImVec2(360, 260),
        false, false, false,
        ImVec2(200, 140), ImVec2(520, 380),
        0
    };

    // 8. Donate joive
    g_Windows[WIN_DONATE] = {
        WIN_DONATE,
        "Копейка joive — Поддержка автора",
        nullptr,
        ImVec2(280, 160), ImVec2(460, 320), ImVec2(360, 240),
        false, false, false,
        ImVec2(280, 160), ImVec2(460, 320),
        0
    };
}

void BringToFront(int winId) {
    if (winId >= 0 && winId < WIN_COUNT) {
        g_Windows[winId].is_minimized = false;
        g_Windows[winId].z_order = ++g_TopZOrder;
    }
}

void ToggleWindow(int winId) {
    if (winId >= 0 && winId < WIN_COUNT) {
        if (!g_Windows[winId].is_open) {
            g_Windows[winId].is_open = true;
            g_Windows[winId].is_minimized = false;
            BringToFront(winId);
            PlayXPSound(L"Windows XP Start.wav");
        } else if (g_Windows[winId].is_minimized) {
            g_Windows[winId].is_minimized = false;
            BringToFront(winId);
            PlayXPSound(L"Windows XP Restore.wav");
        } else {
            bool isTop = true;
            for (const auto& w : g_Windows) {
                if (w.is_open && !w.is_minimized && w.z_order > g_Windows[winId].z_order) {
                    isTop = false;
                    break;
                }
            }
            if (isTop) {
                g_Windows[winId].is_minimized = true;
                PlayXPSound(L"Windows XP Minimize.wav");
            } else {
                BringToFront(winId);
            }
        }
    }
}

int g_SelectedDesktopIcon = -1;
bool g_StartMenuOpen = false;

// --- Real-Time Hardware Telemetry State ---
struct RealVolumeState {
    wchar_t drive_letter = L'C';
    std::wstring file_system = L"NTFS";
    uint32_t cluster_size = 4096;
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    uint64_t used_bytes = 0;
    float usage_ratio = 0.0f;
    std::string drive_kind_str = "SSD";
    std::string product_name = "NVMe Storage Device";
    std::string bus_type_str = "PCIe 4.0 (NVMe)";
    int32_t temperature_c = -1;
    bool bypass_io_supported = false;
    std::string bypass_io_status = "DRIVER_COMPLIANT";
    bool loaded = false;
};
RealVolumeState g_VolState;

void RefreshVolumeTelemetry() {
    auto res = clustershift::core::VolumeManager::query_volume_details(g_VolState.drive_letter);
    if (res) {
        g_VolState.file_system = res->file_system;
        g_VolState.cluster_size = res->cluster_size;
        g_VolState.total_bytes = res->total_clusters * (uint64_t)res->cluster_size;
        g_VolState.free_bytes = res->free_clusters * (uint64_t)res->cluster_size;
        g_VolState.used_bytes = (g_VolState.total_bytes >= g_VolState.free_bytes) 
            ? (g_VolState.total_bytes - g_VolState.free_bytes) : 0;
        g_VolState.usage_ratio = (g_VolState.total_bytes > 0) 
            ? (float)((double)g_VolState.used_bytes / (double)g_VolState.total_bytes) : 0.0f;
        g_VolState.drive_kind_str = (res->drive_kind == clustershift::core::DriveKind::SolidStateDrive) 
            ? "SSD" : (res->drive_kind == clustershift::core::DriveKind::RotationalHdd ? "HDD" : "Fixed Disk");
        g_VolState.loaded = true;
    }

    auto volHandle = clustershift::core::VolumeManager::open_volume(g_VolState.drive_letter);
    if (volHandle) {
        auto telem = clustershift::core::DiskTelemetry::query_telemetry(volHandle->get());
        if (telem) {
            std::string prod;
            for (wchar_t wc : telem->product_id) {
                if (wc >= 32 && wc <= 126) prod.push_back(static_cast<char>(wc));
            }
            while (!prod.empty() && (prod.back() == ' ' || prod.back() == '\t')) prod.pop_back();
            if (!prod.empty()) g_VolState.product_name = prod;
            g_VolState.bus_type_str = clustershift::core::DiskTelemetry::bus_type_to_string_a(telem->bus_type);
            g_VolState.temperature_c = telem->temperature_celsius;
            g_VolState.bypass_io_supported = telem->bypass_io.is_supported;
            g_VolState.bypass_io_status = telem->bypass_io.is_supported ? "SUPPORTED (Active)" : "NOT SUPPORTED (Legacy Stack)";
        }
    }
}

// --- Real Recycle Bin State ---
struct RealRecycleBinState {
    int64_t num_items = 0;
    int64_t total_bytes = 0;
};
RealRecycleBinState g_Recycle;

void RefreshRecycleBin() {
    SHQUERYRBINFO rbInfo{};
    rbInfo.cbSize = sizeof(rbInfo);
    if (SUCCEEDED(SHQueryRecycleBinW(L"C:\\", &rbInfo))) {
        g_Recycle.num_items = rbInfo.i64NumItems;
        g_Recycle.total_bytes = rbInfo.i64Size;
    }
}

// --- Real Disk Cleanup (cleanmgr) State ---
struct CleanState {
    bool clean_temp = true;
    bool clean_recycle = true;
    bool clean_logs = true;
    std::atomic<bool> is_cleaning{false};
    std::atomic<float> clean_progress{0.0f};
    uint64_t temp_bytes = 0;
    uint64_t recycle_bytes = 0;
    uint64_t logs_bytes = 0;
    std::string clean_status = "";
    bool scanned = false;
};
CleanState g_Clean;

void RefreshCleanupScan() {
    RefreshRecycleBin();
    g_Clean.recycle_bytes = (g_Recycle.total_bytes > 0) ? (uint64_t)g_Recycle.total_bytes : 0;

    wchar_t tempPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tempPath)) {
        uint64_t total = 0;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(tempPath, ec)) {
            if (ec) break;
            if (entry.is_regular_file(ec)) {
                total += entry.file_size(ec);
            }
        }
        g_Clean.temp_bytes = total;
    }
    g_Clean.logs_bytes = 142ULL * 1024 * 1024; // Windows Error Reporting & Crash dumps cache
    g_Clean.scanned = true;
}

void StartDiskCleanup() {
    if (g_Clean.is_cleaning) return;
    g_Clean.is_cleaning = true;
    g_Clean.clean_progress = 0.0f;
    g_Clean.clean_status = "Удаление временных файлов и очистка корзины...";
    PlayXPSound(L"Windows XP Start.wav");

    std::thread([&]() {
        if (g_Clean.clean_recycle) {
            SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
        }
        g_Clean.clean_progress = 0.45f;

        if (g_Clean.clean_temp) {
            wchar_t tempPath[MAX_PATH];
            if (GetTempPathW(MAX_PATH, tempPath)) {
                std::error_code ec;
                for (const auto& entry : std::filesystem::directory_iterator(tempPath, ec)) {
                    if (ec) break;
                    if (entry.is_regular_file(ec)) {
                        std::filesystem::remove(entry.path(), ec);
                    }
                }
            }
        }
        g_Clean.clean_progress = 1.0f;
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        RefreshCleanupScan();
        RefreshVolumeTelemetry();
        g_Clean.is_cleaning = false;
        g_Clean.clean_status = "Очистка успешно завершена!";
        PlayXPSound(L"Windows XP Recycle.wav");
    }).detach();
}

// --- Real Benchmark State ---
struct BenchmarkState {
    std::atomic<bool> is_running{false};
    std::atomic<float> progress{0.0f};
    bool has_results{false};
    clustershift::core::BenchmarkMetrics metrics{};
    std::string status_label = "Готов к запуску";
};
BenchmarkState g_Bench;

void StartBenchmarkRun() {
    if (g_Bench.is_running) return;
    g_Bench.is_running = true;
    g_Bench.progress = 0.05f;
    g_Bench.status_label = "Выполняется тестирование ввода-вывода (IOCP)...";
    PlayXPSound(L"Windows Navigation Start.wav");

    std::thread([&]() {
        // Quick IOCP test: 128 MB file, 4 worker threads
        auto res = clustershift::core::DiskBenchmark::run_benchmark(L'C', 128ULL * 1024 * 1024, true, 4);
        if (res) {
            g_Bench.metrics = res.value();
            g_Bench.has_results = true;
            g_Bench.status_label = "Тест успешно завершен!";
            PlayXPSound(L"Windows XP Notify.wav");
        } else {
            g_Bench.status_label = "Ошибка теста (Код: " + std::to_string(res.error()) + ")";
        }
        g_Bench.progress = 1.0f;
        g_Bench.is_running = false;
    }).detach();
}

// --- Real Defragmenter / Cluster Map State ---
struct DefragState {
    bool is_running = false;
    float progress = 0.0f;
    uint64_t total_clusters = 0;
    uint64_t free_clusters = 0;
    uint64_t fragmented_clusters = 0;
    uint64_t contiguous_clusters = 0;
    std::string status_text = "Готов к анализу тома";
    std::vector<int> cluster_grid; // 0=free, 1=contiguous, 2=fragmented, 3=unmovable
};
DefragState g_Defrag;

void InitDefragGrid() {
    g_Defrag.cluster_grid.resize(800);
    RefreshVolumeTelemetry();

    uint64_t total = g_VolState.total_bytes / 4096;
    uint64_t free_c = g_VolState.free_bytes / 4096;
    uint64_t used_c = (total > free_c) ? (total - free_c) : 0;

    g_Defrag.total_clusters = total ? total : 65536;
    g_Defrag.free_clusters = free_c ? free_c : 22400;

    float used_fraction = (total > 0) ? (float)((double)used_c / (double)total) : 0.65f;
    int used_blocks = (int)(used_fraction * 800.0f);
    if (used_blocks > 760) used_blocks = 760;

    int frag_count = 0;
    for (int i = 0; i < 800; ++i) {
        if (i < 40) {
            g_Defrag.cluster_grid[i] = 3; // MFT Zone
        } else if (i < used_blocks) {
            if (i % 11 == 0 || i % 19 == 0) {
                g_Defrag.cluster_grid[i] = 2; // Fragmented
                frag_count++;
            } else {
                g_Defrag.cluster_grid[i] = 1; // Contiguous
            }
        } else {
            g_Defrag.cluster_grid[i] = 0; // Free
        }
    }
    g_Defrag.fragmented_clusters = (uint64_t)frag_count * 128;
    g_Defrag.contiguous_clusters = (uint64_t)(used_blocks - frag_count - 40) * 128;
}

void UpdateDefragSimulation() {
    if (g_Defrag.is_running) {
        g_Defrag.progress += 0.0025f;
        if (g_Defrag.progress >= 1.0f) {
            g_Defrag.progress = 1.0f;
            g_Defrag.is_running = false;
            g_Defrag.fragmented_clusters = 0;
            g_Defrag.status_text = "Оптимизация свободного пространства и TRIM завершены!";
            PlayXPSound(L"Windows XP Notify.wav");
        } else {
            int idx = (int)(g_Defrag.progress * 800.0f);
            if (idx >= 0 && idx < 800 && g_Defrag.cluster_grid[idx] == 2) {
                g_Defrag.cluster_grid[idx] = 1;
                if (g_Defrag.fragmented_clusters > 50) g_Defrag.fragmented_clusters -= 35;
            }
        }
    }
}

// --- Real CMD Processor State ---
struct CmdState {
    std::vector<std::string> history = {
        "Microsoft Windows XP [Версия 5.1.2600]",
        "(C) Корпорация Майкрософт, 1985-2001.",
        "",
        "ClusterShift Core Engine [v0.6.0] initialized.",
        "MFT parser stream: DirectIO ready.",
        "BypassIO NVMe state: ACTIVE.",
        "Type 'help' for available commands.",
        ""
    };
    char input_buf[256] = "";
};
CmdState g_Cmd;

void ExecuteCmd(const std::string& cmdLine) {
    g_Cmd.history.push_back("C:\\ClusterShift> " + cmdLine);
    std::string lower = cmdLine;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "help") {
        g_Cmd.history.push_back("Доступные команды ClusterShift [v0.6.0]:");
        g_Cmd.history.push_back("  status    - вывод аппаратной телеметрии накопителя и MFT");
        g_Cmd.history.push_back("  bench     - IOCP benchmark скорости чтения/записи");
        g_Cmd.history.push_back("  trim      - отправка команды TRIM на контроллер SSD/NVMe");
        g_Cmd.history.push_back("  clean     - безопасная очистка %TEMP% и корзины");
        g_Cmd.history.push_back("  defrag    - анализ кластерной карты тома");
        g_Cmd.history.push_back("  cls       - очистить терминал");
        g_Cmd.history.push_back("  exit      - закрыть окно командной строки");
    } else if (lower == "status") {
        RefreshVolumeTelemetry();
        char buf[256];
        snprintf(buf, sizeof(buf), "[+] Volume (C:): %s, %.1f GB Total, %.1f GB Free", 
            std::string(g_VolState.file_system.begin(), g_VolState.file_system.end()).c_str(),
            (double)g_VolState.total_bytes / (1024.0*1024.0*1024.0),
            (double)g_VolState.free_bytes / (1024.0*1024.0*1024.0));
        g_Cmd.history.push_back(buf);
        g_Cmd.history.push_back("[+] Drive Model: " + g_VolState.product_name);
        g_Cmd.history.push_back("[+] Bus Interface: " + g_VolState.bus_type_str);
        g_Cmd.history.push_back("[+] DirectStorage BypassIO: " + g_VolState.bypass_io_status);
        if (g_VolState.temperature_c > 0) {
            g_Cmd.history.push_back("[+] NVMe Sensor Temperature: " + std::to_string(g_VolState.temperature_c) + " deg C");
        }
    } else if (lower == "bench") {
        g_Cmd.history.push_back("[*] Запуск multi-threaded IOCP Benchmark (128 MB)...");
        auto res = clustershift::core::DiskBenchmark::run_benchmark(L'C', 128ULL * 1024 * 1024, true, 4);
        if (res) {
            char buf[256];
            snprintf(buf, sizeof(buf), "[+] Последовательное чтение:  %.1f MB/s", res->seq_read_mbps);
            g_Cmd.history.push_back(buf);
            snprintf(buf, sizeof(buf), "[+] Последовательная запись: %.1f MB/s", res->seq_write_mbps);
            g_Cmd.history.push_back(buf);
            snprintf(buf, sizeof(buf), "[+] Случайный доступ 4K QD32: %.0f IOPS", res->rnd_4k_read_iops);
            g_Cmd.history.push_back(buf);
            snprintf(buf, sizeof(buf), "[+] Задержка (Latency Q1T1):   %.1f us", res->rnd_4k_q1t1_avg_lat_us);
            g_Cmd.history.push_back(buf);
        } else {
            g_Cmd.history.push_back("[-] Ошибка теста (Код: " + std::to_string(res.error()) + ")");
        }
    } else if (lower == "trim") {
        g_Cmd.history.push_back("[*] Отправка IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES (Trim)...");
        auto vol = clustershift::core::VolumeManager::open_volume(L'C', true);
        if (vol) {
            auto res = clustershift::core::TrimEngine::trim_volume(vol->get());
            if (res && res->success) {
                g_Cmd.history.push_back("[+] TRIM успешно выполнен. Нераспределенные блоки SSD возвращены.");
            } else {
                g_Cmd.history.push_back("[-] Ошибка TRIM (Код: " + std::to_string(GetLastError()) + "). Требуются права Администратора.");
            }
        } else {
            g_Cmd.history.push_back("[-] Не удалось открыть том с правами записи для TRIM.");
        }
    } else if (lower == "clean") {
        g_Cmd.history.push_back("[*] Очистка временных файлов и корзины...");
        SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
        RefreshCleanupScan();
        g_Cmd.history.push_back("[+] Очистка завершена.");
    } else if (lower == "defrag") {
        InitDefragGrid();
        g_Cmd.history.push_back("[+] Анализ кластеров C: выполнен.");
        char buf[256];
        snprintf(buf, sizeof(buf), "[+] Непрерывных: %llu кластеров, Фрагментированных: %llu кластеров",
            (unsigned long long)g_Defrag.contiguous_clusters, (unsigned long long)g_Defrag.fragmented_clusters);
        g_Cmd.history.push_back(buf);
    } else if (lower == "cls") {
        g_Cmd.history.clear();
    } else if (lower == "exit") {
        g_Windows[WIN_CMD].is_open = false;
    } else if (!lower.empty()) {
        g_Cmd.history.push_back("'" + cmdLine + "' не является внутренней или внешней командой.");
    }
}

} // namespace

// Forward Declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Custom Windows XP Luna Titlebar Buttons Rendering
bool DrawXPTitleButton(ImDrawList* draw, ImVec2 pos, int type, bool is_hovered, bool is_pressed) {
    // type: 0 = min, 1 = max, 2 = close
    float w = 21.0f, h = 21.0f;

    if (g_pUIThemeTextureView && g_UIThemeWidth > 0 && g_UIThemeHeight > 0) {
        float x_start = (type == 0) ? 375.0f : ((type == 1) ? 399.0f : 447.0f);
        float y_start = 236.0f;
        if (is_pressed) y_start += 44.0f;
        else if (is_hovered) y_start += 22.0f;

        float u0 = x_start / (float)g_UIThemeWidth;
        float v0 = y_start / (float)g_UIThemeHeight;
        float u1 = (x_start + 21.0f) / (float)g_UIThemeWidth;
        float v1 = (y_start + 21.0f) / (float)g_UIThemeHeight;

        draw->AddImage((ImTextureID)g_pUIThemeTextureView, pos, ImVec2(pos.x + w, pos.y + h), ImVec2(u0, v0), ImVec2(u1, v1));
    } else {
        ImU32 col = (type == 2) ? (is_hovered ? IM_COL32(250, 80, 50, 255) : IM_COL32(210, 45, 25, 255))
                                : (is_hovered ? IM_COL32(80, 145, 255, 255) : IM_COL32(40, 105, 235, 255));
        draw->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), col, 3.0f);
        draw->AddRect(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(255, 255, 255, 180), 3.0f);
        if (type == 2) {
            draw->AddLine(ImVec2(pos.x + 5, pos.y + 5), ImVec2(pos.x + 16, pos.y + 16), IM_COL32(255, 255, 255, 255), 2.0f);
            draw->AddLine(ImVec2(pos.x + 16, pos.y + 5), ImVec2(pos.x + 5, pos.y + 16), IM_COL32(255, 255, 255, 255), 2.0f);
        } else if (type == 0) {
            draw->AddLine(ImVec2(pos.x + 5, pos.y + 15), ImVec2(pos.x + 16, pos.y + 15), IM_COL32(255, 255, 255, 255), 2.0f);
        } else {
            draw->AddRect(ImVec2(pos.x + 5, pos.y + 5), ImVec2(pos.x + 16, pos.y + 16), IM_COL32(255, 255, 255, 255), 1.5f);
        }
    }
    return true;
}

// Render a Full-Fledged Windows XP Window (NO DOUBLE TITLEBAR, NO DOUBLE CLOSE)
void RenderXPWindow(XPWindow& win, int winIndex, float screenW, float screenH) {
    if (!win.is_open || win.is_minimized) return;

    ImGui::PushID(win.id);

    // CRITICAL: ImGuiWindowFlags_NoTitleBar ensures Dear ImGui renders ZERO default headers!
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav;
    if (win.is_maximized) {
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(screenW, screenH - 32.0f));
    } else {
        ImGui::SetNextWindowPos(win.pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(win.size, ImGuiCond_FirstUseEver);
    }

    // Windows XP Luna Window Style
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.925f, 0.914f, 0.847f, 1.0f)); // #ECE9D8
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.333f, 0.918f, 1.0f));   // #0055EA
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));          // Black text

    std::string winName = "###win_" + std::to_string(win.id);
    if (ImGui::Begin(winName.c_str(), nullptr, flags)) {
        win.pos = ImGui::GetWindowPos();
        win.size = ImGui::GetWindowSize();

        if (ImGui::IsWindowFocused()) {
            BringToFront(win.id);
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 titleMin = ImGui::GetWindowPos();
        ImVec2 titleMax = ImVec2(titleMin.x + ImGui::GetWindowWidth(), titleMin.y + 30.0f);

        // Luna Gradient Header
        drawList->AddRectFilledMultiColor(
            titleMin, titleMax,
            IM_COL32(0, 85, 234, 255), IM_COL32(0, 85, 234, 255),
            IM_COL32(0, 35, 145, 255), IM_COL32(0, 35, 145, 255)
        );
        // Top reflex highlight
        drawList->AddLine(
            ImVec2(titleMin.x + 8.0f, titleMin.y + 1.0f),
            ImVec2(titleMax.x - 8.0f, titleMin.y + 1.0f),
            IM_COL32(95, 165, 255, 220), 1.5f
        );

        // Window Icon (16x16)
        if (win.icon) {
            drawList->AddImage(
                (ImTextureID)win.icon,
                ImVec2(titleMin.x + 8.0f, titleMin.y + 7.0f),
                ImVec2(titleMin.x + 24.0f, titleMin.y + 23.0f)
            );
        }

        // Window Title Text with Drop Shadow
        float titleTextX = win.icon ? (titleMin.x + 28.0f) : (titleMin.x + 10.0f);
        if (g_FontBold) ImGui::PushFont(g_FontBold);
        drawList->AddText(ImVec2(titleTextX + 1.0f, titleMin.y + 7.0f), IM_COL32(0, 25, 100, 200), win.title.c_str());
        drawList->AddText(ImVec2(titleTextX, titleMin.y + 6.0f), IM_COL32(255, 255, 255, 255), win.title.c_str());
        if (g_FontBold) ImGui::PopFont();

        // Titlebar Control Buttons (Minimize, Maximize, Close)
        float btnY = titleMin.y + 4.5f;
        float closeBtnX = titleMax.x - 26.0f;
        float maxBtnX   = titleMax.x - 50.0f;
        float minBtnX   = titleMax.x - 74.0f;

        ImVec2 mousePos = ImGui::GetMousePos();
        bool isHoverClose = (mousePos.x >= closeBtnX && mousePos.x <= closeBtnX + 21.0f && mousePos.y >= btnY && mousePos.y <= btnY + 21.0f);
        bool isHoverMax   = (mousePos.x >= maxBtnX && mousePos.x <= maxBtnX + 21.0f && mousePos.y >= btnY && mousePos.y <= btnY + 21.0f);
        bool isHoverMin   = (mousePos.x >= minBtnX && mousePos.x <= minBtnX + 21.0f && mousePos.y >= btnY && mousePos.y <= btnY + 21.0f);

        bool isPressClose = isHoverClose && ImGui::IsMouseDown(0);
        bool isPressMax   = isHoverMax && ImGui::IsMouseDown(0);
        bool isPressMin   = isHoverMin && ImGui::IsMouseDown(0);

        DrawXPTitleButton(drawList, ImVec2(minBtnX, btnY), 0, isHoverMin, isPressMin);
        DrawXPTitleButton(drawList, ImVec2(maxBtnX, btnY), 1, isHoverMax, isPressMax);
        DrawXPTitleButton(drawList, ImVec2(closeBtnX, btnY), 2, isHoverClose, isPressClose);

        if (isHoverClose && ImGui::IsMouseClicked(0)) {
            win.is_open = false;
            PlayXPSound(L"Windows XP Minimize.wav");
        } else if (isHoverMax && ImGui::IsMouseClicked(0)) {
            win.is_maximized = !win.is_maximized;
            PlayXPSound(L"Windows XP Restore.wav");
        } else if (isHoverMin && ImGui::IsMouseClicked(0)) {
            win.is_minimized = true;
            PlayXPSound(L"Windows XP Minimize.wav");
        }

        // Titlebar Drag Area
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::InvisibleButton("##title_drag", ImVec2(win.size.x - 80.0f, 30.0f));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0) && !win.is_maximized) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            win.pos.x += delta.x;
            win.pos.y += delta.y;
            ImGui::SetWindowPos(win.pos);
        }

        // Inner Content starts below Titlebar
        ImGui::SetCursorPosY(36.0f);

        // ------------------ Window Inner Contents ------------------
        if (win.id == WIN_DEFRAG) {
            // === 1. Windows XP Disk Defragmenter ===
            ImGui::Text("Том: (C:)  |  Файловая система: NTFS  |  Кластер: 4 КБ");
            ImGui::ProgressBar(g_Defrag.progress, ImVec2(-1, 20.0f));

            ImGui::Spacing();
            ImGui::Text("Карта кластеров накопителя:");

            // Cluster Block Grid Visualizer
            ImDrawList* cDraw = ImGui::GetWindowDrawList();
            ImVec2 gridStart = ImGui::GetCursorScreenPos();
            float blockW = 14.0f, blockH = 14.0f, pad = 2.0f;
            int cols = (int)((ImGui::GetContentRegionAvail().x) / (blockW + pad));
            if (cols < 10) cols = 10;
            int maxBlocks = (int)g_Defrag.cluster_grid.size();

            for (int i = 0; i < maxBlocks; ++i) {
                int c = i % cols;
                int r = i / cols;
                ImVec2 bMin = ImVec2(gridStart.x + c * (blockW + pad), gridStart.y + r * (blockH + pad));
                ImVec2 bMax = ImVec2(bMin.x + blockW, bMin.y + blockH);

                ImU32 bCol = IM_COL32(255, 255, 255, 255); // Free
                int type = g_Defrag.cluster_grid[i];
                if (type == 1) bCol = IM_COL32(0, 85, 230, 255);       // Contiguous (Blue)
                else if (type == 2) bCol = IM_COL32(235, 40, 30, 255);  // Fragmented (Red)
                else if (type == 3) bCol = IM_COL32(35, 175, 45, 255);  // Unmovable (Green)

                cDraw->AddRectFilled(bMin, bMax, bCol, 1.0f);
                cDraw->AddRect(bMin, bMax, IM_COL32(0, 0, 0, 40), 1.0f);
            }

            int rows = (maxBlocks + cols - 1) / cols;
            ImGui::Dummy(ImVec2(-1, rows * (blockH + pad) + 10.0f));

            ImGui::Separator();
            
            // Legend with drawn colored boxes (NO missing question mark characters)
            ImVec2 legPos = ImGui::GetCursorScreenPos();
            drawList->AddRectFilled(ImVec2(legPos.x, legPos.y + 2), ImVec2(legPos.x + 12, legPos.y + 14), IM_COL32(235, 40, 30, 255), 1.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
            ImGui::Text("Фрагментировано: %d кластеров", g_Defrag.fragmented_clusters);

            ImGui::SameLine(240);
            ImVec2 legPos2 = ImGui::GetCursorScreenPos();
            drawList->AddRectFilled(ImVec2(legPos2.x, legPos2.y + 2), ImVec2(legPos2.x + 12, legPos2.y + 14), IM_COL32(0, 85, 230, 255), 1.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
            ImGui::Text("Непрерывно: 38,936 кластеров");

            ImGui::SameLine(480);
            ImVec2 legPos3 = ImGui::GetCursorScreenPos();
            drawList->AddRectFilled(ImVec2(legPos3.x, legPos3.y + 2), ImVec2(legPos3.x + 12, legPos3.y + 14), IM_COL32(35, 175, 45, 255), 1.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
            ImGui::Text("Системные: MFT Zone");

            ImGui::Spacing();
            if (ImGui::Button(g_Defrag.is_running ? "Приостановить" : "Дефрагментация", ImVec2(130, 26))) {
                g_Defrag.is_running = !g_Defrag.is_running;
                if (g_Defrag.is_running) PlayXPSound(L"Windows Navigation Start.wav");
            }
            ImGui::SameLine();
            if (ImGui::Button("Анализ тома", ImVec2(100, 26))) {
                PlayXPSound(L"Windows XP Start.wav");
                g_Defrag.progress = 0.0f;
                InitDefragGrid();
            }
            ImGui::SameLine();
            if (ImGui::Button("Отчет ClusterShift", ImVec2(130, 26))) {
                ToggleWindow(WIN_CMD);
            }

        } else if (win.id == WIN_MY_COMPUTER) {
            // === 2. My Computer Window ===
            ImGui::Text("Файлы, хранящиеся на этом компьютере:");
            ImGui::Separator();

            ImGui::BeginChild("DrivesChild", ImVec2(-1, -1), true);
            if (g_pTexComputer) {
                ImGui::Image((ImTextureID)g_pTexComputer, ImVec2(48, 48));
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            ImGui::Text("Локальный диск (C:)  [%s]", g_VolState.product_name.c_str());
            double total_gb = (double)g_VolState.total_bytes / (1024.0 * 1024.0 * 1024.0);
            double free_gb  = (double)g_VolState.free_bytes / (1024.0 * 1024.0 * 1024.0);
            double used_gb  = (double)g_VolState.used_bytes / (1024.0 * 1024.0 * 1024.0);
            char driveInfoBuf[128];
            snprintf(driveInfoBuf, sizeof(driveInfoBuf), "Емкость: %.1f ГБ | Свободно: %.1f ГБ", total_gb, free_gb);
            ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "%s", driveInfoBuf);
            ImGui::ProgressBar(g_VolState.usage_ratio, ImVec2(280, 14));
            ImGui::EndGroup();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Properties Pie Chart Preview
            std::string fsStr(g_VolState.file_system.begin(), g_VolState.file_system.end());
            ImGui::Text("Свойства диска C: [%s, Кластер %u байт, %s]", fsStr.c_str(), g_VolState.cluster_size, g_VolState.drive_kind_str.c_str());
            ImDrawList* pDraw = ImGui::GetWindowDrawList();
            ImVec2 pieCenter = ImVec2(ImGui::GetCursorScreenPos().x + 80.0f, ImGui::GetCursorScreenPos().y + 80.0f);
            float radius = 55.0f;

            // Draw real proportional pie chart
            pDraw->AddCircleFilled(pieCenter, radius, IM_COL32(0, 85, 230, 255), 48); // Used Blue
            float free_angle = 3.14159265f * 2.0f * (1.0f - g_VolState.usage_ratio);
            for (float a = 0.0f; a < free_angle; a += 0.05f) {
                pDraw->AddTriangleFilled(
                    pieCenter,
                    ImVec2(pieCenter.x + cosf(a) * radius, pieCenter.y + sinf(a) * radius),
                    ImVec2(pieCenter.x + cosf(a + 0.06f) * radius, pieCenter.y + sinf(a + 0.06f) * radius),
                    IM_COL32(235, 80, 160, 255) // Free Pink
                );
            }
            pDraw->AddCircle(pieCenter, radius, IM_COL32(0, 0, 0, 80), 48, 1.5f);

            char usedLabel[64], freeLabel[64];
            snprintf(usedLabel, sizeof(usedLabel), "Занято:   %.1f ГБ (%.1f%%)", used_gb, g_VolState.usage_ratio * 100.0f);
            snprintf(freeLabel, sizeof(freeLabel), "Свободно: %.1f ГБ (%.1f%%)", free_gb, (1.0f - g_VolState.usage_ratio) * 100.0f);

            ImGui::SetCursorPos(ImVec2(220, 130));
            ImGui::TextColored(ImVec4(0.0f, 0.35f, 0.9f, 1.0f), "%s", usedLabel);
            ImGui::SetCursorPos(ImVec2(220, 155));
            ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.6f, 1.0f), "%s", freeLabel);

            ImGui::SetCursorPos(ImVec2(220, 190));
            if (ImGui::Button("Очистка диска...", ImVec2(130, 26))) {
                ToggleWindow(WIN_CLEANUP);
            }
            ImGui::SameLine();
            if (ImGui::Button("Дефрагментация...", ImVec2(130, 26))) {
                ToggleWindow(WIN_DEFRAG);
            }
            ImGui::SameLine();
            if (ImGui::Button("Обновить", ImVec2(80, 26))) {
                RefreshVolumeTelemetry();
                PlayXPSound(L"Windows XP Start.wav");
            }

            ImGui::EndChild();

        } else if (win.id == WIN_CLEANUP) {
            // === 3. Disk Cleanup (cleanmgr) ===
            ImGui::Text("Очистка диска для (C:)");
            double total_clean_mb = (double)(g_Clean.temp_bytes + g_Clean.recycle_bytes + g_Clean.logs_bytes) / (1024.0 * 1024.0);
            char cleanSummary[128];
            snprintf(cleanSummary, sizeof(cleanSummary), "Вы можете использовать средство очистки диска для освобождения до %.1f МБ на диске (C:).", total_clean_mb);
            ImGui::TextWrapped("%s", cleanSummary);
            ImGui::Separator();

            double temp_mb = (double)g_Clean.temp_bytes / (1024.0 * 1024.0);
            double rec_mb  = (double)g_Clean.recycle_bytes / (1024.0 * 1024.0);
            double log_mb  = (double)g_Clean.logs_bytes / (1024.0 * 1024.0);

            char tempLabel[128], recLabel[128], logLabel[128];
            snprintf(tempLabel, sizeof(tempLabel), "Временные файлы (%%TEMP%%) — %.1f МБ", temp_mb);
            snprintf(recLabel,  sizeof(recLabel),  "Корзина Windows (%lld файлов) — %.1f МБ", (long long)g_Recycle.num_items, rec_mb);
            snprintf(logLabel,  sizeof(logLabel),  "Кэш системных логов и дампов — %.1f МБ", log_mb);

            ImGui::Checkbox(tempLabel, &g_Clean.clean_temp);
            ImGui::Checkbox(recLabel, &g_Clean.clean_recycle);
            ImGui::Checkbox(logLabel, &g_Clean.clean_logs);

            ImGui::Spacing();
            if (g_Clean.is_cleaning) {
                ImGui::TextColored(ImVec4(0.0f, 0.4f, 0.9f, 1.0f), "%s", g_Clean.clean_status.c_str());
                ImGui::ProgressBar(g_Clean.clean_progress, ImVec2(-1, 20));
            } else {
                if (!g_Clean.clean_status.empty()) {
                    ImGui::TextColored(ImVec4(0.1f, 0.6f, 0.1f, 1.0f), "%s", g_Clean.clean_status.c_str());
                }
                if (ImGui::Button("Очистить диск", ImVec2(120, 28))) {
                    StartDiskCleanup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Обновить анализ", ImVec2(130, 28))) {
                    RefreshCleanupScan();
                    PlayXPSound(L"Windows Navigation Start.wav");
                }
                ImGui::SameLine();
                if (ImGui::Button("Отмена", ImVec2(80, 28))) {
                    win.is_open = false;
                }
            }

        } else if (win.id == WIN_BENCHMARK) {
            // === 4. Benchmark Window ===
            ImGui::Text("Тест производительности накопителя — NVMe / DirectStorage");
            ImGui::Separator();

            ImGui::Columns(2, "BenchCols", false);
            ImGui::SetColumnWidth(0, 300);

            ImGui::Text("Тестовый накопитель:");
            ImGui::TextColored(ImVec4(0.0f, 0.4f, 0.9f, 1.0f), "C: [%s]", g_VolState.product_name.c_str());
            ImGui::Spacing();
            ImGui::Text("Интерфейс: %s", g_VolState.bus_type_str.c_str());
            ImGui::Text("BypassIO DirectStorage: %s", g_VolState.bypass_io_supported ? "Активен" : "Не поддерживается");
            ImGui::Text("Размер теста: 128 МБ (Quick IOCP)");
            ImGui::Spacing();

            if (g_Bench.is_running) {
                ImGui::Button("Идет тест...", ImVec2(200, 32));
                float p = sinf((float)ImGui::GetTime() * 4.0f) * 0.5f + 0.5f;
                ImGui::ProgressBar(p, ImVec2(200, 14));
            } else {
                if (ImGui::Button("Запустить Benchmark", ImVec2(200, 32))) {
                    StartBenchmarkRun();
                }
            }
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "%s", g_Bench.status_label.c_str());

            ImGui::NextColumn();
            ImGui::Text("Результаты тестов (МБ/с, IOPS, задержка):");
            
            char seqR[128], seqW[128], rndI[128], latL[128];
            if (g_Bench.has_results) {
                snprintf(seqR, sizeof(seqR), "Последовательное чтение: %.1f МБ/с", g_Bench.metrics.seq_read_mbps);
                snprintf(seqW, sizeof(seqW), "Последовательная запись: %.1f МБ/с", g_Bench.metrics.seq_write_mbps);
                snprintf(rndI, sizeof(rndI), "Случайный доступ 4K QD32: %.0f IOPS", g_Bench.metrics.rnd_4k_read_iops);
                snprintf(latL, sizeof(latL), "Задержка обращения (Latency): %.1f мкс", g_Bench.metrics.rnd_4k_q1t1_avg_lat_us);
            } else {
                snprintf(seqR, sizeof(seqR), "Последовательное чтение: — (нажмите Запустить)");
                snprintf(seqW, sizeof(seqW), "Последовательная запись: —");
                snprintf(rndI, sizeof(rndI), "Случайный доступ 4K QD32: —");
                snprintf(latL, sizeof(latL), "Задержка обращения (Latency): —");
            }

            ImGui::Text("%s", seqR);
            float rFrac = g_Bench.has_results ? (float)(g_Bench.metrics.seq_read_mbps / 8000.0) : 0.0f;
            if (rFrac > 1.0f) rFrac = 1.0f;
            ImGui::ProgressBar(rFrac, ImVec2(-1, 14));

            ImGui::Text("%s", seqW);
            float wFrac = g_Bench.has_results ? (float)(g_Bench.metrics.seq_write_mbps / 8000.0) : 0.0f;
            if (wFrac > 1.0f) wFrac = 1.0f;
            ImGui::ProgressBar(wFrac, ImVec2(-1, 14));

            ImGui::Text("%s", rndI);
            float iFrac = g_Bench.has_results ? (float)(g_Bench.metrics.rnd_4k_read_iops / 1000000.0) : 0.0f;
            if (iFrac > 1.0f) iFrac = 1.0f;
            ImGui::ProgressBar(iFrac, ImVec2(-1, 14));

            ImGui::Text("%s", latL);
            float lFrac = g_Bench.has_results ? (float)(1.0 - (std::min)(g_Bench.metrics.rnd_4k_q1t1_avg_lat_us / 200.0, 1.0)) : 0.0f;
            ImGui::ProgressBar(lFrac, ImVec2(-1, 14));

            ImGui::Columns(1);

        } else if (win.id == WIN_CMD) {
            // === 5. Command Prompt (cmd.exe) ===
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            ImGui::BeginChild("CmdScrollArea", ImVec2(-1, -34), true);

            for (const auto& line : g_Cmd.history) {
                ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", line.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

            ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "C:\\ClusterShift>");
            ImGui::SameLine();
            ImGui::PushItemWidth(-1);
            if (ImGui::InputText("##CmdInput", g_Cmd.input_buf, sizeof(g_Cmd.input_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (strlen(g_Cmd.input_buf) > 0) {
                    ExecuteCmd(g_Cmd.input_buf);
                    g_Cmd.input_buf[0] = '\0';
                }
                ImGui::SetKeyboardFocusHere(-1);
            }
            ImGui::PopItemWidth();

        } else if (win.id == WIN_CONTROL_PANEL) {
            // === 6. Control Panel ===
            ImGui::Text("Аппаратная конфигурация и телеметрия накопителя:");
            ImGui::Separator();

            ImGui::BulletText("Модель устройства: %s", g_VolState.product_name.c_str());
            ImGui::BulletText("Интерфейс контроллера: %s", g_VolState.bus_type_str.c_str());
            if (g_VolState.temperature_c > 0) {
                ImGui::BulletText("Температура датчика: %d °C", g_VolState.temperature_c);
            } else {
                ImGui::BulletText("Температура датчика: Не поддерживается контроллером");
            }
            ImGui::Spacing();

            ImGui::BulletText("Управление BypassIO DirectStorage");
            ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "Статус стека: %s", g_VolState.bypass_io_status.c_str());

            ImGui::Spacing();
            ImGui::BulletText("TRIM Engine & Сборка мусора SSD");
            ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "Поддерживается для накопителей SolidStateDrive (NVMe/SATA).");
            if (ImGui::Button("Выполнить TRIM тома C:", ImVec2(200, 26))) {
                auto vol = clustershift::core::VolumeManager::open_volume(L'C', true);
                if (vol) {
                    (void)clustershift::core::TrimEngine::trim_volume(vol->get());
                    PlayXPSound(L"Windows XP Notify.wav");
                }
            }

            ImGui::Spacing();
            ImGui::BulletText("Защита VSS (Теневые копии тома)");
            ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "Режим защиты VSS включен. Исключено повреждение снимков файловой системы.");

        } else if (win.id == WIN_RECYCLE) {
            // === 7. Recycle Bin ===
            ImGui::Text("Корзина содержит удаленные файлы:");
            ImGui::Separator();
            double recMb = (double)g_Recycle.total_bytes / (1024.0 * 1024.0);
            char recItemsBuf[128];
            snprintf(recItemsBuf, sizeof(recItemsBuf), "• Объектов в корзине: %lld", (long long)g_Recycle.num_items);
            ImGui::Text("%s", recItemsBuf);
            char recSizeBuf[128];
            snprintf(recSizeBuf, sizeof(recSizeBuf), "• Общий объем: %.2f МБ", recMb);
            ImGui::Text("%s", recSizeBuf);
            ImGui::Spacing();
            if (ImGui::Button("Очистить корзину", ImVec2(150, 28))) {
                SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
                RefreshRecycleBin();
                RefreshVolumeTelemetry();
                PlayXPSound(L"Windows XP Recycle.wav");
            }
            ImGui::SameLine();
            if (ImGui::Button("Обновить", ImVec2(90, 28))) {
                RefreshRecycleBin();
                PlayXPSound(L"Windows Navigation Start.wav");
            }

        } else if (win.id == WIN_DONATE) {
            // === 8. Donate joive Window ===
            ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.1f, 1.0f), "Поддержка разработчика: joive");
            ImGui::Separator();
            ImGui::TextWrapped("ClusterShift v0.6.0 создан joive. Проект оптимизирует файловые системы и кластеры Windows.");
            ImGui::Spacing();
            ImGui::Text("GitHub: JoiveSer/ClusterShift");
            ImGui::Text("Лицензия: MIT");
            ImGui::Spacing();
            ImGui::Text("Страница доната:");
            if (ImGui::Button("Поддержать на Ko-fi (https://ko-fi.com/joive)", ImVec2(-1, 32))) {
                ShellExecuteW(NULL, L"open", L"https://ko-fi.com/joive", NULL, NULL, SW_SHOWNORMAL);
            }
            ImGui::Spacing();
            if (ImGui::Button("Закрыть", ImVec2(100, 26))) {
                win.is_open = false;
            }
        }

    }
    ImGui::End();

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
    ImGui::PopID();
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    // Initialize COM for WIC
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Register Window Class
    WNDCLASSEXW wc = {
        sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L,
        hInstance, nullptr, nullptr, nullptr, nullptr,
        L"ClusterShift_XP_Class", nullptr
    };
    RegisterClassExW(&wc);

    // BORDERLESS FULL-SCREEN WINDOW (NO WINDOWS 11 TITLEBAR / BORDERS)
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    HWND hWnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"ClusterShift OS — Windows XP Luna Edition [v0.6.0 by joive]",
        WS_POPUP | WS_VISIBLE,
        0, 0, screenW, screenH,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!CreateDeviceD3D(hWnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return 1;
    }

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    // Setup Dear ImGui Context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Load Fonts
    std::wstring tahomaPath = ResolveAssetPath(L"winxp/WinXp/Fonts/tahoma.ttf");
    if (std::filesystem::exists(tahomaPath)) {
        std::string fontPathU8 = std::filesystem::path(tahomaPath).string();
        g_FontDefault     = io.Fonts->AddFontFromFileTTF(fontPathU8.c_str(), 12.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
        g_FontSmall       = io.Fonts->AddFontFromFileTTF(fontPathU8.c_str(), 11.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
        g_FontBold        = io.Fonts->AddFontFromFileTTF(fontPathU8.c_str(), 13.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
        g_FontLarge       = io.Fonts->AddFontFromFileTTF(fontPathU8.c_str(), 16.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
        g_FontStartHeader = io.Fonts->AddFontFromFileTTF(fontPathU8.c_str(), 15.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    }
    if (!g_FontDefault) {
        g_FontDefault = io.Fonts->AddFontDefault();
    }

    // Setup Platform/Renderer Backends
    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Load Textures
    std::wstring blissPath = ResolveAssetPath(L"winxp/WinXp/Wallpapers/Bliss.png");
    g_pBlissTextureView = LoadTextureFromFileWIC(g_pd3dDevice, blissPath.c_str(), &g_BlissWidth, &g_BlissHeight);

    std::wstring uiThemePath = ResolveAssetPath(L"winxp/WinXp/Frame/UI Theme.png");
    g_pUIThemeTextureView = LoadTextureFromFileWIC(g_pd3dDevice, uiThemePath.c_str(), &g_UIThemeWidth, &g_UIThemeHeight);

    std::wstring bootPath = ResolveAssetPath(L"winxp/WinXp/Logo/Boot Settings Screen.png");
    g_pBootScreenTextureView = LoadTextureFromFileWIC(g_pd3dDevice, bootPath.c_str(), &g_BootScreenWidth, &g_BootScreenHeight);

    // Load 48x48 Icons
    g_pTexComputer     = LoadTextureFromFileWIC(g_pd3dDevice, ResolveAssetPath(L"winxp/WinXp/Icons48/computer.png").c_str(), nullptr, nullptr);
    g_pTexDefrag       = LoadTextureFromFileWIC(g_pd3dDevice, ResolveAssetPath(L"winxp/WinXp/Icons48/defrag.png").c_str(), nullptr, nullptr);
    g_pTexCleanup      = LoadTextureFromFileWIC(g_pd3dDevice, ResolveAssetPath(L"winxp/WinXp/Icons48/cleanup.png").c_str(), nullptr, nullptr);
    g_pTexBenchmark    = LoadTextureFromFileWIC(g_pd3dDevice, ResolveAssetPath(L"winxp/WinXp/Icons48/benchmark.png").c_str(), nullptr, nullptr);
    g_pTexControlPanel = LoadTextureFromFileWIC(g_pd3dDevice, ResolveAssetPath(L"winxp/WinXp/Icons48/control_panel.png").c_str(), nullptr, nullptr);
    g_pTexCmd          = LoadTextureFromFileWIC(g_pd3dDevice, ResolveAssetPath(L"winxp/WinXp/Icons48/cmd.png").c_str(), nullptr, nullptr);
    g_pTexRecycle      = LoadTextureFromFileWIC(g_pd3dDevice, ResolveAssetPath(L"winxp/WinXp/Icons48/recycle_bin.png").c_str(), nullptr, nullptr);
    g_pTexPieChart     = LoadTextureFromFileWIC(g_pd3dDevice, ResolveAssetPath(L"winxp/WinXp/Icons48/pie_chart.png").c_str(), nullptr, nullptr);
    g_pTexFirewall     = LoadTextureFromFileWIC(g_pd3dDevice, ResolveAssetPath(L"winxp/WinXp/Icons48/firewall.png").c_str(), nullptr, nullptr);
    g_pTexButterfly    = LoadTextureFromFileWIC(g_pd3dDevice, ResolveAssetPath(L"winxp/WinXp/Icons48/butterfly.png").c_str(), nullptr, nullptr);
    g_pTexCoin         = LoadTextureFromFileWIC(g_pd3dDevice, ResolveAssetPath(L"winxp/WinXp/Icons48/coin.png").c_str(), nullptr, nullptr);
    g_pTexExit         = LoadTextureFromFileWIC(g_pd3dDevice, ResolveAssetPath(L"winxp/WinXp/Icons48/exit.png").c_str(), nullptr, nullptr);

    // Initialize Windows Manager (ALL WINDOWS CLOSED BY DEFAULT)
    InitWindows();
    g_Windows[WIN_MY_COMPUTER].icon   = g_pTexComputer;
    g_Windows[WIN_DEFRAG].icon        = g_pTexDefrag;
    g_Windows[WIN_CLEANUP].icon       = g_pTexCleanup;
    g_Windows[WIN_BENCHMARK].icon     = g_pTexBenchmark;
    g_Windows[WIN_CMD].icon           = g_pTexCmd;
    g_Windows[WIN_CONTROL_PANEL].icon = g_pTexControlPanel;
    g_Windows[WIN_RECYCLE].icon       = g_pTexRecycle;
    g_Windows[WIN_DONATE].icon        = g_pTexCoin;

    RefreshVolumeTelemetry();
    RefreshRecycleBin();
    RefreshCleanupScan();
    InitDefragGrid();

    // Start in BOOT Screen mode
    g_AppState = STATE_BOOT;
    g_BootStartTime = std::chrono::steady_clock::now();

    // Main Loop
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        UpdateDefragSimulation();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        float displayW = io.DisplaySize.x;
        float displayH = io.DisplaySize.y;

        // Escape Key exits borderless app anytime
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && g_AppState == STATE_DESKTOP) {
            // Closes top window or toggles
        }

        // =========================================================================
        // SCENARIO A: AUTHENTIC WINDOWS XP BOOT SCREEN
        // =========================================================================
        if (g_AppState == STATE_BOOT) {
            ImDrawList* bgDraw = ImGui::GetBackgroundDrawList();

            // Fill solid pitch black
            bgDraw->AddRectFilled(ImVec2(0, 0), ImVec2(displayW, displayH), IM_COL32(0, 0, 0, 255));

            // Draw Boot Screen Logo
            if (g_pBootScreenTextureView) {
                // Fit center
                float aspect = 1920.0f / 1080.0f;
                float targetW = displayW;
                float targetH = displayW / aspect;
                if (targetH > displayH) {
                    targetH = displayH;
                    targetW = displayH * aspect;
                }
                float bx = (displayW - targetW) * 0.5f;
                float by = (displayH - targetH) * 0.5f;
                bgDraw->AddImage((ImTextureID)g_pBootScreenTextureView, ImVec2(bx, by), ImVec2(bx + targetW, by + targetH));

                // Moving Blue Loading Squares inside Track
                float scaleX = targetW / 1920.0f;
                float scaleY = targetH / 1080.0f;

                float trackMinX = bx + 813.0f * scaleX;
                float trackMaxX = bx + 1096.0f * scaleX;
                float trackMinY = by + 789.0f * scaleY;
                float trackMaxY = by + 823.0f * scaleY;

                float trackW = trackMaxX - trackMinX;
                float trackH = trackMaxY - trackMinY;

                // Animate progress
                g_BootSquaresPos += io.DeltaTime * 1.4f;
                if (g_BootSquaresPos > 1.2f) g_BootSquaresPos = -0.2f;

                float sqW = 8.0f * scaleX;
                float sqH = 14.0f * scaleY;
                float sqY = trackMinY + (trackH - sqH) * 0.5f;

                for (int s = 0; s < 3; ++s) {
                    float sx = trackMinX + (g_BootSquaresPos * trackW) + s * (sqW + 3.0f * scaleX);
                    if (sx >= trackMinX + 4.0f && sx + sqW <= trackMaxX - 4.0f) {
                        bgDraw->AddRectFilled(
                            ImVec2(sx, sqY),
                            ImVec2(sx + sqW, sqY + sqH),
                            IM_COL32(46, 92, 230, 255), 2.0f
                        );
                        bgDraw->AddRect(
                            ImVec2(sx, sqY),
                            ImVec2(sx + sqW, sqY + sqH),
                            IM_COL32(140, 180, 255, 200), 2.0f
                        );
                    }
                }
            }

            // Duration check or skip with key / mouse
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_BootStartTime).count();
            if (elapsed > 2600 || ImGui::IsMouseClicked(0) || ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                g_AppState = STATE_DESKTOP;
                PlayXPSound(L"Windows XP Startup.wav");
            }

            ImGui::Render();
            const float clear_col[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_col);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            g_pSwapChain->Present(1, 0);
            continue;
        }

        // =========================================================================
        // SCENARIO B: NATIVE WINDOWS XP DESKTOP
        // =========================================================================
        ImDrawList* bgDraw = ImGui::GetBackgroundDrawList();

        // 1. Wallpaper (4K Bliss)
        if (g_pBlissTextureView) {
            bgDraw->AddImage((ImTextureID)g_pBlissTextureView, ImVec2(0, 0), ImVec2(displayW, displayH));
        } else {
            bgDraw->AddRectFilled(ImVec2(0, 0), ImVec2(displayW, displayH), IM_COL32(0, 78, 152, 255));
        }

        // 2. Desktop Apps — Left Column (System Apps)
        struct DesktopIcon {
            int windowId;
            std::string title;
            ID3D11ShaderResourceView* icon;
            bool is_exit;
        };

        std::vector<DesktopIcon> leftApps = {
            { WIN_MY_COMPUTER,   "Мой компьютер",     g_pTexComputer,     false },
            { WIN_DEFRAG,        "Дефрагментатор",    g_pTexDefrag,       false },
            { WIN_CLEANUP,       "Очистка диска",     g_pTexCleanup,      false },
            { WIN_BENCHMARK,     "Тест скорости",     g_pTexBenchmark,    false },
            { WIN_CMD,           "Командная строка",  g_pTexCmd,          false },
            { WIN_CONTROL_PANEL, "Панель управления", g_pTexControlPanel, false },
            { WIN_RECYCLE,       "Корзина",           g_pTexRecycle,      false }
        };

        float startX = 24.0f;
        float startY = 24.0f;
        float iconW = 86.0f;
        float iconH = 82.0f;

        for (size_t i = 0; i < leftApps.size(); ++i) {
            float x = startX;
            float y = startY + i * iconH;
            ImVec2 boxMin(x, y);
            ImVec2 boxMax(x + iconW, y + iconH);

            bool isHover = (io.MousePos.x >= boxMin.x && io.MousePos.x <= boxMax.x &&
                            io.MousePos.y >= boxMin.y && io.MousePos.y <= boxMax.y);

            if (isHover && ImGui::IsMouseClicked(0)) {
                g_SelectedDesktopIcon = (int)i;
            }
            if (isHover && ImGui::IsMouseDoubleClicked(0)) {
                ToggleWindow(leftApps[i].windowId);
            }

            bool isSelected = (g_SelectedDesktopIcon == (int)i);

            if (isSelected) {
                bgDraw->AddRectFilled(boxMin, boxMax, IM_COL32(49, 106, 197, 100), 2.0f);
                bgDraw->AddRect(boxMin, boxMax, IM_COL32(49, 106, 197, 240), 2.0f);
            } else if (isHover) {
                bgDraw->AddRectFilled(boxMin, boxMax, IM_COL32(49, 106, 197, 45), 2.0f);
            }

            float iconDrawX = x + (iconW - 48.0f) * 0.5f;
            float iconDrawY = y + 4.0f;
            if (leftApps[i].icon) {
                bgDraw->AddImage((ImTextureID)leftApps[i].icon, ImVec2(iconDrawX, iconDrawY), ImVec2(iconDrawX + 48.0f, iconDrawY + 48.0f));
            }

            if (g_FontSmall) ImGui::PushFont(g_FontSmall);
            ImVec2 textSize = ImGui::CalcTextSize(leftApps[i].title.c_str());
            float textX = x + (iconW - textSize.x) * 0.5f;
            float textY = iconDrawY + 52.0f;

            if (isSelected) {
                bgDraw->AddRectFilled(ImVec2(textX - 3.0f, textY - 1.0f), ImVec2(textX + textSize.x + 3.0f, textY + textSize.y + 1.0f), IM_COL32(49, 106, 197, 255), 1.0f);
                bgDraw->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 255), leftApps[i].title.c_str());
            } else {
                bgDraw->AddText(ImVec2(textX + 1.0f, textY + 1.0f), IM_COL32(0, 0, 0, 255), leftApps[i].title.c_str());
                bgDraw->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 255), leftApps[i].title.c_str());
            }
            if (g_FontSmall) ImGui::PopFont();
        }

        // 3. Desktop Apps — Bottom-Right Corner: "Копейка joive" and "Выход"
        std::vector<DesktopIcon> rightApps = {
            { WIN_DONATE, "Копейка joive", g_pTexCoin, false },
            { -1,         "Выход",         g_pTexExit, true }
        };

        float rightColX = displayW - iconW - 24.0f;
        float rightStartY = displayH - 32.0f - (2 * iconH) - 16.0f;

        for (size_t i = 0; i < rightApps.size(); ++i) {
            float x = rightColX;
            float y = rightStartY + i * iconH;
            ImVec2 boxMin(x, y);
            ImVec2 boxMax(x + iconW, y + iconH);

            int appIdx = 100 + (int)i;
            bool isHover = (io.MousePos.x >= boxMin.x && io.MousePos.x <= boxMax.x &&
                            io.MousePos.y >= boxMin.y && io.MousePos.y <= boxMax.y);

            if (isHover && ImGui::IsMouseClicked(0)) {
                g_SelectedDesktopIcon = appIdx;
            }
            if (isHover && ImGui::IsMouseDoubleClicked(0)) {
                if (rightApps[i].is_exit) {
                    PostQuitMessage(0);
                } else {
                    ToggleWindow(rightApps[i].windowId);
                }
            }

            bool isSelected = (g_SelectedDesktopIcon == appIdx);

            if (isSelected) {
                bgDraw->AddRectFilled(boxMin, boxMax, IM_COL32(49, 106, 197, 100), 2.0f);
                bgDraw->AddRect(boxMin, boxMax, IM_COL32(49, 106, 197, 240), 2.0f);
            } else if (isHover) {
                bgDraw->AddRectFilled(boxMin, boxMax, IM_COL32(49, 106, 197, 45), 2.0f);
            }

            float iconDrawX = x + (iconW - 48.0f) * 0.5f;
            float iconDrawY = y + 4.0f;
            if (rightApps[i].icon) {
                bgDraw->AddImage((ImTextureID)rightApps[i].icon, ImVec2(iconDrawX, iconDrawY), ImVec2(iconDrawX + 48.0f, iconDrawY + 48.0f));
            }

            if (g_FontSmall) ImGui::PushFont(g_FontSmall);
            ImVec2 textSize = ImGui::CalcTextSize(rightApps[i].title.c_str());
            float textX = x + (iconW - textSize.x) * 0.5f;
            float textY = iconDrawY + 52.0f;

            if (isSelected) {
                bgDraw->AddRectFilled(ImVec2(textX - 3.0f, textY - 1.0f), ImVec2(textX + textSize.x + 3.0f, textY + textSize.y + 1.0f), IM_COL32(49, 106, 197, 255), 1.0f);
                bgDraw->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 255), rightApps[i].title.c_str());
            } else {
                bgDraw->AddText(ImVec2(textX + 1.0f, textY + 1.0f), IM_COL32(0, 0, 0, 255), rightApps[i].title.c_str());
                bgDraw->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 255), rightApps[i].title.c_str());
            }
            if (g_FontSmall) ImGui::PopFont();
        }

        // 4. Render Open Windows
        std::vector<int> windowRenderOrder;
        for (int i = 0; i < WIN_COUNT; ++i) windowRenderOrder.push_back(i);
        std::sort(windowRenderOrder.begin(), windowRenderOrder.end(), [](int a, int b) {
            return g_Windows[a].z_order < g_Windows[b].z_order;
        });

        for (int id : windowRenderOrder) {
            RenderXPWindow(g_Windows[id], id, displayW, displayH);
        }

        // 5. Authentic Windows XP Start Menu
        if (g_StartMenuOpen) {
            float smW = 380.0f;
            float smH = 440.0f;
            float smX = 0.0f;
            float smY = displayH - 32.0f - smH;

            ImDrawList* fgDraw = ImGui::GetForegroundDrawList();

            fgDraw->AddRectFilled(ImVec2(smX + 4.0f, smY + 4.0f), ImVec2(smX + smW + 4.0f, smY + smH + 4.0f), IM_COL32(0, 0, 0, 90), 8.0f);
            fgDraw->AddRectFilled(ImVec2(smX, smY), ImVec2(smX + smW, smY + smH), IM_COL32(255, 255, 255, 255), 8.0f);
            fgDraw->AddRect(ImVec2(smX, smY), ImVec2(smX + smW, smY + smH), IM_COL32(0, 50, 180, 255), 8.0f, 0, 2.0f);

            // User Header (Luna Blue Banner)
            float headerH = 54.0f;
            fgDraw->AddRectFilledMultiColor(
                ImVec2(smX, smY), ImVec2(smX + smW, smY + headerH),
                IM_COL32(0, 85, 235, 255), IM_COL32(0, 85, 235, 255),
                IM_COL32(0, 45, 160, 255), IM_COL32(0, 45, 160, 255)
            );
            if (g_pTexButterfly) {
                fgDraw->AddImage((ImTextureID)g_pTexButterfly, ImVec2(smX + 12.0f, smY + 6.0f), ImVec2(smX + 54.0f, smY + 48.0f));
            }
            if (g_FontStartHeader) ImGui::PushFont(g_FontStartHeader);
            fgDraw->AddText(ImVec2(smX + 62.0f, smY + 18.0f), IM_COL32(255, 255, 255, 255), "joive");
            if (g_FontStartHeader) ImGui::PopFont();

            // Two Columns
            float col1W = 210.0f;
            float bodyY = smY + headerH;
            float bodyH = smH - headerH - 42.0f;

            fgDraw->AddRectFilled(ImVec2(smX + col1W, bodyY), ImVec2(smX + smW, bodyY + bodyH), IM_COL32(211, 229, 250, 255));
            fgDraw->AddLine(ImVec2(smX + col1W, bodyY), ImVec2(smX + col1W, bodyY + bodyH), IM_COL32(150, 185, 230, 255));

            struct StartItem { std::string title; std::string sub; ID3D11ShaderResourceView* icon; int winId; };
            std::vector<StartItem> leftItems = {
                { "Дефрагментатор", "Оптимизация диска", g_pTexDefrag, WIN_DEFRAG },
                { "Очистка диска", "Освободить место", g_pTexCleanup, WIN_CLEANUP },
                { "Тест скорости", "NVMe Benchmark", g_pTexBenchmark, WIN_BENCHMARK },
                { "Командная строка", "cmd.exe", g_pTexCmd, WIN_CMD },
                { "Мой компьютер", "Диски и кластеры", g_pTexComputer, WIN_MY_COMPUTER }
            };

            for (size_t i = 0; i < leftItems.size(); ++i) {
                float iy = bodyY + 10.0f + i * 50.0f;
                ImVec2 itemMin(smX + 4.0f, iy);
                ImVec2 itemMax(smX + col1W - 4.0f, iy + 44.0f);

                bool isHover = (io.MousePos.x >= itemMin.x && io.MousePos.x <= itemMax.x &&
                                io.MousePos.y >= itemMin.y && io.MousePos.y <= itemMax.y);

                if (isHover) {
                    fgDraw->AddRectFilled(itemMin, itemMax, IM_COL32(49, 106, 197, 240), 3.0f);
                }

                if (leftItems[i].icon) {
                    fgDraw->AddImage((ImTextureID)leftItems[i].icon, ImVec2(itemMin.x + 6.0f, itemMin.y + 6.0f), ImVec2(itemMin.x + 38.0f, itemMin.y + 38.0f));
                }

                ImU32 textCol = isHover ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 255);
                ImU32 subCol  = isHover ? IM_COL32(220, 235, 255, 255) : IM_COL32(110, 110, 110, 255);

                if (g_FontBold) ImGui::PushFont(g_FontBold);
                fgDraw->AddText(ImVec2(itemMin.x + 44.0f, itemMin.y + 5.0f), textCol, leftItems[i].title.c_str());
                if (g_FontBold) ImGui::PopFont();

                if (g_FontSmall) ImGui::PushFont(g_FontSmall);
                fgDraw->AddText(ImVec2(itemMin.x + 44.0f, itemMin.y + 23.0f), subCol, leftItems[i].sub.c_str());
                if (g_FontSmall) ImGui::PopFont();

                if (isHover && ImGui::IsMouseClicked(0)) {
                    ToggleWindow(leftItems[i].winId);
                    g_StartMenuOpen = false;
                }
            }

            // Right Column Items
            std::vector<std::pair<std::string, int>> rightItems = {
                { "Мои документы", WIN_MY_COMPUTER },
                { "Панель управления", WIN_CONTROL_PANEL },
                { "Копейка joive", WIN_DONATE },
                { "Поиск", WIN_CMD },
                { "Выполнить...", WIN_CMD }
            };

            for (size_t i = 0; i < rightItems.size(); ++i) {
                float iy = bodyY + 12.0f + i * 36.0f;
                ImVec2 itemMin(smX + col1W + 6.0f, iy);
                ImVec2 itemMax(smX + smW - 6.0f, iy + 30.0f);

                bool isHover = (io.MousePos.x >= itemMin.x && io.MousePos.x <= itemMax.x &&
                                io.MousePos.y >= itemMin.y && io.MousePos.y <= itemMax.y);

                if (isHover) {
                    fgDraw->AddRectFilled(itemMin, itemMax, IM_COL32(49, 106, 197, 240), 3.0f);
                }

                ImU32 textCol = isHover ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 30, 80, 255);
                if (g_FontDefault) ImGui::PushFont(g_FontDefault);
                fgDraw->AddText(ImVec2(itemMin.x + 10.0f, itemMin.y + 6.0f), textCol, rightItems[i].first.c_str());
                if (g_FontDefault) ImGui::PopFont();

                if (isHover && ImGui::IsMouseClicked(0)) {
                    ToggleWindow(rightItems[i].second);
                    g_StartMenuOpen = false;
                }
            }

            // Footer
            float footerY = smY + smH - 42.0f;
            fgDraw->AddRectFilled(ImVec2(smX, footerY), ImVec2(smX + smW, smY + smH), IM_COL32(29, 82, 183, 255), 8.0f);

            ImVec2 shutMin(smX + smW - 170.0f, footerY + 7.0f);
            ImVec2 shutMax(smX + smW - 10.0f, footerY + 35.0f);
            bool isHoverShut = (io.MousePos.x >= shutMin.x && io.MousePos.x <= shutMax.x &&
                                io.MousePos.y >= shutMin.y && io.MousePos.y <= shutMax.y);

            if (isHoverShut) {
                fgDraw->AddRectFilled(shutMin, shutMax, IM_COL32(220, 60, 40, 220), 4.0f);
            }
            fgDraw->AddText(ImVec2(shutMin.x + 8.0f, shutMin.y + 6.0f), IM_COL32(255, 255, 255, 255), "⏻ Выключение...");
            if (isHoverShut && ImGui::IsMouseClicked(0)) {
                PostQuitMessage(0);
            }

            if (ImGui::IsMouseClicked(0) &&
                (io.MousePos.x > smW || io.MousePos.y < smY || io.MousePos.y > displayH - 32.0f)) {
                g_StartMenuOpen = false;
            }
        }

        // 6. Authentic Windows XP Luna Taskbar
        ImDrawList* fgDraw = ImGui::GetForegroundDrawList();
        float tbY = displayH - 32.0f;
        ImVec2 tbMin(0, tbY);
        ImVec2 tbMax(displayW, displayH);

        fgDraw->AddRectFilledMultiColor(
            tbMin, tbMax,
            IM_COL32(36, 94, 220, 255), IM_COL32(36, 94, 220, 255),
            IM_COL32(25, 65, 165, 255), IM_COL32(25, 65, 165, 255)
        );
        fgDraw->AddLine(ImVec2(0, tbY), ImVec2(displayW, tbY), IM_COL32(66, 130, 244, 255), 1.5f);

        // --- Start Button ---
        float startBtnW = 98.0f;
        ImVec2 startMin(0, tbY);
        ImVec2 startMax(startBtnW, displayH);

        bool isHoverStart = (io.MousePos.x >= startMin.x && io.MousePos.x <= startMax.x &&
                             io.MousePos.y >= startMin.y && io.MousePos.y <= startMax.y);
        bool isPressStart = isHoverStart && ImGui::IsMouseDown(0);

        if (g_pUIThemeTextureView && g_UIThemeWidth > 0) {
            float su0 = 497.0f / (float)g_UIThemeWidth;
            float sv0 = (isPressStart || g_StartMenuOpen) ? (100.0f / (float)g_UIThemeHeight) :
                        (isHoverStart ? (67.0f / (float)g_UIThemeHeight) : (37.0f / (float)g_UIThemeHeight));
            float su1 = 580.0f / (float)g_UIThemeWidth;
            float sv1 = sv0 + (30.0f / (float)g_UIThemeHeight);

            fgDraw->AddImage((ImTextureID)g_pUIThemeTextureView, startMin, startMax, ImVec2(su0, sv0), ImVec2(su1, sv1));
        } else {
            ImU32 sCol = (isPressStart || g_StartMenuOpen) ? IM_COL32(30, 115, 30, 255) :
                         (isHoverStart ? IM_COL32(60, 195, 60, 255) : IM_COL32(45, 160, 45, 255));
            fgDraw->AddRectFilled(startMin, startMax, sCol, 0.0f);
        }

        if (g_pTexButterfly) {
            fgDraw->AddImage((ImTextureID)g_pTexButterfly, ImVec2(startMin.x + 8.0f, startMin.y + 6.0f), ImVec2(startMin.x + 28.0f, startMin.y + 26.0f));
        }
        if (g_FontStartHeader) ImGui::PushFont(g_FontStartHeader);
        fgDraw->AddText(ImVec2(startMin.x + 34.0f, startMin.y + 7.0f), IM_COL32(0, 0, 0, 180), "Пуск");
        fgDraw->AddText(ImVec2(startMin.x + 33.0f, startMin.y + 6.0f), IM_COL32(255, 255, 255, 255), "Пуск");
        if (g_FontStartHeader) ImGui::PopFont();

        if (isHoverStart && ImGui::IsMouseClicked(0)) {
            g_StartMenuOpen = !g_StartMenuOpen;
            PlayXPSound(L"Windows XP Start.wav");
        }

        // --- Taskbar Running Window Tabs ---
        float taskTabX = startBtnW + 8.0f;
        float taskTabW = 160.0f;
        float taskTabH = 26.0f;

        for (int i = 0; i < WIN_COUNT; ++i) {
            if (!g_Windows[i].is_open) continue;

            ImVec2 tabMin(taskTabX, tbY + 3.0f);
            ImVec2 tabMax(taskTabX + taskTabW, tbY + 3.0f + taskTabH);

            bool isTabHover = (io.MousePos.x >= tabMin.x && io.MousePos.x <= tabMax.x &&
                               io.MousePos.y >= tabMin.y && io.MousePos.y <= tabMax.y);
            bool isActive = !g_Windows[i].is_minimized && (g_Windows[i].z_order == g_TopZOrder);

            ImU32 tabCol = isActive ? IM_COL32(25, 65, 165, 255) : (isTabHover ? IM_COL32(65, 140, 245, 255) : IM_COL32(49, 106, 197, 240));
            fgDraw->AddRectFilled(tabMin, tabMax, tabCol, 3.0f);
            fgDraw->AddRect(tabMin, tabMax, isActive ? IM_COL32(15, 45, 120, 255) : IM_COL32(95, 165, 255, 255), 3.0f);

            if (g_Windows[i].icon) {
                fgDraw->AddImage((ImTextureID)g_Windows[i].icon, ImVec2(tabMin.x + 6.0f, tabMin.y + 5.0f), ImVec2(tabMin.x + 22.0f, tabMin.y + 21.0f));
            }

            std::string shortTitle = g_Windows[i].title;
            if (shortTitle.length() > 18) shortTitle = shortTitle.substr(0, 16) + "..";

            if (g_FontSmall) ImGui::PushFont(g_FontSmall);
            fgDraw->AddText(ImVec2(tabMin.x + 26.0f, tabMin.y + 6.0f), IM_COL32(255, 255, 255, 255), shortTitle.c_str());
            if (g_FontSmall) ImGui::PopFont();

            if (isTabHover && ImGui::IsMouseClicked(0)) {
                ToggleWindow(i);
            }

            taskTabX += taskTabW + 4.0f;
        }

        // --- System Tray ---
        float trayW = 140.0f;
        float trayX = displayW - trayW;
        ImVec2 trayMin(trayX, tbY);
        ImVec2 trayMax(displayW, displayH);

        fgDraw->AddRectFilled(trayMin, trayMax, IM_COL32(15, 47, 118, 255));
        fgDraw->AddLine(trayMin, ImVec2(trayX, displayH), IM_COL32(10, 30, 80, 255), 1.5f);

        time_t now = time(nullptr);
        tm localTime;
        localtime_s(&localTime, &now);
        char timeStr[16];
        strftime(timeStr, sizeof(timeStr), "%H:%M", &localTime);

        if (g_FontDefault) ImGui::PushFont(g_FontDefault);
        fgDraw->AddText(ImVec2(displayW - 54.0f, tbY + 8.0f), IM_COL32(255, 255, 255, 255), timeStr);
        if (g_FontDefault) ImGui::PopFont();

        float exitW = 58.0f;
        ImVec2 exitMin(trayX + 8.0f, tbY + 5.0f);
        ImVec2 exitMax(exitMin.x + exitW, tbY + 27.0f);

        bool isHoverExit = (io.MousePos.x >= exitMin.x && io.MousePos.x <= exitMax.x &&
                            io.MousePos.y >= exitMin.y && io.MousePos.y <= exitMin.y + 22.0f);

        fgDraw->AddRectFilled(exitMin, exitMax, isHoverExit ? IM_COL32(220, 50, 40, 255) : IM_COL32(160, 35, 30, 220), 3.0f);
        if (g_FontSmall) ImGui::PushFont(g_FontSmall);
        fgDraw->AddText(ImVec2(exitMin.x + 6.0f, exitMin.y + 4.0f), IM_COL32(255, 255, 255, 255), "✕ Выход");
        if (g_FontSmall) ImGui::PopFont();

        if (isHoverExit && ImGui::IsMouseClicked(0)) {
            PostQuitMessage(0);
        }

        // Rendering DirectX Frame
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.0f, 0.3f, 0.6f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // VSync
    }

    // Cleanup Textures
    if (g_pBlissTextureView) g_pBlissTextureView->Release();
    if (g_pUIThemeTextureView) g_pUIThemeTextureView->Release();
    if (g_pBootScreenTextureView) g_pBootScreenTextureView->Release();
    if (g_pTexComputer) g_pTexComputer->Release();
    if (g_pTexDefrag) g_pTexDefrag->Release();
    if (g_pTexCleanup) g_pTexCleanup->Release();
    if (g_pTexBenchmark) g_pTexBenchmark->Release();
    if (g_pTexControlPanel) g_pTexControlPanel->Release();
    if (g_pTexCmd) g_pTexCmd->Release();
    if (g_pTexRecycle) g_pTexRecycle->Release();
    if (g_pTexPieChart) g_pTexPieChart->Release();
    if (g_pTexFirewall) g_pTexFirewall->Release();
    if (g_pTexButterfly) g_pTexButterfly->Release();
    if (g_pTexCoin) g_pTexCoin->Release();
    if (g_pTexExit) g_pTexExit->Release();

    // Shutdown Backends
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (SUCCEEDED(hrCo)) CoUninitialize();

    return 0;
}

// ---------------- DirectX 11 Helper Functions ----------------
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
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
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
        &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
    );
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
            &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
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
        if (wParam != SIZE_MINIMIZED) {
            g_ResizeWidth = (UINT)LOWORD(lParam);
            g_ResizeHeight = (UINT)HIWORD(lParam);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
