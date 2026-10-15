#include "core/disk_benchmark.hpp"
#include "core/volume_manager.hpp"
#include <windows.h>
#include <random>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>

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
    bool quick_mode,
    uint32_t threads
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

    // --- TEST 3: HIGH-PERFORMANCE MULTI-THREADED IOCP RANDOM 4KB READ (Q32Tx PIPELINE) ---
    uint32_t active_threads = (threads > 0) ? threads : (std::max)(1u, std::thread::hardware_concurrency());
    metrics.thread_count = active_threads;

    const uint64_t max_blocks = test_size_bytes / rnd_block_size;
    const uint32_t total_rnd_ops = quick_mode ? 16000 : 80000;

    // Bind file handle to an I/O Completion Port (IOCP) with concurrency matching worker count
    HANDLE iocp = CreateIoCompletionPort(file_handle.get(), nullptr, 0, active_threads);
    if (!iocp) {
        return std::unexpected(GetLastError());
    }

    std::atomic<uint32_t> issued_counter{0};
    std::atomic<uint32_t> completed_counter{0};

    auto worker_task = [&](uint32_t thread_id) {
        AlignedBuffer thread_buf(rnd_block_size * queue_depth);
        if (!thread_buf.ptr) return;

        std::mt19937_64 thread_rng(1337 + thread_id * 17);
        std::uniform_int_distribution<uint64_t> thread_dist(0, max_blocks - 1);

        std::vector<IoContext> thread_slots(queue_depth);
        for (uint32_t i = 0; i < queue_depth; ++i) {
            thread_slots[i].slot_idx = i;
        }

        auto dispatch_slot = [&](IoContext& ctx) {
            if (issued_counter.fetch_add(1, std::memory_order_relaxed) >= total_rnd_ops) {
                return false;
            }
            const uint64_t block_idx = thread_dist(thread_rng);
            const uint64_t byte_offset = block_idx * rnd_block_size;

            ZeroMemory(&ctx.ov, sizeof(OVERLAPPED));
            ctx.ov.Offset = static_cast<DWORD>(byte_offset & 0xFFFFFFFF);
            ctx.ov.OffsetHigh = static_cast<DWORD>((byte_offset >> 32) & 0xFFFFFFFF);

            uint8_t* slot_buf = static_cast<uint8_t*>(thread_buf.ptr) + (ctx.slot_idx * rnd_block_size);
            DWORD bytes_read = 0;
            BOOL res = ReadFile(file_handle.get(), slot_buf, static_cast<DWORD>(rnd_block_size), &bytes_read, &ctx.ov);
            if (!res && GetLastError() != ERROR_IO_PENDING) {
                // Ignore unexpected or EOF
            }
            return true;
        };

        // Fill initial thread queue slots
        for (uint32_t i = 0; i < queue_depth; ++i) {
            if (!dispatch_slot(thread_slots[i])) break;
        }

        OVERLAPPED_ENTRY entries[32];
        while (completed_counter.load(std::memory_order_relaxed) < total_rnd_ops) {
            ULONG dequeued = 0;
            BOOL ok = GetQueuedCompletionStatusEx(iocp, entries, 32, &dequeued, 50, FALSE);
            if (!ok || dequeued == 0) {
                if (completed_counter.load(std::memory_order_relaxed) >= total_rnd_ops) {
                    break;
                }
                continue;
            }

            for (ULONG c = 0; c < dequeued; ++c) {
                completed_counter.fetch_add(1, std::memory_order_relaxed);
                IoContext* ctx = reinterpret_cast<IoContext*>(entries[c].lpOverlapped);
                if (ctx) {
                    dispatch_slot(*ctx);
                }
            }
        }
    };

    QueryPerformanceCounter(&t_start);
    std::vector<std::thread> workers;
    workers.reserve(active_threads);
    for (uint32_t t = 0; t < active_threads; ++t) {
        workers.emplace_back(worker_task, t);
    }
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    QueryPerformanceCounter(&t_end);
    CloseHandle(iocp);

    double rnd_time = get_time_seconds(t_start, t_end, freq);
    uint32_t total_done = completed_counter.load();
    if (rnd_time > 0.0 && total_done > 0) {
        metrics.rnd_4k_read_iops = static_cast<double>(total_done) / rnd_time;
        metrics.rnd_4k_read_mbps = (static_cast<double>(total_done * rnd_block_size) / (1024.0 * 1024.0)) / rnd_time;
    }

    // --- TEST 4: SINGLE-THREAD RANDOM 4KB LATENCY (Q1T1 BENCHMARK) ---
    HANDLE q1_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (q1_event) {
        AlignedBuffer q1_buf(rnd_block_size);
        const uint32_t q1_ops = quick_mode ? 1500 : 5000;
        std::mt19937_64 q1_rng(42);
        std::uniform_int_distribution<uint64_t> q1_dist(0, max_blocks - 1);

        double total_lat_us = 0.0;
        double min_lat_us = 1e9;
        double max_lat_us = 0.0;
        uint32_t q1_completed = 0;

        LARGE_INTEGER q1_start_total, q1_end_total;
        QueryPerformanceCounter(&q1_start_total);

        for (uint32_t i = 0; i < q1_ops; ++i) {
            const uint64_t block_idx = q1_dist(q1_rng);
            const uint64_t byte_offset = block_idx * rnd_block_size;

            ResetEvent(q1_event);
            OVERLAPPED ov{};
            ov.hEvent = q1_event;
            ov.Offset = static_cast<DWORD>(byte_offset & 0xFFFFFFFF);
            ov.OffsetHigh = static_cast<DWORD>((byte_offset >> 32) & 0xFFFFFFFF);

            LARGE_INTEGER op_t0, op_t1;
            QueryPerformanceCounter(&op_t0);

            DWORD bytes_read = 0;
            BOOL ok = ReadFile(file_handle.get(), q1_buf.ptr, static_cast<DWORD>(rnd_block_size), &bytes_read, &ov);
            if (!ok && GetLastError() == ERROR_IO_PENDING) {
                WaitForSingleObject(q1_event, INFINITE);
                GetOverlappedResult(file_handle.get(), &ov, &bytes_read, FALSE);
            }
            QueryPerformanceCounter(&op_t1);

            double lat_us = (static_cast<double>(op_t1.QuadPart - op_t0.QuadPart) * 1000000.0) / static_cast<double>(freq.QuadPart);
            total_lat_us += lat_us;
            if (lat_us < min_lat_us) min_lat_us = lat_us;
            if (lat_us > max_lat_us) max_lat_us = lat_us;
            q1_completed++;
        }
        QueryPerformanceCounter(&q1_end_total);
        CloseHandle(q1_event);

        double q1_time = get_time_seconds(q1_start_total, q1_end_total, freq);
        if (q1_time > 0.0 && q1_completed > 0) {
            metrics.rnd_4k_q1t1_iops = static_cast<double>(q1_completed) / q1_time;
            metrics.rnd_4k_q1t1_mbps = (static_cast<double>(q1_completed * rnd_block_size) / (1024.0 * 1024.0)) / q1_time;
            metrics.rnd_4k_q1t1_avg_lat_us = total_lat_us / static_cast<double>(q1_completed);
            metrics.rnd_4k_q1t1_min_lat_us = min_lat_us;
            metrics.rnd_4k_q1t1_max_lat_us = max_lat_us;
        }
    }

    metrics.duration_seconds = seq_write_time + seq_read_time + rnd_time;
    return metrics;
}

} // namespace clustershift::core
