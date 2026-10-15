#pragma once

#include "scoped_handle.hpp"
#include <windows.h>
#include <string>
#include <cstdint>
#include <expected>
#include <filesystem>

namespace clustershift::core {

struct BenchmarkMetrics {
    double seq_write_mbps{0.0};
    double seq_read_mbps{0.0};

    // Multi-threaded RND4K (e.g. Q32Tx)
    double rnd_4k_read_iops{0.0};
    double rnd_4k_read_mbps{0.0};
    uint32_t queue_depth{32};
    uint32_t thread_count{1};

    // Single-thread Latency RND4K (Q1T1)
    double rnd_4k_q1t1_iops{0.0};
    double rnd_4k_q1t1_mbps{0.0};
    double rnd_4k_q1t1_avg_lat_us{0.0};
    double rnd_4k_q1t1_min_lat_us{0.0};
    double rnd_4k_q1t1_max_lat_us{0.0};

    uint64_t test_file_size_bytes{0};
    double duration_seconds{0.0};
};

class DiskBenchmark {
public:
    static std::expected<BenchmarkMetrics, DWORD> run_benchmark(
        wchar_t drive_letter,
        uint64_t test_size_bytes = 1024ULL * 1024 * 1024, // 1 GB test file default
        bool quick_mode = false,
        uint32_t threads = 0 // 0 = auto-detect hardware concurrency
    );
};

} // namespace clustershift::core
