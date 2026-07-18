#include "benchmark_log.h"
#include "data_extraction_and_storage/data_extraction_storage.h"
#include "geo/pip_pipeline.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace bench {

// ============================================================
//  Small formatting helpers
// ============================================================

static std::string fmt_count(size_t n) {
    // 1234567 → "1,234,567"
    std::string s = std::to_string(n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, ",");
    return s;
}

static std::string fmt_bytes(size_t b) {
    char buf[64];
    if (b < 1024ULL) {
        snprintf(buf, sizeof(buf), "%zu B", b);
    } else if (b < 1024ULL * 1024) {
        snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
    } else if (b < 1024ULL * 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f MB", b / (1024.0 * 1024.0));
    } else {
        snprintf(buf, sizeof(buf), "%.2f GB", b / (1024.0 * 1024.0 * 1024.0));
    }
    return buf;
}

static std::string fmt_duration_ms(double ms) {
    char buf[64];
    if (ms < 1000.0) {
        snprintf(buf, sizeof(buf), "%.1f ms", ms);
    } else if (ms < 60000.0) {
        snprintf(buf, sizeof(buf), "%.2f s", ms / 1000.0);
    } else {
        int total_sec = (int)(ms / 1000.0);
        int m = total_sec / 60;
        int s = total_sec % 60;
        snprintf(buf, sizeof(buf), "%dm %02ds", m, s);
    }
    return buf;
}

static std::string fmt_double(double v, int prec = 2) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", prec, v);
    return buf;
}

// ============================================================
//  Timestamp helpers
// ============================================================

std::string timestamp_str(std::chrono::system_clock::time_point t) {
    std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H%M%S", &tm_buf);
    return buf;
}

static std::string readable_time(std::chrono::system_clock::time_point t) {
    std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

// ============================================================
//  Markdown sections
// ============================================================

static void section_des(std::ostream& os, const DES::Stats& s) {
    os << "## 1. Data Extraction (DES)\n\n";

    os << "### Raw counts\n\n";
    os << "| Metric | Value |\n"
       << "|---|---:|\n"
       << "| Nodes                | " << fmt_count(s.raw_node_count)         << " |\n"
       << "| Ways                 | " << fmt_count(s.raw_way_count)          << " |\n"
       << "| Relations (all)      | " << fmt_count(s.raw_relation_count)     << " |\n"
       << "| Boundary way IDs (pre-scan) | " << fmt_count(s.needed_way_ids_count) << " |\n\n";

    os << "### Processed counts\n\n";
    os << "| Semantic type | Count |\n"
       << "|---|---:|\n"
       << "| Points (POI / house centroids) | " << fmt_count(s.point_count)  << " |\n"
       << "| Lines (streets / rails / ...)  | " << fmt_count(s.line_count)   << " |\n"
       << "| Admin polygons                 | " << fmt_count(s.admin_count)  << " |\n\n";

    os << "### Timing\n\n";
    double total_ms = s.prescan_ms + s.pass1_ms + s.pass2_ms + s.processing_ms + s.storage_ms;
    os << "| Phase | Duration |\n"
       << "|---|---:|\n"
       << "| Pass 0 — pre-scan relations | " << fmt_duration_ms(s.prescan_ms)    << " |\n"
       << "| Pass 1 — nodes              | " << fmt_duration_ms(s.pass1_ms)      << " |\n"
       << "| Pass 2 — ways + relations   | " << fmt_duration_ms(s.pass2_ms)      << " |\n"
       << "| Processing (ring stitching) | " << fmt_duration_ms(s.processing_ms) << " |\n"
       << "| Storage (binary + JSON)     | " << fmt_duration_ms(s.storage_ms)    << " |\n"
       << "| **Extraction total** | **" << fmt_duration_ms(total_ms) << "** |\n\n";

    os << "### Memory (estimated peaks)\n\n";
    os << "| Component | Size |\n"
       << "|---|---:|\n"
       << "| Node cache         | " << fmt_bytes(s.node_cache_bytes)  << " |\n"
       << "| Way geo cache      | " << fmt_bytes(s.way_cache_bytes)   << " |\n"
       << "| Raw relations      | " << fmt_bytes(s.raw_rel_bytes)     << " |\n"
       << "| Points             | " << fmt_bytes(s.points_bytes)      << " |\n"
       << "| Lines              | " << fmt_bytes(s.lines_bytes)       << " |\n"
       << "| Admin areas        | " << fmt_bytes(s.admin_bytes)       << " |\n"
       << "| String pool        | " << fmt_bytes(s.string_pool_bytes) << " |\n\n";
}

static void section_hierarchy(std::ostream& os, const geo::HierarchyStats& s) {
    os << "## 2. PIP + Polygon Hierarchy\n\n";

    os << "### Structure\n\n";
    os << "| Metric | Value |\n"
       << "|---|---:|\n"
       << "| Polygon nodes (post-tier filter) | " << fmt_count(s.node_count)         << " |\n"
       << "| Dropped (non-tier admin levels)  | " << fmt_count(s.tier_dropped)       << " |\n"
       << "| Root polygons                    | " << fmt_count(s.root_count)         << " |\n"
       << "| Max hierarchy depth              | " << s.max_depth                     << " |\n"
       << "| Same-tier intersection pairs     | " << fmt_count(s.intersection_pairs) << " |\n\n";

    os << "### By tier\n\n";
    os << "| Tier | Count |\n"
       << "|---|---:|\n"
       << "| Country            | " << fmt_count(s.tier_country) << " |\n"
       << "| State              | " << fmt_count(s.tier_state)   << " |\n"
       << "| City               | " << fmt_count(s.tier_city)    << " |\n"
       << "| Suburb (9/10/11)   | " << fmt_count(s.tier_suburb)  << " |\n\n";

    os << "### Build timing\n\n";
    os << "| Phase | Duration |\n"
       << "|---|---:|\n"
       << "| Slabs (parallel)    | " << fmt_duration_ms(s.slab_build_ms) << " |\n"
       << "| Level grids         | " << fmt_duration_ms(s.level_grid_ms) << " |\n"
       << "| Hierarchy assembly  | " << fmt_duration_ms(s.hierarchy_ms)  << " |\n"
       << "| Intersect detection | " << fmt_duration_ms(s.intersect_ms)  << " |\n\n";
}

static void section_pip_bench(std::ostream& os, const geo::BenchResult& b) {
    os << "## 3. PIP Benchmark (top-down over `db.points`)\n\n";
    os << "| Metric | Value |\n"
       << "|---|---:|\n"
       << "| Queries            | " << fmt_count((size_t)b.n_queries)  << " |\n"
       << "| Total time         | " << fmt_duration_ms(b.total_ms)     << " |\n"
       << "| Avg per query      | " << fmt_double(b.avg_us, 3) << " µs |\n"
       << "| Empty results      | " << fmt_count((size_t)b.empty_hits) << " |\n"
       << "| Avg chain depth    | " << fmt_double(b.avg_depth, 2)      << " |\n\n";
}

static void one_layer(std::ostream& os, const char* label, const geo::LayerBench& l) {
    int64_t hits = l.queries - l.misses;
    os << "**" << label << "**\n\n";
    os << "| Metric | Value |\n"
       << "|---|---:|\n"
       << "| Queries (buildings) | " << fmt_count((size_t)l.queries)    << " |\n"
       << "| Misses (tier absent) | " << fmt_count((size_t)l.misses)     << " |\n"
       << "| Hits                | " << fmt_count((size_t)hits)          << " |\n"
       << "| Total time          | " << fmt_duration_ms(l.total_ms)      << " |\n"
       << "| Avg per hit         | " << fmt_double(l.avg_us, 3) << " µs |\n\n";
}

static void section_layered(std::ostream& os, const geo::LayeredBenchResult& r) {
    os << "## 4. Layered PIP Benchmark (per building)\n\n";
    os << "| Metric | Value |\n"
       << "|---|---:|\n"
       << "| Buildings tested       | " << fmt_count(r.building_count)          << " |\n"
       << "| Non-buildings skipped  | " << fmt_count(r.non_building_skipped)    << " |\n"
       << "| Setup time (untimed for bench) | " << fmt_duration_ms(r.setup_ms)   << " |\n\n";

    one_layer(os, "T1: building → city polygon",   r.t1_building_in_city);
    one_layer(os, "T2: city → state polygon",      r.t2_city_in_state);
    one_layer(os, "T3: state → country polygon",   r.t3_state_in_country);
}

static void section_attach(std::ostream& os, const geo::AttachAdminStats& s) {
    os << "## 5. Attach Admin Chain to Points\n\n";
    double pct = s.points_total ? 100.0 * s.points_with_any / s.points_total : 0.0;
    os << "| Metric | Value |\n"
       << "|---|---:|\n"
       << "| Points total         | " << fmt_count(s.points_total)                            << " |\n"
       << "| Points with any tag  | " << fmt_count(s.points_with_any) << " (" << fmt_double(pct, 1) << " %) |\n"
       << "| Country filled       | " << fmt_count(s.country_filled)                          << " |\n"
       << "| State filled         | " << fmt_count(s.state_filled)                            << " |\n"
       << "| City filled          | " << fmt_count(s.city_filled)                             << " |\n"
       << "| Suburb filled        | " << fmt_count(s.suburb_filled)                           << " |\n"
       << "| Total time           | " << fmt_duration_ms(s.total_ms)                          << " |\n\n";
}

// ============================================================
//  Main entry point
// ============================================================

std::string write_markdown(const ParseReport& r, const std::string& dir) {
    // Ensure the directory exists.
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    // Compose filename: <dataset>_<timestamp>.md
    std::string ts = timestamp_str(r.start_time);
    std::string sanitized_dataset = r.dataset.empty() ? "unknown" : r.dataset;
    // Filenames should not contain '/'
    for (auto& c : sanitized_dataset) if (c == '/' || c == '\\') c = '_';

    std::string sep = dir;
    if (!sep.empty() && sep.back() != '/' && sep.back() != '\\') sep += '/';
    std::string path = sep + sanitized_dataset + "_" + ts + ".md";

    std::ofstream os(path);
    if (!os) {
        std::cerr << "  benchmark: could not open " << path << " for writing\n";
        return {};
    }

    // ── Header ──
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                       r.end_time - r.start_time).count();
    int minutes = (int)(elapsed / 60), seconds = (int)(elapsed % 60);

    os << "# Parse Benchmark Report\n\n";
    os << "| Field | Value |\n"
       << "|---|---|\n"
       << "| Dataset          | `" << r.dataset  << "` |\n"
       << "| PBF source       | `" << r.pbf_path << "` |\n"
       << "| Started at       | " << readable_time(r.start_time) << " |\n"
       << "| Finished at      | " << readable_time(r.end_time)   << " |\n"
       << "| **Wall-clock**   | **" << minutes << "m " << (seconds<10?"0":"") << seconds << "s** |\n\n";

    // ── Sections ──
    if (r.des_stats)       section_des      (os, *r.des_stats);
    if (r.hierarchy_stats) section_hierarchy(os, *r.hierarchy_stats);
    if (r.pip_bench)       section_pip_bench(os, *r.pip_bench);
    if (r.layered_bench)   section_layered  (os, *r.layered_bench);
    if (r.attach_stats)    section_attach   (os, *r.attach_stats);

    os << "---\n";
    os << "*Generated by OSM_Geocoder benchmark_log.cpp.*\n";

    os.flush();
    return path;
}

} // namespace bench
