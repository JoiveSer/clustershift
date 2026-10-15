#include "core/disk_benchmark.hpp"
#include "core/volume_manager.hpp"
#include <windows.h>
#include <random>
#include <vector>
#include <chrono>

namespace clustershift::core {

namespace {

struct AlignedBuffer {
    void* ptr{nullptr};
    size_t size{0};

    explicit AlignedBuffer(size_t bytes) : size(bytes) {
        ptr = VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }
    ~AlignedBuffer() {
        if (ptr) {
            VirtualFree(ptr, 0, MEM_RELEASE);
        }
    }
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
};

double get_time_seconds(LARGE_INTEGER start, LARGE_INTEGER end, LARGE_INTEGER freq) {
    return static_cast<double>(end.QuadPart - start.QuadPart) / static_cast<double>(freq.QuadPart);
}

} // namespace

std::expected<BenchmarkMetrics, DWORD> DiskBenchmark::run_benchmark(
    wchar_t drive_letter,
    uint64_t test_size_bytes,
    bool quick_mode
) {
    const wchar_t upper = static_cast<wchar_t>(towupper(drive_letter));
    
    // Choose test file location
    std::wstring test_file_path = std::wstring(1, upper) + L":\\.clustershift_bench.tmp";

    // Unbuffered I/O requires aligned buffers and sizes
    const size_t seq_block_size = 1024 * 1024; // 1 MB
    const size_t rnd_block_size = 4096;        // 4 KB

    if (quick_mode) {
        test_size_bytes = (std::min)(test_size_bytes, static_cast<uint64_t>(64 * 1024 * 1024)); // 64 MB
    }

    // Ensure test_size is multiple of 1MB
    test_size_bytes = (test_size_bytes / seq_block_size) * seq_block_size;
    if (test_size_bytes == 0) {
        test_size_bytes = seq_block_size * 64;
    }

    AlignedBuffer seq_buf(seq_block_size);
    if (!seq_buf.ptr) {
        return std::unexpected(GetLastError());
    }

    // Fill with pseudo-random pattern to defeat controller deduplication/compression
    uint8_t* byte_ptr = static_cast<uint8_t*>(seq_buf.ptr);
    for (size_t i = 0; i < seq_block_size; ++i) {
        byte_ptr[i] = static_cast<uint8_t>((i * 137 + 13) & 0xFF);
    }

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    // 1. Open Unbuffered File (Auto-delete on close)
    HANDLE raw_handle = CreateFileW(
        test_file_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr
    );

    // If root directory write access denied, fallback to temp folder on target drive
    if (raw_handle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED) {
        wchar_t temp_dir[MAX_PATH];
        if (GetTempPathW(MAX_PATH, temp_dir)) {
            test_file_path = std::wstring(temp_dir) + L"\\.clustershift_bench.tmp";
            raw_handle = CreateFileW(
                test_file_path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                CREATE_ALWAYS,
                FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_DELETE_ON_CLOSE,
                nullptr
            );
        }
    }

    if (raw_handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(GetLastError());
    }

    ScopedHandle file_handle(raw_handle);

    BenchmarkMetrics metrics{};
    metrics.test_file_size_bytes = test_size_bytes;

    LARGE_INTEGER t_start, t_end;

    // --- TEST 1: SEQUENTIAL WRITE ---
    QueryPerformanceCounter(&t_start);
    uint64_t bytes_written_total = 0;
    while (bytes_written_total < test_size_bytes) {
        DWORD bytes_written = 0;
        if (!WriteFile(file_handle.get(), seq_buf.ptr, static_cast<DWORD>(seq_block_size), &bytes_written, nullptr)) {
            return std::unexpected(GetLastError());
        }
        bytes_written_total += bytes_written;
    }
    QueryPerformanceCounter(&t_end);
    double seq_write_time = get_time_seconds(t_start, t_end, freq);
    if (seq_write_time > 0.0) {
        metrics.seq_write_mbps = (static_cast<double>(test_size_bytes) / (1024.0 * 1024.0)) / seq_write_time;
    }

    // --- TEST 2: SEQUENTIAL READ ---
    LARGE_INTEGER move_ptr{};
    move_ptr.QuadPart = 0;
    SetFilePointerEx(file_handle.get(), move_ptr, nullptr, FILE_BEGIN);

    QueryPerformanceCounter(&t_start);
    uint64_t bytes_read_total = 0;
    while (bytes_read_total < test_size_bytes) {
        DWORD bytes_read = 0;
        if (!ReadFile(file_handle.get(), seq_buf.ptr, static_cast<DWORD>(seq_block_size), &bytes_read, nullptr)) {
            return std::unexpected(GetLastError());
        }
        bytes_read_total += bytes_read;
    }
    QueryPerformanceCounter(&t_end);
    double seq_read_time = get_time_seconds(t_start, t_end, freq);
    if (seq_read_time > 0.0) {
        metrics.seq_read_mbps = (static_cast<double>(test_size_bytes) / (1024.0 * 1024.0)) / seq_read_time;
    }

    // --- TEST 3: RANDOM 4KB READ (IOPS) ---
    AlignedBuffer rnd_buf(rnd_block_size);
    const uint64_t max_blocks = test_size_bytes / rnd_block_size;
    const uint32_t total_rnd_ops = quick_mode ? 1500 : 4000;

    std::mt19937_64 rng(1337);
    std::uniform_int_distribution<uint64_t> dist(0, max_blocks - 1);

    QueryPerformanceCounter(&t_start);
    for (uint32_t i = 0; i < total_rnd_ops; ++i) {
        const uint64_t block_idx = dist(rng);
        LARGE_INTEGER pos{};
        pos.QuadPart = static_cast<LONGLONG>(block_idx * rnd_block_size);

        OVERLAPPED ov{};
        ov.Offset = pos.LowPart;
        ov.OffsetHigh = static_cast<DWORD>(pos.HighPart);

        DWORD read_bytes = 0;
        ReadFile(file_handle.get(), rnd_buf.ptr, static_cast<DWORD>(rnd_block_size), &read_bytes, &ov);
    }
    QueryPerformanceCounter(&t_end);

    double rnd_time = get_time_seconds(t_start, t_end, freq);
    if (rnd_time > 0.0) {
        metrics.rnd_4k_read_iops = static_cast<double>(total_rnd_ops) / rnd_time;
        metrics.rnd_4k_read_mbps = (static_cast<double>(total_rnd_ops * rnd_block_size) / (1024.0 * 1024.0)) / rnd_time;
    }

    metrics.duration_seconds = seq_write_time + seq_read_time + rnd_time;
    return metrics;
}

} // namespace clustershift::core
