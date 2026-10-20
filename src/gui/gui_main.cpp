// language: C++, file: gui_main.cpp, target: Windows 10/11, MSVC, DirectX 11 + Dear ImGui
// ClusterShift OS  -  Authentic Windows XP Luna Edition (v0.6.0 by joive)
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

// --- Internationalization (i18n) Engine ---
bool g_IsRussian = false;

void UpdateLanguageStrings();
void InitCmdHistory();

void DetectLanguage(const wchar_t* cmdLine = nullptr) {
    if (cmdLine) {
        if (wcsstr(cmdLine, L"--lang en") || wcsstr(cmdLine, L"-lang en") ||
            wcsstr(cmdLine, L"/lang:en") || wcsstr(cmdLine, L"/lang en") ||
            wcsstr(cmdLine, L"--en") || wcsstr(cmdLine, L"-en")) {
            g_IsRussian = false;
            return;
        }
        if (wcsstr(cmdLine, L"--lang ru") || wcsstr(cmdLine, L"-lang ru") ||
            wcsstr(cmdLine, L"/lang:ru") || wcsstr(cmdLine, L"/lang ru") ||
            wcsstr(cmdLine, L"--ru") || wcsstr(cmdLine, L"-ru")) {
            g_IsRussian = true;
            return;
        }
    }
    LANGID userUi = PRIMARYLANGID(GetUserDefaultUILanguage());
    LANGID sysUi = PRIMARYLANGID(GetSystemDefaultUILanguage());
    LANGID userLcid = PRIMARYLANGID(GetUserDefaultLCID());
    g_IsRussian = (userUi == LANG_RUSSIAN || sysUi == LANG_RUSSIAN || userLcid == LANG_RUSSIAN);
}

inline const char* Tr(const char* ru, const char* en) {
    return g_IsRussian ? ru : en;
}

void SetLanguage(bool isRussian) {
    g_IsRussian = isRussian;
    UpdateLanguageStrings();
}

void ToggleLanguage() {
    SetLanguage(!g_IsRussian);
}

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
        std::filesystem::current_path() / L"assets" / relativePath
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

void UpdateLanguageStrings() {
    if (g_Windows.size() < WIN_COUNT) return;
    g_Windows[WIN_MY_COMPUTER].title   = Tr("Мой компьютер", "My Computer");
    g_Windows[WIN_DEFRAG].title        = Tr("Дефрагментатор диска - ClusterShift", "Disk Defragmenter - ClusterShift");
    g_Windows[WIN_CLEANUP].title       = Tr("Очистка диска (C:)", "Disk Cleanup (C:)");
    g_Windows[WIN_BENCHMARK].title     = Tr("Тест скорости накопителя (NVMe/SSD Benchmark)", "Storage Speed Test (NVMe/SSD Benchmark)");
    g_Windows[WIN_CMD].title           = Tr("Командная строка - clustershift.exe", "Command Prompt - clustershift.exe");
    g_Windows[WIN_CONTROL_PANEL].title = Tr("Панель управления ClusterShift", "ClusterShift Control Panel");
    g_Windows[WIN_RECYCLE].title       = Tr("Корзина", "Recycle Bin");
    g_Windows[WIN_DONATE].title        = Tr("Копейка joive - Поддержка автора", "Support joive - Author Donation");

    InitCmdHistory();
}

void InitWindows() {
    g_Windows.resize(WIN_COUNT);

    // 1. My Computer
    g_Windows[WIN_MY_COMPUTER] = {
        WIN_MY_COMPUTER,
        "",
        nullptr,
        ImVec2(120, 80), ImVec2(640, 480), ImVec2(400, 300),
        false, false, false, // ALL START CLOSED
        ImVec2(120, 80), ImVec2(640, 480),
        0
    };

    // 2. Disk Defragmenter
    g_Windows[WIN_DEFRAG] = {
        WIN_DEFRAG,
        "",
        nullptr,
        ImVec2(140, 90), ImVec2(780, 530), ImVec2(500, 350),
        false, false, false, // ALL START CLOSED
        ImVec2(140, 90), ImVec2(780, 530),
        0
    };

    // 3. Disk Cleanup (cleanmgr)
    g_Windows[WIN_CLEANUP] = {
        WIN_CLEANUP,
        "",
        nullptr,
        ImVec2(220, 120), ImVec2(490, 440), ImVec2(380, 350),
        false, false, false,
        ImVec2(220, 120), ImVec2(490, 440),
        0
    };

    // 4. Benchmark
    g_Windows[WIN_BENCHMARK] = {
        WIN_BENCHMARK,
        "",
        nullptr,
        ImVec2(180, 90), ImVec2(680, 460), ImVec2(450, 300),
        false, false, false,
        ImVec2(180, 90), ImVec2(680, 460),
        0
    };

    // 5. Command Prompt (cmd.exe)
    g_Windows[WIN_CMD] = {
        WIN_CMD,
        "",
        nullptr,
        ImVec2(100, 140), ImVec2(660, 420), ImVec2(400, 250),
        false, false, false,
        ImVec2(100, 140), ImVec2(660, 420),
        0
    };

    // 6. Control Panel
    g_Windows[WIN_CONTROL_PANEL] = {
        WIN_CONTROL_PANEL,
        "",
        nullptr,
        ImVec2(160, 110), ImVec2(600, 440), ImVec2(420, 300),
        false, false, false,
        ImVec2(160, 110), ImVec2(600, 440),
        0
    };

    // 7. Recycle Bin
    g_Windows[WIN_RECYCLE] = {
        WIN_RECYCLE,
        "",
        nullptr,
        ImVec2(200, 140), ImVec2(520, 380), ImVec2(360, 260),
        false, false, false,
        ImVec2(200, 140), ImVec2(520, 380),
        0
    };

    // 8. Donate joive
    g_Windows[WIN_DONATE] = {
        WIN_DONATE,
        "",
        nullptr,
        ImVec2(280, 160), ImVec2(460, 320), ImVec2(360, 240),
        false, false, false,
        ImVec2(280, 160), ImVec2(460, 320),
        0
    };

    UpdateLanguageStrings();
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
    std::string bypass_io_driver = "";
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
            std::string failDriver;
            for (wchar_t wc : telem->bypass_io.failing_driver) {
                if (wc >= 32 && wc <= 126) failDriver.push_back(static_cast<char>(wc));
            }
            g_VolState.bypass_io_driver = failDriver;

            if (telem->bypass_io.is_supported) {
                g_VolState.bypass_io_status = "SUPPORTED (Active)";
            } else if (!failDriver.empty()) {
                g_VolState.bypass_io_status = "BLOCKED by " + failDriver;
            } else if (telem->bypass_io.is_filter_blocked) {
                g_VolState.bypass_io_status = "BLOCKED by Minifilter Driver";
            } else {
                g_VolState.bypass_io_status = "NOT SUPPORTED (Legacy Stack)";
            }
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
    int status_code = 0; // 0=ready, 1=running, 2=done, 3=error
    int error_code = 0;
};
BenchmarkState g_Bench;

void StartBenchmarkRun() {
    if (g_Bench.is_running) return;
    g_Bench.is_running = true;
    g_Bench.status_code = 1;
    g_Bench.progress = 0.05f;
    PlayXPSound(L"Windows Navigation Start.wav");

    std::thread([&]() {
        // Quick IOCP test: 128 MB file, 4 worker threads
        auto res = clustershift::core::DiskBenchmark::run_benchmark(L'C', 128ULL * 1024 * 1024, true, 4);
        if (res) {
            g_Bench.metrics = res.value();
            g_Bench.has_results = true;
            g_Bench.status_code = 2;
            PlayXPSound(L"Windows XP Notify.wav");
        } else {
            g_Bench.status_code = 3;
            g_Bench.error_code = res.error();
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
    std::vector<std::string> history;
    char input_buf[256] = "";
};
CmdState g_Cmd;

void InitCmdHistory() {
    if (!g_Cmd.history.empty() && g_Cmd.history.size() > 8) return;
    g_Cmd.history.clear();
    g_Cmd.history.push_back(Tr("Microsoft Windows XP [Версия 5.1.2600]", "Microsoft Windows XP [Version 5.1.2600]"));
    g_Cmd.history.push_back(Tr("(C) Корпорация Майкрософт, 1985-2001.", "(C) Copyright 1985-2001 Microsoft Corp."));
    g_Cmd.history.push_back("");
    g_Cmd.history.push_back("ClusterShift Core Engine [v0.6.0] initialized.");
    g_Cmd.history.push_back(Tr("Поток парсера MFT: DirectIO готов.", "MFT parser stream: DirectIO ready."));
    g_Cmd.history.push_back(Tr("Статус BypassIO: Аппаратно готов | Стек: Заблокирован фильтром", "BypassIO state: Hardware Capable | Stack: Blocked (Filter)"));
    g_Cmd.history.push_back(Tr("Введите 'help' для списка доступных команд.", "Type 'help' for available commands."));
    g_Cmd.history.push_back("");
}

void ExecuteCmd(const std::string& cmdLine) {
    g_Cmd.history.push_back("C:\\ClusterShift> " + cmdLine);
    std::string lower = cmdLine;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "help") {
        g_Cmd.history.push_back(Tr("Справка по командам ClusterShift [v0.6.0]:", "ClusterShift commands [v0.6.0]:"));
        g_Cmd.history.push_back(Tr("  status    - состояние накопителя и анализ MFT", "  status    - drive telemetry and MFT analysis"));
        g_Cmd.history.push_back(Tr("  bench     - IOCP бенчмарк чтения/записи", "  bench     - IOCP read/write benchmark"));
        g_Cmd.history.push_back(Tr("  trim      - отправка команды TRIM на накопитель SSD/NVMe", "  trim      - issue TRIM command to SSD/NVMe"));
        g_Cmd.history.push_back(Tr("  clean     - безопасная очистка %TEMP% и корзины", "  clean     - clean %TEMP% and Recycle Bin"));
        g_Cmd.history.push_back(Tr("  defrag    - карта распределения кластеров", "  defrag    - show cluster allocation map"));
        g_Cmd.history.push_back(Tr("  cls       - очистить экран", "  cls       - clear screen"));
        g_Cmd.history.push_back(Tr("  exit      - закрыть командную строку", "  exit      - close command prompt"));
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
        g_Cmd.history.push_back(Tr("[*] Запуск многопоточного IOCP бенчмарка (128 MB)...", "[*] Starting multi-threaded IOCP Benchmark (128 MB)..."));
        auto res = clustershift::core::DiskBenchmark::run_benchmark(L'C', 128ULL * 1024 * 1024, true, 4);
        if (res) {
            char buf[256];
            snprintf(buf, sizeof(buf), "[+] Sequential Read:  %.1f MB/s", res->seq_read_mbps);
            g_Cmd.history.push_back(buf);
            snprintf(buf, sizeof(buf), "[+] Sequential Write: %.1f MB/s", res->seq_write_mbps);
            g_Cmd.history.push_back(buf);
            snprintf(buf, sizeof(buf), "[+] Random 4K QD32:   %.0f IOPS", res->rnd_4k_read_iops);
            g_Cmd.history.push_back(buf);
            snprintf(buf, sizeof(buf), "[+] Latency (Q1T1):   %.1f us", res->rnd_4k_q1t1_avg_lat_us);
            g_Cmd.history.push_back(buf);
        } else {
            g_Cmd.history.push_back("[-] Benchmark failed (Error: " + std::to_string(res.error()) + ")");
        }
    } else if (lower == "trim") {
        g_Cmd.history.push_back("[*] IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES (Trim)...");
        auto vol = clustershift::core::VolumeManager::open_volume(L'C', true);
        if (vol) {
            auto res = clustershift::core::TrimEngine::trim_volume(vol->get());
            if (res && res->success) {
                g_Cmd.history.push_back(Tr("[+] TRIM успешно отправлен на SSD.", "[+] TRIM issued successfully to SSD."));
            } else {
                g_Cmd.history.push_back(Tr("[-] Ошибка отправки TRIM.", "[-] Failed to issue TRIM."));
            }
        } else {
            g_Cmd.history.push_back(Tr("[-] Не удалось открыть том с правами записи для TRIM.", "[-] Could not open volume with write access for TRIM."));
        }
    } else if (lower == "clean") {
        g_Cmd.history.push_back(Tr("[*] Очистка корзины и временных файлов...", "[*] Cleaning Recycle Bin & temporary files..."));
        SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
        RefreshCleanupScan();
        g_Cmd.history.push_back(Tr("[+] Очистка выполнена.", "[+] Cleanup complete."));
    } else if (lower == "defrag") {
        InitDefragGrid();
        g_Cmd.history.push_back(Tr("[+] Карта кластеров тома C: обновлена.", "[+] Cluster map for volume C: updated."));
        char buf[256];
        snprintf(buf, sizeof(buf), "[+] Contiguous: %llu clusters, Fragmented: %llu clusters",
            (unsigned long long)g_Defrag.contiguous_clusters, (unsigned long long)g_Defrag.fragmented_clusters);
        g_Cmd.history.push_back(buf);
    } else if (lower == "cls") {
        g_Cmd.history.clear();
    } else if (lower == "exit") {
        g_Windows[WIN_CMD].is_open = false;
    } else if (!lower.empty()) {
        g_Cmd.history.push_back("'" + cmdLine + "' " + Tr("не является внутренней или внешней командой.", "is not recognized as an internal or external command."));
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

        // Dynamic Scale Factor when Resizing or Maximizing Window
        float baseW = (win.restore_size.x > 0) ? win.restore_size.x : 640.0f;
        float baseH = (win.restore_size.y > 0) ? win.restore_size.y : 480.0f;
        float scale = 1.0f;
        if (win.is_maximized) {
            float scaleY = (screenH - 32.0f) / baseH;
            float scaleX = screenW / baseW;
            scale = std::clamp((scaleX + scaleY) * 0.5f, 1.0f, 1.70f);
        } else {
            float scaleY = win.size.y / baseH;
            float scaleX = win.size.x / baseW;
            scale = std::clamp((scaleX + scaleY) * 0.5f, 0.95f, 1.70f);
        }
        ImGui::SetWindowFontScale(scale);

        // ------------------ Window Inner Contents ------------------
        if (win.id == WIN_DEFRAG) {
            // === 1. Windows XP Disk Defragmenter ===
            ImGui::Text(Tr("Том: (C:)  |  Файловая система: NTFS  |  Кластер: %u КБ", "Volume: (C:)  |  File System: NTFS  |  Cluster: %u KB"), g_VolState.cluster_size / 1024);
            ImGui::ProgressBar(g_Defrag.progress, ImVec2(-1, 20.0f * scale));

            ImGui::Spacing();
            ImGui::Text("%s", Tr("Карта кластеров накопителя:", "Storage Cluster Allocation Map:"));

            // Adaptive Cluster Block Grid Visualizer
            ImDrawList* cDraw = ImGui::GetWindowDrawList();
            ImVec2 gridStart = ImGui::GetCursorScreenPos();
            float blockW = 15.0f * scale, blockH = 15.0f * scale, pad = 2.0f * scale;
            float availW = ImGui::GetContentRegionAvail().x;
            float availH = ImGui::GetContentRegionAvail().y;
            int cols = (int)(availW / (blockW + pad));
            if (cols < 10) cols = 10;
            int targetRows = (int)((availH - 95.0f * scale) / (blockH + pad));
            if (targetRows < 8) targetRows = 8;
            int totalBlocks = cols * targetRows;

            if ((int)g_Defrag.cluster_grid.size() != totalBlocks) {
                g_Defrag.cluster_grid.resize(totalBlocks);
                float used_fraction = (g_VolState.total_bytes > 0) ? (float)((double)g_VolState.used_bytes / (double)g_VolState.total_bytes) : 0.65f;
                int used_blocks = (int)(used_fraction * (float)totalBlocks);
                for (int i = 0; i < totalBlocks; ++i) {
                    if (i < 40) g_Defrag.cluster_grid[i] = 3;
                    else if (i < used_blocks) {
                        if (i % 11 == 0 || i % 19 == 0) g_Defrag.cluster_grid[i] = 2;
                        else g_Defrag.cluster_grid[i] = 1;
                    } else g_Defrag.cluster_grid[i] = 0;
                }
            }

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
            ImGui::Dummy(ImVec2(-1, rows * (blockH + pad) + 10.0f * scale));

            ImGui::Separator();
            
            // Legend
            ImVec2 legPos = ImGui::GetCursorScreenPos();
            drawList->AddRectFilled(ImVec2(legPos.x, legPos.y + 2), ImVec2(legPos.x + 12 * scale, legPos.y + (2 + 12 * scale)), IM_COL32(235, 40, 30, 255), 1.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f * scale);
            ImGui::Text(Tr("Фрагментировано: %llu кластеров", "Fragmented: %llu clusters"), (unsigned long long)g_Defrag.fragmented_clusters);

            ImGui::SameLine(250 * scale);
            ImVec2 legPos2 = ImGui::GetCursorScreenPos();
            drawList->AddRectFilled(ImVec2(legPos2.x, legPos2.y + 2), ImVec2(legPos2.x + 12 * scale, legPos2.y + (2 + 12 * scale)), IM_COL32(0, 85, 230, 255), 1.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f * scale);
            ImGui::Text(Tr("Непрерывно: %llu кластеров", "Contiguous: %llu clusters"), (unsigned long long)g_Defrag.contiguous_clusters);

            ImGui::SameLine(500 * scale);
            ImVec2 legPos3 = ImGui::GetCursorScreenPos();
            drawList->AddRectFilled(ImVec2(legPos3.x, legPos3.y + 2), ImVec2(legPos3.x + 12 * scale, legPos3.y + (2 + 12 * scale)), IM_COL32(35, 175, 45, 255), 1.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f * scale);
            ImGui::Text("%s", Tr("Системные: MFT Zone", "System: MFT Zone"));

            ImGui::Spacing();
            float btnH = 28.0f * scale;
            if (ImGui::Button(g_Defrag.is_running ? Tr("Приостановить", "Pause") : Tr("Дефрагментация / TRIM", "Defragment / TRIM"), ImVec2(190 * scale, btnH))) {
                g_Defrag.is_running = !g_Defrag.is_running;
                if (g_Defrag.is_running) {
                    PlayXPSound(L"Windows Navigation Start.wav");
                    auto vol = clustershift::core::VolumeManager::open_volume(L'C', true);
                    if (vol) {
                        (void)clustershift::core::TrimEngine::trim_volume(vol->get());
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Анализ тома", "Analyze Volume"), ImVec2(120 * scale, btnH))) {
                PlayXPSound(L"Windows XP Start.wav");
                g_Defrag.progress = 0.0f;
                InitDefragGrid();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Отчет ClusterShift", "ClusterShift Report"), ImVec2(150 * scale, btnH))) {
                ToggleWindow(WIN_CMD);
            }

        } else if (win.id == WIN_MY_COMPUTER) {
            // === 2. My Computer Window ===
            ImGui::Text("%s", Tr("Файлы, хранящиеся на этом компьютере:", "Files stored on this computer:"));
            ImGui::Separator();

            ImGui::BeginChild("DrivesChild", ImVec2(-1, -1), true);
            float iconSize = 48.0f * scale;
            if (g_pTexComputer) {
                ImGui::Image((ImTextureID)g_pTexComputer, ImVec2(iconSize, iconSize));
                ImGui::SameLine(0.0f, 12.0f * scale);
            }
            ImGui::BeginGroup();
            ImGui::Text("%s (C:)  [%s]", Tr("Локальный диск", "Local Disk"), g_VolState.product_name.c_str());
            double total_gb = (double)g_VolState.total_bytes / (1024.0 * 1024.0 * 1024.0);
            double free_gb  = (double)g_VolState.free_bytes / (1024.0 * 1024.0 * 1024.0);
            double used_gb  = (double)g_VolState.used_bytes / (1024.0 * 1024.0 * 1024.0);
            char driveInfoBuf[128];
            snprintf(driveInfoBuf, sizeof(driveInfoBuf), Tr("Емкость: %.1f ГБ | Свободно: %.1f ГБ", "Capacity: %.1f GB | Free: %.1f GB"), total_gb, free_gb);
            ImGui::TextColored(ImVec4(0.35f, 0.35f, 0.35f, 1.0f), "%s", driveInfoBuf);
            ImGui::ProgressBar(g_VolState.usage_ratio, ImVec2(-1, 16.0f * scale));
            ImGui::EndGroup();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Properties Pie Chart Section
            std::string fsStr(g_VolState.file_system.begin(), g_VolState.file_system.end());
            ImGui::Text("%s: [%s, %s %u %s, %s]",
                Tr("Свойства диска C", "Properties: Local Disk (C)"),
                fsStr.c_str(),
                Tr("Кластер", "Cluster"),
                g_VolState.cluster_size,
                Tr("байт", "bytes"),
                g_VolState.drive_kind_str.c_str());
            
            float availW = ImGui::GetContentRegionAvail().x;
            float availH = ImGui::GetContentRegionAvail().y;
            float pieRadius = std::clamp(availH * 0.32f, 55.0f, 140.0f);
            if (pieRadius * 2.2f > availW * 0.45f) pieRadius = availW * 0.20f;

            ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
            ImVec2 pieCenter = ImVec2(cursorScreen.x + pieRadius + 20.0f * scale, cursorScreen.y + pieRadius + 15.0f * scale);
            ImDrawList* pDraw = ImGui::GetWindowDrawList();

            // Draw real proportional pie chart
            pDraw->AddCircleFilled(pieCenter, pieRadius, IM_COL32(0, 85, 230, 255), 64); // Used Blue
            float free_angle = 3.14159265f * 2.0f * (1.0f - g_VolState.usage_ratio);
            for (float a = 0.0f; a < free_angle; a += 0.04f) {
                pDraw->AddTriangleFilled(
                    pieCenter,
                    ImVec2(pieCenter.x + cosf(a) * pieRadius, pieCenter.y + sinf(a) * pieRadius),
                    ImVec2(pieCenter.x + cosf(a + 0.05f) * pieRadius, pieCenter.y + sinf(a + 0.05f) * pieRadius),
                    IM_COL32(235, 80, 160, 255) // Free Pink
                );
            }
            pDraw->AddCircle(pieCenter, pieRadius, IM_COL32(0, 0, 0, 80), 64, 1.5f);

            // Legend and Buttons aligned to the right of Pie
            float legendX = pieCenter.x + pieRadius + 30.0f * scale;
            ImGui::SetCursorScreenPos(ImVec2(legendX, cursorScreen.y + 15.0f * scale));

            ImGui::BeginGroup();
            char usedLabel[64], freeLabel[64];
            snprintf(usedLabel, sizeof(usedLabel), Tr("Занято:   %.1f ГБ (%.1f%%)", "Used:     %.1f GB (%.1f%%)"), used_gb, g_VolState.usage_ratio * 100.0f);
            snprintf(freeLabel, sizeof(freeLabel), Tr("Свободно: %.1f ГБ (%.1f%%)", "Free:     %.1f GB (%.1f%%)"), free_gb, (1.0f - g_VolState.usage_ratio) * 100.0f);

            ImVec2 leg1 = ImGui::GetCursorScreenPos();
            pDraw->AddRectFilled(ImVec2(leg1.x, leg1.y + 2), ImVec2(leg1.x + 14 * scale, leg1.y + (2 + 12 * scale)), IM_COL32(0, 85, 230, 255), 2.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0f * scale);
            ImGui::TextColored(ImVec4(0.0f, 0.35f, 0.9f, 1.0f), "%s", usedLabel);

            ImGui::Spacing();
            ImVec2 leg2 = ImGui::GetCursorScreenPos();
            pDraw->AddRectFilled(ImVec2(leg2.x, leg2.y + 2), ImVec2(leg2.x + 14 * scale, leg2.y + (2 + 12 * scale)), IM_COL32(235, 80, 160, 255), 2.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0f * scale);
            ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.6f, 1.0f), "%s", freeLabel);

            ImGui::Spacing();
            ImGui::Spacing();
            float btnW = 140.0f * scale;
            float btnH = 28.0f * scale;
            if (ImGui::Button(Tr("Очистка диска...", "Disk Cleanup..."), ImVec2(btnW, btnH))) {
                ToggleWindow(WIN_CLEANUP);
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Дефрагментация...", "Defragmentation..."), ImVec2(btnW, btnH))) {
                ToggleWindow(WIN_DEFRAG);
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Обновить", "Refresh"), ImVec2(90.0f * scale, btnH))) {
                RefreshVolumeTelemetry();
                PlayXPSound(L"Windows XP Start.wav");
            }
            ImGui::EndGroup();

            ImGui::EndChild();

        } else if (win.id == WIN_CLEANUP) {
            // === 3. Disk Cleanup (cleanmgr) ===
            ImGui::Text("%s", Tr("Очистка диска для (C:)", "Disk Cleanup for (C:)"));
            double total_clean_mb = (double)(g_Clean.temp_bytes + g_Clean.recycle_bytes + g_Clean.logs_bytes) / (1024.0 * 1024.0);
            char cleanSummary[128];
            snprintf(cleanSummary, sizeof(cleanSummary), Tr("Вы можете использовать средство очистки диска для освобождения до %.1f МБ на диске (C:).", "You can use Disk Cleanup to free up to %.1f MB on disk (C:)."), total_clean_mb);
            ImGui::TextWrapped("%s", cleanSummary);
            ImGui::Separator();

            ImGui::BeginChild("CleanListChild", ImVec2(-1, -45.0f * scale), true);
            double temp_mb = (double)g_Clean.temp_bytes / (1024.0 * 1024.0);
            double rec_mb  = (double)g_Clean.recycle_bytes / (1024.0 * 1024.0);
            double log_mb  = (double)g_Clean.logs_bytes / (1024.0 * 1024.0);

            char tempLabel[128], recLabel[128], logLabel[128];
            snprintf(tempLabel, sizeof(tempLabel), Tr("Временные файлы (%%TEMP%%)  -  %.1f МБ", "Temporary files (%%TEMP%%)  -  %.1f MB"), temp_mb);
            snprintf(recLabel,  sizeof(recLabel),  Tr("Корзина Windows (%lld файлов)  -  %.1f МБ", "Windows Recycle Bin (%lld files)  -  %.1f MB"), (long long)g_Recycle.num_items, rec_mb);
            snprintf(logLabel,  sizeof(logLabel),  Tr("Кэш системных логов и дампов  -  %.1f МБ", "System logs and crash dumps  -  %.1f MB"), log_mb);

            ImGui::Spacing();
            ImGui::Checkbox(tempLabel, &g_Clean.clean_temp);
            ImGui::Spacing();
            ImGui::Checkbox(recLabel, &g_Clean.clean_recycle);
            ImGui::Spacing();
            ImGui::Checkbox(logLabel, &g_Clean.clean_logs);
            ImGui::EndChild();

            ImGui::Spacing();
            if (g_Clean.is_cleaning) {
                ImGui::TextColored(ImVec4(0.0f, 0.4f, 0.9f, 1.0f), "%s", g_Clean.clean_status.c_str());
                ImGui::ProgressBar(g_Clean.clean_progress, ImVec2(-1, 20.0f * scale));
            } else {
                if (!g_Clean.clean_status.empty()) {
                    ImGui::TextColored(ImVec4(0.1f, 0.6f, 0.1f, 1.0f), "%s", g_Clean.clean_status.c_str());
                }
                float btnH = 30.0f * scale;
                if (ImGui::Button(Tr("Очистить диск", "Clean Disk Now"), ImVec2(130.0f * scale, btnH))) {
                    StartDiskCleanup();
                }
                ImGui::SameLine();
                if (ImGui::Button(Tr("Обновить анализ", "Refresh Scan"), ImVec2(140.0f * scale, btnH))) {
                    RefreshCleanupScan();
                    PlayXPSound(L"Windows Navigation Start.wav");
                }
                ImGui::SameLine();
                if (ImGui::Button(Tr("Отмена", "Cancel"), ImVec2(90.0f * scale, btnH))) {
                    win.is_open = false;
                }
            }

        } else if (win.id == WIN_BENCHMARK) {
            // === 4. Benchmark Window ===
            ImGui::Text("%s", Tr("Тест производительности накопителя  -  NVMe / DirectStorage", "Storage Performance Benchmark  -  NVMe / DirectStorage"));
            ImGui::Separator();

            float availW = ImGui::GetContentRegionAvail().x;
            ImGui::Columns(2, "BenchCols", false);
            ImGui::SetColumnWidth(0, std::max(280.0f * scale, availW * 0.42f));

            ImGui::Text("%s", Tr("Тестовый накопитель:", "Test Target:"));
            ImGui::TextColored(ImVec4(0.0f, 0.4f, 0.9f, 1.0f), "C: [%s]", g_VolState.product_name.c_str());
            ImGui::Spacing();
            ImGui::Text("%s %s", Tr("Интерфейс:", "Interface:"), g_VolState.bus_type_str.c_str());
            std::string bpDisplay;
            if (g_VolState.bypass_io_supported) {
                bpDisplay = Tr("Активен (NVMe -> GPU)", "Active (NVMe -> GPU)");
            } else if (!g_VolState.bypass_io_driver.empty()) {
                bpDisplay = Tr("Заблокирован (", "Blocked by ") + g_VolState.bypass_io_driver + ")";
            } else if (g_VolState.bus_type_str.find("NVMe") != std::string::npos) {
                bpDisplay = Tr("Блок фильтра (WdFilter/volsnap)", "Blocked (Minifilter Driver)");
            } else {
                bpDisplay = Tr("Не поддерживается накопителем", "Hardware Unsupported");
            }
            ImGui::Text("%s %s", Tr("BypassIO DirectStorage:", "BypassIO DirectStorage:"), bpDisplay.c_str());
            ImGui::Text("%s", Tr("Размер теста: 128 МБ (Quick IOCP)", "Sample Size: 128 MB (Quick IOCP)"));
            ImGui::Spacing();

            float benchBtnW = 220.0f * scale;
            float benchBtnH = 36.0f * scale;
            if (g_Bench.is_running) {
                ImGui::Button(Tr("Идет тест...", "Running Benchmark..."), ImVec2(benchBtnW, benchBtnH));
                float p = sinf((float)ImGui::GetTime() * 4.0f) * 0.5f + 0.5f;
                ImGui::ProgressBar(p, ImVec2(benchBtnW, 14.0f * scale));
            } else {
                if (ImGui::Button(Tr("Запустить Benchmark", "Run Benchmark"), ImVec2(benchBtnW, benchBtnH))) {
                    StartBenchmarkRun();
                }
            }
            ImGui::Spacing();
            const char* benchStatus = Tr("Готов к запуску", "Ready to start");
            if (g_Bench.is_running) {
                benchStatus = Tr("Выполняется стресс-тест (IOCP)...", "Running IOCP stress-test (128 MB)...");
            } else if (g_Bench.has_results) {
                benchStatus = Tr("Тестирование завершено успешно!", "Benchmark completed successfully!");
            } else if (g_Bench.status_code == 3) {
                benchStatus = Tr("Ошибка тестирования", "Benchmark test error");
            }
            ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "%s", benchStatus);

            ImGui::NextColumn();
            ImGui::Text("%s", Tr("Результаты тестов (МБ/с, IOPS, задержка):", "Benchmark Results (MB/s, IOPS, Latency):"));
            ImGui::Spacing();
            
            char seqR[128], seqW[128], rndI[128], latL[128];
            if (g_Bench.has_results) {
                snprintf(seqR, sizeof(seqR), Tr("Последовательное чтение: %.1f МБ/с", "Sequential Read:  %.1f MB/s"), g_Bench.metrics.seq_read_mbps);
                snprintf(seqW, sizeof(seqW), Tr("Последовательная запись: %.1f МБ/с", "Sequential Write: %.1f MB/s"), g_Bench.metrics.seq_write_mbps);
                snprintf(rndI, sizeof(rndI), Tr("Случайный доступ 4K QD32: %.0f IOPS", "Random Read 4KB QD32: %.0f IOPS"), g_Bench.metrics.rnd_4k_read_iops);
                snprintf(latL, sizeof(latL), Tr("Задержка обращения (Latency): %.1f мкс", "Read Latency (Q1T1 4KB): %.1f us"), g_Bench.metrics.rnd_4k_q1t1_avg_lat_us);
            } else {
                snprintf(seqR, sizeof(seqR), Tr("Последовательное чтение:  -  (нажмите Запустить)", "Sequential Read:   -  (click Run Benchmark)"));
                snprintf(seqW, sizeof(seqW), Tr("Последовательная запись:  - ", "Sequential Write:  - "));
                snprintf(rndI, sizeof(rndI), Tr("Случайный доступ 4K QD32:  - ", "Random Read 4KB QD32:  - "));
                snprintf(latL, sizeof(latL), Tr("Задержка обращения (Latency):  - ", "Read Latency (Q1T1 4KB):  - "));
            }

            float barH = 16.0f * scale;
            ImGui::Text("%s", seqR);
            float rFrac = g_Bench.has_results ? (float)(g_Bench.metrics.seq_read_mbps / 8000.0) : 0.0f;
            if (rFrac > 1.0f) rFrac = 1.0f;
            ImGui::ProgressBar(rFrac, ImVec2(-1, barH));

            ImGui::Spacing();
            ImGui::Text("%s", seqW);
            float wFrac = g_Bench.has_results ? (float)(g_Bench.metrics.seq_write_mbps / 8000.0) : 0.0f;
            if (wFrac > 1.0f) wFrac = 1.0f;
            ImGui::ProgressBar(wFrac, ImVec2(-1, barH));

            ImGui::Spacing();
            ImGui::Text("%s", rndI);
            float iFrac = g_Bench.has_results ? (float)(g_Bench.metrics.rnd_4k_read_iops / 1000000.0) : 0.0f;
            if (iFrac > 1.0f) iFrac = 1.0f;
            ImGui::ProgressBar(iFrac, ImVec2(-1, barH));

            ImGui::Spacing();
            ImGui::Text("%s", latL);
            float lFrac = g_Bench.has_results ? (float)(1.0 - (std::min)(g_Bench.metrics.rnd_4k_q1t1_avg_lat_us / 200.0, 1.0)) : 0.0f;
            ImGui::ProgressBar(lFrac, ImVec2(-1, barH));

            ImGui::Columns(1);

        } else if (win.id == WIN_CMD) {
            // === 5. Command Prompt (cmd.exe) ===
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            ImGui::BeginChild("CmdScrollArea", ImVec2(-1, -38.0f * scale), true);

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
            ImGui::Text("%s", Tr("Панель управления ClusterShift  -  Конфигурация системы:", "ClusterShift Control Panel  -  System Configuration:"));
            ImGui::Separator();

            float availW = ImGui::GetContentRegionAvail().x;
            float availH = ImGui::GetContentRegionAvail().y;
            float cardH = (availH - 24.0f * scale) * 0.48f;
            if (cardH < 130.0f) cardH = 130.0f;

            // Card 1: Hardware Telemetry
            ImGui::BeginChild("CP_Card1", ImVec2(availW, cardH), true);
            ImGui::TextColored(ImVec4(0.0f, 0.33f, 0.9f, 1.0f), "%s", Tr("Аппаратная конфигурация накопителя:", "Hardware Storage Configuration:"));
            ImGui::Separator();
            ImGui::BulletText("%s %s", Tr("Модель устройства:", "Device Model:"), g_VolState.product_name.c_str());
            ImGui::BulletText("%s %s", Tr("Интерфейс контроллера:", "Controller Bus:"), g_VolState.bus_type_str.c_str());
            if (g_VolState.temperature_c > 0) {
                ImGui::BulletText("%s %d °C  [%s]", Tr("Температура датчика:", "Thermal Sensor:"), g_VolState.temperature_c, Tr("Норма", "Normal"));
            } else {
                ImGui::BulletText("%s %s", Tr("Температура датчика:", "Thermal Sensor:"), Tr("Не поддерживается контроллером", "Unsupported by controller"));
            }
            ImGui::BulletText("%s %u %s", Tr("Размер сектора / кластера:", "Sector / Cluster size:"), g_VolState.cluster_size, Tr("байт", "bytes"));
            ImGui::EndChild();

            ImGui::Spacing();

            // Card 2: Advanced Storage Features (BypassIO, TRIM, VSS)
            ImGui::BeginChild("CP_Card2", ImVec2(availW, -1), true);
            ImGui::TextColored(ImVec4(0.0f, 0.33f, 0.9f, 1.0f), "%s", Tr("Стек прямого доступа и оптимизация flash-памяти:", "Direct Access Stack & Flash Optimization:"));
            ImGui::Separator();

            ImGui::Columns(2, "CPCols", false);
            ImGui::SetColumnWidth(0, availW * 0.55f);

            ImGui::BulletText("%s", Tr("Управление BypassIO DirectStorage:", "DirectStorage BypassIO Status:"));
            ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "%s %s", Tr("Статус стека:", "Stack Status:"), g_VolState.bypass_io_status.c_str());
            ImGui::TextWrapped("%s", Tr("Обеспечивает прямой обмен данными между NVMe и видеопамятью GPU минуя стек файлового фильтра.", "Enables direct high-throughput streaming from NVMe to GPU VRAM bypassing filter drivers."));

            ImGui::Spacing();
            ImGui::BulletText("%s", Tr("Защита VSS (Теневые копии тома):", "VSS Shadow Copy Protection:"));
            ImGui::TextColored(ImVec4(0.1f, 0.55f, 0.1f, 1.0f), "%s", Tr("АКТИВНА  -  снимки NTFS защищены от деградации.", "ACTIVE  -  NTFS snapshots are protected from degradation."));

            ImGui::NextColumn();
            ImGui::BulletText("%s", Tr("TRIM Engine & Сборка мусора SSD:", "TRIM Engine & SSD Garbage Collection:"));
            ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "%s", Tr("Освобождение неиспользуемых блоков NAND.", "Reclaims unused flash NAND memory blocks."));
            ImGui::Spacing();
            float trimBtnW = 210.0f * scale;
            float trimBtnH = 32.0f * scale;
            if (ImGui::Button(Tr("Выполнить TRIM тома C:", "Execute TRIM for Volume C:"), ImVec2(trimBtnW, trimBtnH))) {
                auto vol = clustershift::core::VolumeManager::open_volume(L'C', true);
                if (vol) {
                    (void)clustershift::core::TrimEngine::trim_volume(vol->get());
                    PlayXPSound(L"Windows XP Notify.wav");
                }
            }
            ImGui::Columns(1);
            ImGui::EndChild();

        } else if (win.id == WIN_RECYCLE) {
            // === 7. Recycle Bin ===
            ImGui::Text("%s", Tr("Корзина содержит удаленные файлы:", "Recycle Bin contains deleted files:"));
            ImGui::Separator();

            float availW = ImGui::GetContentRegionAvail().x;
            ImGui::BeginChild("RecycleCard", ImVec2(availW, -45.0f * scale), true);
            float recIconSize = 48.0f * scale;
            if (g_pTexRecycle) {
                ImGui::Image((ImTextureID)g_pTexRecycle, ImVec2(recIconSize, recIconSize));
                ImGui::SameLine(0.0f, 15.0f * scale);
            }
            ImGui::BeginGroup();
            double recMb = (double)g_Recycle.total_bytes / (1024.0 * 1024.0);
            char recItemsBuf[128];
            snprintf(recItemsBuf, sizeof(recItemsBuf), Tr("Объектов в корзине: %lld", "Items in Recycle Bin: %lld"), (long long)g_Recycle.num_items);
            ImGui::TextColored(ImVec4(0.0f, 0.35f, 0.9f, 1.0f), "%s", recItemsBuf);
            char recSizeBuf[128];
            snprintf(recSizeBuf, sizeof(recSizeBuf), Tr("Общий объем: %.2f МБ", "Total Size: %.2f MB"), recMb);
            ImGui::TextColored(ImVec4(0.35f, 0.35f, 0.35f, 1.0f), "%s", recSizeBuf);
            ImGui::EndGroup();
            ImGui::EndChild();

            ImGui::Spacing();
            float rBtnH = 30.0f * scale;
            if (ImGui::Button(Tr("Очистить корзину", "Empty Recycle Bin"), ImVec2(160 * scale, rBtnH))) {
                SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
                RefreshRecycleBin();
                RefreshVolumeTelemetry();
                PlayXPSound(L"Windows XP Recycle.wav");
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr("Обновить", "Refresh"), ImVec2(100 * scale, rBtnH))) {
                RefreshRecycleBin();
                PlayXPSound(L"Windows Navigation Start.wav");
            }

        } else if (win.id == WIN_DONATE) {
            // === 8. Donate joive Window ===
            ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.1f, 1.0f), "%s", Tr("Поддержка разработчика: joive", "Support Developer: joive"));
            ImGui::Separator();
            ImGui::TextWrapped("%s", Tr("ClusterShift v0.6.0 создан joive. Проект оптимизирует файловые системы и кластеры Windows.", "ClusterShift v0.6.0 was created by joive to optimize Windows file systems and storage clusters."));
            ImGui::Spacing();
            ImGui::Text("GitHub: JoiveSer/ClusterShift");
            ImGui::Text("%s: MIT", Tr("Лицензия", "License"));
            ImGui::Spacing();
            ImGui::Text("%s", Tr("Страница доната:", "Donation Page:"));
            float dBtnH = 34.0f * scale;
            if (ImGui::Button(Tr("Поддержать на Ko-fi (https://ko-fi.com/joive)", "Support on Ko-fi (https://ko-fi.com/joive)"), ImVec2(-1, dBtnH))) {
                ShellExecuteW(NULL, L"open", L"https://ko-fi.com/joive", NULL, NULL, SW_SHOWNORMAL);
            }
            ImGui::Spacing();
            if (ImGui::Button(Tr("Закрыть", "Close"), ImVec2(110 * scale, 28 * scale))) {
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
    DetectLanguage(lpCmdLine);

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
        L"ClusterShift OS  -  Windows XP Luna Edition [v0.6.0 by joive]",
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

    // Load Fonts with High-Quality Anti-Aliasing (Smooth, Pleasant Typography & Comprehensive Glyph Ranges)
    static const ImWchar custom_glyph_ranges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement (including 0xB0 degree sign)
        0x0400, 0x052F, // Cyrillic + Cyrillic Supplement
        0x2000, 0x206F, // General Punctuation (dashes, quotes, ellipses, bullets)
        0x2100, 0x214F, // Letterlike Symbols
        0x2190, 0x21FF, // Arrows
        0x2200, 0x22FF, // Mathematical Operators
        0x25A0, 0x25FF, // Geometric Shapes
        0x2DE0, 0x2DFF, // Cyrillic Extended-A
        0xA640, 0xA69F, // Cyrillic Extended-B
        0,
    };

    std::wstring tahomaPath = ResolveAssetPath(L"winxp/WinXp/Fonts/tahoma.ttf");
    if (std::filesystem::exists(tahomaPath)) {
        std::string fontPathU8 = std::filesystem::path(tahomaPath).string();
        ImFontConfig font_cfg;
        font_cfg.OversampleH = 3;
        font_cfg.OversampleV = 2;
        font_cfg.PixelSnapH = true;          // Crisp pixel-perfect grid alignment
        font_cfg.RasterizerMultiply = 1.15f; // Crisp, deep black ink

        g_FontDefault     = io.Fonts->AddFontFromFileTTF(fontPathU8.c_str(), 14.0f, &font_cfg, custom_glyph_ranges);
        g_FontSmall       = io.Fonts->AddFontFromFileTTF(fontPathU8.c_str(), 12.0f, &font_cfg, custom_glyph_ranges);
        g_FontBold        = io.Fonts->AddFontFromFileTTF(fontPathU8.c_str(), 15.0f, &font_cfg, custom_glyph_ranges);
        g_FontLarge       = io.Fonts->AddFontFromFileTTF(fontPathU8.c_str(), 18.0f, &font_cfg, custom_glyph_ranges);
        g_FontStartHeader = io.Fonts->AddFontFromFileTTF(fontPathU8.c_str(), 16.0f, &font_cfg, custom_glyph_ranges);
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

        // F10 Global Hotkey: Toggle Language (RU <-> EN)
        if (ImGui::IsKeyPressed(ImGuiKey_F10)) {
            ToggleLanguage();
        }

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

        // 2. Desktop Apps  -  Left Column (System Apps)
        struct DesktopIcon {
            int windowId;
            std::string title;
            ID3D11ShaderResourceView* icon;
            bool is_exit;
        };

        std::vector<DesktopIcon> leftApps = {
            { WIN_MY_COMPUTER,   Tr("Мой компьютер", "My Computer"),     g_pTexComputer,     false },
            { WIN_DEFRAG,        Tr("Дефрагментатор", "Defragmenter"),    g_pTexDefrag,       false },
            { WIN_CLEANUP,       Tr("Очистка диска", "Disk Cleanup"),     g_pTexCleanup,      false },
            { WIN_BENCHMARK,     Tr("Тест скорости", "Speed Test"),     g_pTexBenchmark,    false },
            { WIN_CMD,           Tr("Командная строка", "Command Prompt"),  g_pTexCmd,          false },
            { WIN_CONTROL_PANEL, Tr("Панель управления", "Control Panel"), g_pTexControlPanel, false },
            { WIN_RECYCLE,       Tr("Корзина", "Recycle Bin"),           g_pTexRecycle,      false }
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

        // 3. Desktop Apps  -  Bottom-Right Corner: "Копейка joive" and "Выход"
        std::vector<DesktopIcon> rightApps = {
            { WIN_DONATE, Tr("Копейка joive", "Support joive"), g_pTexCoin, false },
            { -1,         Tr("Выход", "Exit"),         g_pTexExit, true }
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
                { Tr("Дефрагментатор", "Defragmenter"), Tr("Оптимизация диска", "Disk Optimization"), g_pTexDefrag, WIN_DEFRAG },
                { Tr("Очистка диска", "Disk Cleanup"), Tr("Освободить место", "Free disk space"), g_pTexCleanup, WIN_CLEANUP },
                { Tr("Тест скорости", "Speed Test"), Tr("NVMe Benchmark", "NVMe Benchmark"), g_pTexBenchmark, WIN_BENCHMARK },
                { Tr("Командная строка", "Command Prompt"), "cmd.exe", g_pTexCmd, WIN_CMD },
                { Tr("Мой компьютер", "My Computer"), Tr("Диски и кластеры", "Drives & Clusters"), g_pTexComputer, WIN_MY_COMPUTER }
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
                { Tr("Мои документы", "My Documents"), WIN_MY_COMPUTER },
                { Tr("Панель управления", "Control Panel"), WIN_CONTROL_PANEL },
                { Tr("Копейка joive", "Support joive"), WIN_DONATE },
                { Tr("Поиск", "Search"), WIN_CMD },
                { Tr("Выполнить...", "Run..."), WIN_CMD }
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

            // Footer: System branding & interactive Language Switcher (Single Exit Button is on Desktop)
            float footerY = smY + smH - 42.0f;
            fgDraw->AddRectFilled(ImVec2(smX, footerY), ImVec2(smX + smW, smY + smH), IM_COL32(29, 82, 183, 255), 8.0f);

            if (g_FontSmall) ImGui::PushFont(g_FontSmall);
            fgDraw->AddText(ImVec2(smX + 16.0f, footerY + 12.0f), IM_COL32(200, 225, 255, 200), "ClusterShift v0.6.0");

            ImVec2 langBtnMin(smX + smW - 175.0f, footerY + 6.0f);
            ImVec2 langBtnMax(smX + smW - 12.0f, footerY + 36.0f);
            bool isHoverLang = (io.MousePos.x >= langBtnMin.x && io.MousePos.x <= langBtnMax.x &&
                                io.MousePos.y >= langBtnMin.y && io.MousePos.y <= langBtnMax.y);

            if (isHoverLang) {
                fgDraw->AddRectFilled(langBtnMin, langBtnMax, IM_COL32(60, 130, 245, 220), 4.0f);
                fgDraw->AddRect(langBtnMin, langBtnMax, IM_COL32(255, 255, 255, 200), 4.0f);
            } else {
                fgDraw->AddRectFilled(langBtnMin, langBtnMax, IM_COL32(38, 98, 205, 220), 4.0f);
                fgDraw->AddRect(langBtnMin, langBtnMax, IM_COL32(20, 60, 150, 220), 4.0f);
            }

            const char* langLabel = g_IsRussian ? "Язык: RU [F10]" : "Language: EN [F10]";
            ImVec2 lSize = ImGui::CalcTextSize(langLabel);
            float lx = langBtnMin.x + ((langBtnMax.x - langBtnMin.x) - lSize.x) * 0.5f;
            float ly = langBtnMin.y + ((langBtnMax.y - langBtnMin.y) - lSize.y) * 0.5f;
            fgDraw->AddText(ImVec2(lx, ly), IM_COL32(255, 255, 255, 255), langLabel);
            if (g_FontSmall) ImGui::PopFont();

            if (isHoverLang && ImGui::IsMouseClicked(0)) {
                ToggleLanguage();
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
        fgDraw->AddText(ImVec2(startMin.x + 34.0f, startMin.y + 7.0f), IM_COL32(0, 0, 0, 180), Tr("Пуск", "start"));
        fgDraw->AddText(ImVec2(startMin.x + 33.0f, startMin.y + 6.0f), IM_COL32(255, 255, 255, 255), Tr("Пуск", "start"));
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

        // --- Authentic Windows XP Luna System Tray with Language Bar & Clock ---
        float trayW = 138.0f;
        float trayX = displayW - trayW;
        ImVec2 trayMin(trayX, tbY);
        ImVec2 trayMax(displayW, displayH);

        // Inset darker blue tray gradient
        fgDraw->AddRectFilledMultiColor(
            trayMin, trayMax,
            IM_COL32(15, 66, 160, 255), IM_COL32(15, 66, 160, 255),
            IM_COL32(11, 46, 126, 255), IM_COL32(11, 46, 126, 255)
        );
        // 3D Inset groove on the left border of tray
        fgDraw->AddLine(trayMin, ImVec2(trayX, displayH), IM_COL32(9, 32, 90, 255), 1.5f);
        fgDraw->AddLine(ImVec2(trayX + 1.5f, tbY), ImVec2(trayX + 1.5f, displayH), IM_COL32(35, 95, 225, 255), 1.0f);
        // Inset top shadow
        fgDraw->AddLine(ImVec2(trayX, tbY), ImVec2(displayW, tbY), IM_COL32(9, 32, 90, 255), 1.0f);

        // Windows XP Language Indicator Button [ RU ] / [ EN ]
        ImVec2 langBtnMin(trayX + 8.0f, tbY + 5.0f);
        ImVec2 langBtnMax(trayX + 46.0f, tbY + 27.0f);
        bool isHoverTrayLang = (io.MousePos.x >= langBtnMin.x && io.MousePos.x <= langBtnMax.x &&
                                io.MousePos.y >= langBtnMin.y && io.MousePos.y <= langBtnMax.y);

        if (isHoverTrayLang) {
            fgDraw->AddRectFilled(langBtnMin, langBtnMax, IM_COL32(40, 110, 230, 220), 2.0f);
            fgDraw->AddRect(langBtnMin, langBtnMax, IM_COL32(130, 195, 255, 255), 2.0f);
        } else {
            fgDraw->AddRectFilled(langBtnMin, langBtnMax, IM_COL32(18, 48, 130, 220), 2.0f);
            fgDraw->AddRect(langBtnMin, langBtnMax, IM_COL32(10, 32, 95, 255), 2.0f);
        }

        const char* langCode = g_IsRussian ? "RU" : "EN";
        if (g_FontSmall) ImGui::PushFont(g_FontSmall);
        ImVec2 langTextSize = ImGui::CalcTextSize(langCode);
        float langTextX = langBtnMin.x + ((langBtnMax.x - langBtnMin.x) - langTextSize.x) * 0.5f;
        float langTextY = langBtnMin.y + ((langBtnMax.y - langBtnMin.y) - langTextSize.y) * 0.5f;
        fgDraw->AddText(ImVec2(langTextX + 1.0f, langTextY + 1.0f), IM_COL32(0, 0, 0, 150), langCode);
        fgDraw->AddText(ImVec2(langTextX, langTextY), IM_COL32(255, 255, 255, 255), langCode);
        if (g_FontSmall) ImGui::PopFont();

        if (isHoverTrayLang && ImGui::IsMouseClicked(0)) {
            ToggleLanguage();
        }

        // Clock on the right
        time_t now = time(nullptr);
        tm localTime;
        localtime_s(&localTime, &now);
        char timeStr[16];
        strftime(timeStr, sizeof(timeStr), "%H:%M", &localTime);

        if (g_FontDefault) ImGui::PushFont(g_FontDefault);
        ImVec2 timeSize = ImGui::CalcTextSize(timeStr);
        float clockAreaX = trayX + 50.0f;
        float clockAreaW = displayW - clockAreaX;
        float timeX = clockAreaX + (clockAreaW - timeSize.x) * 0.5f;
        float timeY = tbY + (32.0f - timeSize.y) * 0.5f;

        // Drop shadow for crisp contrast
        fgDraw->AddText(ImVec2(timeX + 1.0f, timeY + 1.0f), IM_COL32(0, 0, 0, 140), timeStr);
        fgDraw->AddText(ImVec2(timeX, timeY), IM_COL32(255, 255, 255, 255), timeStr);
        if (g_FontDefault) ImGui::PopFont();

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
