// ============================================================
//  Benchmark log writer.
//
//  Every parse produces a Markdown report under
//    data/benchmark/<dataset>_<YYYY-MM-DD_HHMMSS>.md
//
//  The file is easy to open in any text viewer / GitHub UI and
//  summarises: raw counts, per-phase timing, memory estimates,
//  PIP + hierarchy stats, the three layered PIP benchmarks, and
//  the admin-chain attachment stats.
// ============================================================
#pragma once
#include <chrono>
#include <string>

// Forward declarations — full headers included in the .cpp only.
namespace DES { struct Stats; }
namespace geo {
    struct HierarchyStats;
    struct BenchResult;
    struct LayeredBenchResult;
    struct AttachAdminStats;
}

namespace bench {

struct ParseReport {
    std::string dataset;
    std::string pbf_path;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;

    const DES::Stats*                    des_stats       = nullptr;
    const geo::HierarchyStats*           hierarchy_stats = nullptr;
    const geo::BenchResult*              pip_bench       = nullptr;
    const geo::LayeredBenchResult*       layered_bench   = nullptr;
    const geo::AttachAdminStats*         attach_stats    = nullptr;
};

// Write the report to `dir` (created if missing). Returns the path
// of the file that was written, or empty on failure.
std::string write_markdown(const ParseReport& r, const std::string& dir);

// "2026-07-18_143045" — used as the timestamp segment of the filename.
std::string timestamp_str(std::chrono::system_clock::time_point t);

} // namespace bench
