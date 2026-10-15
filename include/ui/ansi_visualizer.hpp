#pragma once

#include <iostream>
#include <vector>
#include <cstdint>
#include <string>

namespace clustershift::ui {

class AnsiVisualizer {
public:
    static void render_cluster_grid(
        const std::vector<uint8_t>& bitmap_data,
        uint64_t total_clusters,
        size_t columns = 64,
        size_t rows = 8
    ) {
        if (total_clusters == 0 || bitmap_data.empty()) return;

        const size_t total_cells = columns * rows;
        const uint64_t clusters_per_cell = (total_clusters + total_cells - 1) / total_cells;

        std::wcout << L"\n\033[1;37m=== Volume Cluster Allocation Map ===\033[0m\n";
        std::wcout << L"\033[90m+";
        for (size_t c = 0; c < columns; ++c) std::wcout << L"-";
        std::wcout << L"+\033[0m\n";

        for (size_t r = 0; r < rows; ++r) {
            std::wcout << L"\033[90m|\033[0m";
            for (size_t c = 0; c < columns; ++c) {
                const uint64_t cell_start = (r * columns + c) * clusters_per_cell;
                if (cell_start >= total_clusters) {
                    std::wcout << L" ";
                    continue;
                }

                const uint64_t cell_end = (std::min)(cell_start + clusters_per_cell, total_clusters);
                uint64_t free_in_cell = 0;

                for (uint64_t i = cell_start; i < cell_end; ++i) {
                    const size_t byte_idx = static_cast<size_t>(i / 8);
                    const uint8_t bit_idx = static_cast<uint8_t>(i % 8);
                    if (byte_idx < bitmap_data.size() && ((bitmap_data[byte_idx] & (1 << bit_idx)) == 0)) {
                        ++free_in_cell;
                    }
                }

                const double free_ratio = static_cast<double>(free_in_cell) / static_cast<double>(cell_end - cell_start);
                if (free_ratio > 0.85) {
                    std::wcout << L"\033[1;32m░\033[0m"; // mostly free
                } else if (free_ratio > 0.40) {
                    std::wcout << L"\033[1;33m▒\033[0m"; // mixed
                } else {
                    std::wcout << L"\033[1;36m█\033[0m"; // allocated / dense
                }
            }
            std::wcout << L"\033[90m|\033[0m\n";
        }

        std::wcout << L"\033[90m+";
        for (size_t c = 0; c < columns; ++c) std::wcout << L"-";
        std::wcout << L"+\033[0m\n";

        std::wcout << L"Legend: \033[1;36m█\033[0m Allocated  \033[1;33m▒\033[0m Partially Free  \033[1;32m░\033[0m Contiguous Free Space\n\n";
    }
};

} // namespace clustershift::ui
