// ============================================================
//  Convenience wrappers + benchmarking + stats report.
//  Glues the PIP / hierarchy code to the DES::OsmData pipeline.
// ============================================================
#pragma once
#include "polygon_hierarchy.h"

namespace DES { struct OsmData; }

namespace geo {

struct BenchResult {
    int64_t n_queries  = 0;
    int64_t empty_hits = 0;     // queries returning empty chain
    double  total_ms   = 0;
    double  avg_us     = 0;
    double  avg_depth  = 0;
};

// Build PIP + hierarchy from the DB's admin_areas.
void build_pip_for_db(const DES::OsmData& db, PolygonHierarchy& H);

// Sheet 2 Task 2: walk db.points, run hierarchy.query() on each, and
// store country/state/city/suburb name offsets back into the Point.
// Returns the number of points that received at least one admin tag.
struct AttachAdminStats {
    size_t points_total      = 0;
    size_t points_with_any   = 0;
    size_t country_filled    = 0;
    size_t state_filled      = 0;
    size_t city_filled       = 0;
    size_t suburb_filled     = 0;
    double total_ms          = 0;
};

AttachAdminStats attach_admin_to_db(DES::OsmData& db, const PolygonHierarchy& H);

void print_attach_admin_report(const AttachAdminStats& s);

// Run the top-down PIP for every point in db.points (≈ houses + POI).
// Single-threaded so the per-query timing reflects the data structure,
// not the parallel scheduler.
BenchResult benchmark_pip(const DES::OsmData& db, const PolygonHierarchy& H);

// Pretty-print stats to std::cout.
void print_hierarchy_report(const PolygonHierarchy& H, const BenchResult& b);

// Run a few sample query_address() calls and print the resulting addresses.
void print_sample_addresses(const DES::OsmData& db, const PolygonHierarchy& H,
                            size_t n_samples = 5);

// ───────────────────────────────────────────────────────────
//  Layered PIP benchmark (per building, 3 polygon size classes)
//
//  T1: PIP( building_point , city_polygon    )
//  T2: PIP( city_rep_point , state_polygon   )
//  T3: PIP( state_rep_point, country_polygon )
//
//  All three iterate over the same set of buildings so that
//  raw counts are directly comparable. Setup (hierarchy chain
//  lookup) is excluded from the timed loops.
// ───────────────────────────────────────────────────────────
struct LayerBench {
    int64_t queries   = 0;
    int64_t misses    = 0;   // tier not found in chain
    double  total_ms  = 0;
    double  avg_us    = 0;
    int64_t hit_count = 0;   // sink for pip_single results (anti-DCE)
};

struct LayeredBenchResult {
    size_t     building_count = 0;
    size_t     non_building_skipped = 0;
    LayerBench t1_building_in_city;
    LayerBench t2_city_in_state;
    LayerBench t3_state_in_country;
    double     setup_ms = 0;
};

LayeredBenchResult benchmark_layered_pip(const DES::OsmData& db,
                                         const PolygonHierarchy& H);

void print_layered_bench(const LayeredBenchResult& r);

} // namespace geo
