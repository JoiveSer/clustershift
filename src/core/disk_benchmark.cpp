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

struct IoContext {
    OVERLAPPED ov{};
    uint32_t slot_idx{0};
};

} // namespace

std::expected<BenchmarkMetrics, DWORD> DiskBenchmark::run_benchmark(
    wchar_t drive_letter,
    uint64_t test_size_bytes,
    bool quick_mode
) {
    const wchar_t upper = static_cast<wchar_t>(towupper(drive_letter));
    
    std::wstring test_file_path = std::wstring(1, upper) + L":\\.clustershift_bench.tmp";

    const size_t seq_block_size = 1024 * 1024; // 1 MB
    const size_t rnd_block_size = 4096;        // 4 KB
    const uint32_t queue_depth = 32;          // True QD32 for modern NVMe hardware queues

    if (quick_mode) {
        test_size_bytes = 128ULL * 1024 * 1024; // 128 MB in quick mode
    } else {
        test_size_bytes = (std::max)(test_size_bytes, 1024ULL * 1024 * 1024); // 1 GB standard
    }

    test_size_bytes = (test_size_bytes / seq_block_size) * seq_block_size;
    if (test_size_bytes == 0) {
        test_size_bytes = seq_block_size * 128;
    }

    AlignedBuffer seq_buf(seq_block_size);
    if (!seq_buf.ptr) {
        return std::unexpected(GetLastError());
    }

    uint8_t* byte_ptr = static_cast<uint8_t*>(seq_buf.ptr);
    for (size_t i = 0; i < seq_block_size; ++i) {
        byte_ptr[i] = static_cast<uint8_t>((i * 137 + 13) & 0xFF);
    }

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    // Open unbuffered file with OVERLAPPED and auto-delete.
    // NOTICE: Omit FILE_FLAG_WRITE_THROUGH to allow parallel NVMe controller hardware queue streaming.
    HANDLE raw_handle = CreateFileW(
        test_file_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        CREATE_ALWAYS,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (raw_handle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED) {
        wchar_t temp_dir[MAX_PATH];
        if (GetTempPathW(MAX_PATH, temp_dir)) {
            test_file_path = std::wstring(temp_dir) + L"\\.clustershift_bench.tmp";
            raw_handle = CreateFileW(
                test_file_path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                CREATE_ALWAYS,
                FILE_FLAG_NO_BUFFERING | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OVERLAPPED,
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
    metrics.queue_depth = queue_depth;

    LARGE_INTEGER t_start, t_end;

    // --- TEST 1: SEQUENTIAL WRITE (1 MB blocks) ---
    HANDLE seq_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!seq_event) {
        return std::unexpected(GetLastError());
    }

    QueryPerformanceCounter(&t_start);
    uint64_t bytes_written_total = 0;
    while (bytes_written_total < test_size_bytes) {
        ResetEvent(seq_event);
        OVERLAPPED ov{};
        ov.hEvent = seq_event;
        ov.Offset = static_cast<DWORD>(bytes_written_total & 0xFFFFFFFF);
        ov.OffsetHigh = static_cast<DWORD>((bytes_written_total >> 32) & 0xFFFFFFFF);

        DWORD written = 0;
        BOOL ok = WriteFile(file_handle.get(), seq_buf.ptr, static_cast<DWORD>(seq_block_size), &written, &ov);
        if (!ok && GetLastError() != ERROR_IO_PENDING) {
            CloseHandle(seq_event);
            return std::unexpected(GetLastError());
        }
        if (!ok) {
            WaitForSingleObject(seq_event, INFINITE);
            GetOverlappedResult(file_handle.get(), &ov, &written, FALSE);
        }
        bytes_written_total += written;
    }
    QueryPerformanceCounter(&t_end);
    CloseHandle(seq_event);

    FlushFileBuffers(file_handle.get());

    double seq_write_time = get_time_seconds(t_start, t_end, freq);
    if (seq_write_time > 0.0) {
        metrics.seq_write_mbps = (static_cast<double>(test_size_bytes) / (1024.0 * 1024.0)) / seq_write_time;
    }

    // --- TEST 2: SEQUENTIAL READ (1 MB blocks) ---
    seq_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!seq_event) {
        return std::unexpected(GetLastError());
    }

    QueryPerformanceCounter(&t_start);
    uint64_t bytes_read_total = 0;
    while (bytes_read_total < test_size_bytes) {
        ResetEvent(seq_event);
        OVERLAPPED ov{};
        ov.hEvent = seq_event;
        ov.Offset = static_cast<DWORD>(bytes_read_total & 0xFFFFFFFF);
        ov.OffsetHigh = static_cast<DWORD>((bytes_read_total >> 32) & 0xFFFFFFFF);

        DWORD read = 0;
        BOOL ok = ReadFile(file_handle.get(), seq_buf.ptr, static_cast<DWORD>(seq_block_size), &read, &ov);
        if (!ok && GetLastError() != ERROR_IO_PENDING) {
            CloseHandle(seq_event);
            return std::unexpected(GetLastError());
        }
        if (!ok) {
            WaitForSingleObject(seq_event, INFINITE);
            GetOverlappedResult(file_handle.get(), &ov, &read, FALSE);
        }
        bytes_read_total += read;
    }
    QueryPerformanceCounter(&t_end);
    CloseHandle(seq_event);

    double seq_read_time = get_time_seconds(t_start, t_end, freq);
    if (seq_read_time > 0.0) {
        metrics.seq_read_mbps = (static_cast<double>(test_size_bytes) / (1024.0 * 1024.0)) / seq_read_time;
    }

    // --- TEST 3: HIGH-PERFORMANCE IOCP RANDOM 4KB READ (QD32 PIPELINE) ---
    AlignedBuffer rnd_buffers(rnd_block_size * queue_depth);
    if (!rnd_buffers.ptr) {
        return std::unexpected(GetLastError());
    }

    const uint64_t max_blocks = test_size_bytes / rnd_block_size;
    const uint32_t total_rnd_ops = quick_mode ? 8000 : 40000;

    std::mt19937_64 rng(1337);
    std::uniform_int_distribution<uint64_t> dist(0, max_blocks - 1);

    // Bind file handle to an I/O Completion Port (IOCP) for zero-wait kernel-level batch dequeuing
    HANDLE iocp = CreateIoCompletionPort(file_handle.get(), nullptr, 0, 1);
    if (!iocp) {
        return std::unexpected(GetLastError());
    }

    std::vector<IoContext> slots(queue_depth);
    for (uint32_t i = 0; i < queue_depth; ++i) {
        slots[i].slot_idx = i;
    }

    uint32_t requests_issued = 0;
    uint32_t requests_completed = 0;

    auto dispatch_read = [&](IoContext& ctx) {
        const uint64_t block_idx = dist(rng);
        const uint64_t byte_offset = block_idx * rnd_block_size;

        ZeroMemory(&ctx.ov, sizeof(OVERLAPPED));
        ctx.ov.Offset = static_cast<DWORD>(byte_offset & 0xFFFFFFFF);
        ctx.ov.OffsetHigh = static_cast<DWORD>((byte_offset >> 32) & 0xFFFFFFFF);

        uint8_t* slot_buf = static_cast<uint8_t*>(rnd_buffers.ptr) + (ctx.slot_idx * rnd_block_size);
        DWORD bytes_read = 0;

        BOOL res = ReadFile(file_handle.get(), slot_buf, static_cast<DWORD>(rnd_block_size), &bytes_read, &ctx.ov);
        if (!res) {
            DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) {
                // Ignore unexpected or EOF
            }
        }
        requests_issued++;
    };

    QueryPerformanceCounter(&t_start);

    // Pre-fill all 32 hardware queue slots
    const uint32_t initial_count = (std::min)(queue_depth, total_rnd_ops);
    for (uint32_t i = 0; i < initial_count; ++i) {
        dispatch_read(slots[i]);
    }

    // High-speed sliding window dequeuing via GetQueuedCompletionStatusEx
    OVERLAPPED_ENTRY completed_entries[32];

    while (requests_completed < total_rnd_ops) {
        ULONG dequeued_count = 0;
        BOOL status = GetQueuedCompletionStatusEx(
            iocp,
            completed_entries,
            queue_depth,
            &dequeued_count,
            INFINITE,
            FALSE
        );

        if (!status || dequeued_count == 0) {
            break;
        }

        for (ULONG c = 0; c < dequeued_count; ++c) {
            requests_completed++;
            IoContext* ctx = reinterpret_cast<IoContext*>(completed_entries[c].lpOverlapped);
            if (requests_issued < total_rnd_ops && ctx) {
                dispatch_read(*ctx);
            }
        }
    }

    QueryPerformanceCounter(&t_end);
    CloseHandle(iocp);

    double rnd_time = get_time_seconds(t_start, t_end, freq);
    if (rnd_time > 0.0) {
        metrics.rnd_4k_read_iops = static_cast<double>(requests_completed) / rnd_time;
        metrics.rnd_4k_read_mbps = (static_cast<double>(requests_completed * rnd_block_size) / (1024.0 * 1024.0)) / rnd_time;
    }

    metrics.duration_seconds = seq_write_time + seq_read_time + rnd_time;
    return metrics;
}

} // namespace clustershift::core
