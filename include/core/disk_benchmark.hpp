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
    double rnd_4k_read_iops{0.0};
    double rnd_4k_read_mbps{0.0};
    uint64_t test_file_size_bytes{0};
    double duration_seconds{0.0};
};

class DiskBenchmark {
public:
    static std::expected<BenchmarkMetrics, DWORD> run_benchmark(
        wchar_t drive_letter,
        uint64_t test_size_bytes = 128 * 1024 * 1024, // 128 MB test file
        bool quick_mode = false
    );
};

} // namespace clustershift::core
