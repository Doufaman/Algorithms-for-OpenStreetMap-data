#include "pip_pipeline.h"
#include "data_extraction_and_storage/data_extraction_storage.h"

#include <chrono>
#include <iomanip>
#include <iostream>

namespace geo {

using Clock = std::chrono::steady_clock;

void build_pip_for_db(const DES::OsmData& db, PolygonHierarchy& H) {
    H.build(db.admin_areas);
}

// ───────────────────────── attach_admin_to_db ───────────────
//
// Three-phase fill, in priority order:
//
//   Phase 0 — OSM addr:* tags already populated p.country_off / state_off
//             / city_off / suburb_off during PBF parse. Those are kept.
//
//   Phase 1 — Hierarchy top-down chain. Fast, gives consistent ancestry
//             when the polygon outer-rings are well-formed.
//
//   Phase 2 — Independent per-level PIP via level_grids_. Robust to
//             broken hierarchy linkages (admin polygons whose outer
//             ring is mis-assembled and so PIP-excludes some children).
//
AttachAdminStats attach_admin_to_db(DES::OsmData& db, const PolygonHierarchy& H) {
    AttachAdminStats s;
    auto t0 = Clock::now();
    s.points_total = db.points.size();

    // Helper: try to fill one slot by doing PIP at the given admin level.
    auto try_pip_fill = [&](int32_t lat, int32_t lon, uint8_t lvl,
                            uint32_t& slot) -> bool {
        if (slot != DES::StringPool::NONE) return false;
        uint32_t poly_id = H.pip_at_level(lat, lon, lvl);
        if (poly_id == INVALID_ID) return false;
        const auto& a = db.admin_areas[H.nodes()[poly_id].admin_idx];
        if (a.name_off == DES::StringPool::NONE) return false;
        slot = a.name_off;
        return true;
    };

    for (auto& p : db.points) {
        bool any = false;

        // Track whether we filled in this iteration (avoid double-count)
        bool had_country = p.country_off != DES::StringPool::NONE;
        bool had_state   = p.state_off   != DES::StringPool::NONE;
        bool had_city    = p.city_off    != DES::StringPool::NONE;
        bool had_suburb  = p.suburb_off  != DES::StringPool::NONE;

        // ── Phase 1: hierarchy top-down chain ────────────────
        PIPResult chain = H.query(p.lat_e7, p.lon_e7);
        uint8_t deepest_suburb = 0;
        for (uint32_t node_id : chain.chain) {
            const PolygonNode& n = H.nodes()[node_id];
            const DES::AdminArea& a = db.admin_areas[n.admin_idx];
            if (a.name_off == DES::StringPool::NONE) continue;

            switch (n.tier) {
                case Tier::COUNTRY:
                    if (p.country_off == DES::StringPool::NONE) p.country_off = a.name_off;
                    break;
                case Tier::STATE:
                    if (p.state_off == DES::StringPool::NONE) p.state_off = a.name_off;
                    break;
                case Tier::CITY:
                    if (p.city_off == DES::StringPool::NONE) p.city_off = a.name_off;
                    break;
                case Tier::SUBURB:
                    if (p.suburb_off == DES::StringPool::NONE ||
                        n.admin_level > deepest_suburb) {
                        p.suburb_off    = a.name_off;
                        deepest_suburb  = n.admin_level;
                    }
                    break;
                default: break;
            }
        }

        // ── Phase 2: independent per-level PIP for missing tiers ──
        try_pip_fill(p.lat_e7, p.lon_e7, 2, p.country_off);
        try_pip_fill(p.lat_e7, p.lon_e7, 4, p.state_off);
        try_pip_fill(p.lat_e7, p.lon_e7, 8, p.city_off);
        if (p.suburb_off == DES::StringPool::NONE) {
            try_pip_fill(p.lat_e7, p.lon_e7, 10, p.suburb_off);
        }
        if (p.suburb_off == DES::StringPool::NONE) {
            try_pip_fill(p.lat_e7, p.lon_e7, 9, p.suburb_off);
        }
        if (p.suburb_off == DES::StringPool::NONE) {
            try_pip_fill(p.lat_e7, p.lon_e7, 11, p.suburb_off);
        }

        // ── Count what's filled (after both phases) ──
        if (!had_country && p.country_off != DES::StringPool::NONE) { ++s.country_filled; any = true; }
        if (!had_state   && p.state_off   != DES::StringPool::NONE) { ++s.state_filled;   any = true; }
        if (!had_city    && p.city_off    != DES::StringPool::NONE) { ++s.city_filled;    any = true; }
        if (!had_suburb  && p.suburb_off  != DES::StringPool::NONE) { ++s.suburb_filled;  any = true; }
        if (any || had_country || had_state || had_city || had_suburb) ++s.points_with_any;
    }

    s.total_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                     Clock::now() - t0).count() / 1000.0;
    return s;
}

void print_attach_admin_report(const AttachAdminStats& s) {
    double pct = s.points_total ? (100.0 * s.points_with_any / s.points_total) : 0.0;
    std::cout << std::fixed << std::setprecision(2)
        << "\n"
        << "╔══════════════════════════════════════════════╗\n"
        << "║      ATTACH ADMIN to db.points (Task 2)       ║\n"
        << "╠══════════════════════════════════════════════╣\n"
        << "║  Points total       : " << std::setw(10) << s.points_total    << "\n"
        << "║  Points w/ any tag  : " << std::setw(10) << s.points_with_any << "  (" << pct << " %)\n"
        << "║  Country filled     : " << std::setw(10) << s.country_filled  << "\n"
        << "║  State   filled     : " << std::setw(10) << s.state_filled    << "\n"
        << "║  City    filled     : " << std::setw(10) << s.city_filled     << "\n"
        << "║  Suburb  filled     : " << std::setw(10) << s.suburb_filled   << "\n"
        << "║  Total time         : " << std::setw(10) << s.total_ms        << " ms\n"
        << "╚══════════════════════════════════════════════╝\n";
}

BenchResult benchmark_pip(const DES::OsmData& db, const PolygonHierarchy& H) {
    BenchResult b;
    auto t0 = Clock::now();
    int64_t depth_sum = 0;

    for (const auto& p : db.points) {
        PIPResult r = H.query(p.lat_e7, p.lon_e7);
        ++b.n_queries;
        if (r.chain.empty()) ++b.empty_hits;
        depth_sum += (int64_t)r.chain.size();
    }
    b.total_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                     Clock::now() - t0).count() / 1000.0;
    b.avg_us   = b.n_queries ? (b.total_ms * 1000.0 / (double)b.n_queries) : 0.0;
    b.avg_depth= b.n_queries ? ((double)depth_sum / (double)b.n_queries)   : 0.0;
    return b;
}

void print_hierarchy_report(const PolygonHierarchy& H, const BenchResult& b) {
    const auto& s = H.stats();
    std::cout << std::fixed << std::setprecision(2)
        << "\n"
        << "╔══════════════════════════════════════════════╗\n"
        << "║          PIP + Hierarchy Stats                ║\n"
        << "╠══════════════════════════════════════════════╣\n"
        << "║  COUNTS                                       ║\n"
        << "║    Polygon nodes      : " << std::setw(10) << s.node_count          << "\n"
        << "║    Dropped (non-tier) : " << std::setw(10) << s.tier_dropped        << "\n"
        << "║    Roots              : " << std::setw(10) << s.root_count          << "\n"
        << "║    Max depth          : " << std::setw(10) << s.max_depth           << "\n"
        << "║    Intersection pairs : " << std::setw(10) << s.intersection_pairs  << "\n"
        << "╠══════════════════════════════════════════════╣\n"
        << "║  PER TIER                                     ║\n"
        << "║    Country            : " << std::setw(10) << s.tier_country << "\n"
        << "║    State              : " << std::setw(10) << s.tier_state   << "\n"
        << "║    City               : " << std::setw(10) << s.tier_city    << "\n"
        << "║    Suburb (9/10/11)   : " << std::setw(10) << s.tier_suburb  << "\n"
        << "╠══════════════════════════════════════════════╣\n"
        << "║  BUILD TIMINGS                                ║\n"
        << "║    Slabs (parallel) : " << std::setw(10) << s.slab_build_ms << " ms\n"
        << "║    Level grids      : " << std::setw(10) << s.level_grid_ms << " ms\n"
        << "║    Hierarchy        : " << std::setw(10) << s.hierarchy_ms  << " ms\n"
        << "║    Intersect detect : " << std::setw(10) << s.intersect_ms  << " ms\n"
        << "╠══════════════════════════════════════════════╣\n"
        << "║  PIP BENCHMARK (over db.points)               ║\n"
        << "║    Queries          : " << std::setw(10) << b.n_queries  << "\n"
        << "║    Total time       : " << std::setw(10) << b.total_ms   << " ms\n"
        << "║    Avg per query    : " << std::setw(10) << b.avg_us     << " us\n"
        << "║    Empty results    : " << std::setw(10) << b.empty_hits << "\n"
        << "║    Avg chain depth  : " << std::setw(10) << b.avg_depth  << "\n"
        << "╚══════════════════════════════════════════════╝\n";

    // Print a few sample intersection pairs (data-quality signal).
    if (!H.intersections().empty()) {
        std::cout << "\n  First intersection pairs (admin_idx):\n";
        size_t show = std::min<size_t>(5, H.intersections().size());
        for (size_t i = 0; i < show; ++i) {
            auto [a, b] = H.intersections()[i];
            std::cout << "    " << H.nodes()[a].admin_idx
                      << " <-> "
                      << H.nodes()[b].admin_idx
                      << "  (admin_level=" << (int)H.nodes()[a].admin_level << ")\n";
        }
    }
}

// ───────────────────────────────────────────────────────────
//  Layered PIP benchmark
// ───────────────────────────────────────────────────────────

namespace {

// A point counts as a "building" if it carries some addressable
// or type tag. This catches both addr-tagged nodes and centroids
// of way-based building/amenity/leisure features.
bool is_building(const DES::Point& p) {
    return p.type_off       != DES::StringPool::NONE
        || p.street_off     != DES::StringPool::NONE
        || p.housenumber_off!= DES::StringPool::NONE;
}

struct ChainTiers {
    uint32_t city_node    = INVALID_ID;
    uint32_t state_node   = INVALID_ID;
    uint32_t country_node = INVALID_ID;
};

} // anonymous namespace

LayeredBenchResult benchmark_layered_pip(const DES::OsmData& db,
                                         const PolygonHierarchy& H) {
    LayeredBenchResult r;

    // ── Setup (untimed): collect buildings and pre-resolve their chains ──
    auto t_setup = Clock::now();
    std::vector<uint32_t> buildings;
    buildings.reserve(db.points.size());
    for (uint32_t i = 0; i < db.points.size(); ++i) {
        if (is_building(db.points[i])) buildings.push_back(i);
        else                            ++r.non_building_skipped;
    }
    r.building_count = buildings.size();

    std::vector<ChainTiers> tiers(buildings.size());
    for (size_t k = 0; k < buildings.size(); ++k) {
        const auto& p = db.points[buildings[k]];
        PIPResult chain = H.query(p.lat_e7, p.lon_e7);
        ChainTiers ct;
        for (uint32_t node_id : chain.chain) {
            switch (H.nodes()[node_id].tier) {
                case Tier::CITY:    ct.city_node    = node_id; break;
                case Tier::STATE:   ct.state_node   = node_id; break;
                case Tier::COUNTRY: ct.country_node = node_id; break;
                default: break;
            }
        }
        tiers[k] = ct;
    }
    r.setup_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                     Clock::now() - t_setup).count() / 1000.0;

    // Helper: time a per-building inner PIP and report.
    auto run_layer = [&](LayerBench& lb,
                         auto&& src_point,        // (size_t k) → (lat, lon)
                         auto&& target_node) {    // (size_t k) → uint32_t (node id)
        auto t0 = Clock::now();
        int64_t sink = 0, misses = 0;
        for (size_t k = 0; k < buildings.size(); ++k) {
            uint32_t tgt = target_node(k);
            if (tgt == INVALID_ID) { ++misses; continue; }
            auto [lat, lon] = src_point(k);
            // sink the bool to prevent dead-code elimination
            sink += pip_single(H.nodes()[tgt], lat, lon) ? 1 : 0;
        }
        lb.total_ms  = std::chrono::duration_cast<std::chrono::microseconds>(
                           Clock::now() - t0).count() / 1000.0;
        lb.queries   = (int64_t)buildings.size();
        lb.misses    = misses;
        lb.hit_count = sink;
        int64_t denom = lb.queries - lb.misses;
        lb.avg_us    = denom > 0 ? (lb.total_ms * 1000.0 / (double)denom) : 0.0;
    };

    // ── T1: building → city ──────────────────────────────────
    run_layer(r.t1_building_in_city,
              [&](size_t k){
                  const auto& p = db.points[buildings[k]];
                  return std::pair<int32_t,int32_t>{p.lat_e7, p.lon_e7};
              },
              [&](size_t k){ return tiers[k].city_node; });

    // ── T2: city → state ─────────────────────────────────────
    run_layer(r.t2_city_in_state,
              [&](size_t k){
                  uint32_t cn = tiers[k].city_node;
                  if (cn == INVALID_ID) return std::pair<int32_t,int32_t>{0, 0};
                  const auto& c = H.nodes()[cn];
                  return std::pair<int32_t,int32_t>{c.rep_lat, c.rep_lon};
              },
              [&](size_t k){
                  // Need both: city to source point + state target.
                  // If city missing we'll miss anyway, but be explicit:
                  return tiers[k].city_node == INVALID_ID
                       ? INVALID_ID : tiers[k].state_node;
              });

    // ── T3: state → country ──────────────────────────────────
    run_layer(r.t3_state_in_country,
              [&](size_t k){
                  uint32_t sn = tiers[k].state_node;
                  if (sn == INVALID_ID) return std::pair<int32_t,int32_t>{0, 0};
                  const auto& s = H.nodes()[sn];
                  return std::pair<int32_t,int32_t>{s.rep_lat, s.rep_lon};
              },
              [&](size_t k){
                  return tiers[k].state_node == INVALID_ID
                       ? INVALID_ID : tiers[k].country_node;
              });

    return r;
}

void print_layered_bench(const LayeredBenchResult& r) {
    auto layer = [](const char* name, const LayerBench& lb) {
        int64_t hits = lb.queries - lb.misses;
        std::cout << "║  " << name << "\n"
                  << "║    queries (buildings) : " << std::setw(10) << lb.queries  << "\n"
                  << "║    misses              : " << std::setw(10) << lb.misses
                  << "   (tier not in chain)\n"
                  << "║    hits                : " << std::setw(10) << hits        << "\n"
                  << "║    total time          : " << std::setw(10) << lb.total_ms << " ms\n"
                  << "║    avg per query (hit) : " << std::setw(10) << lb.avg_us   << " us\n"
                  << "║    pip_single == true  : " << std::setw(10) << lb.hit_count
                  << "   (anti-DCE sink)\n";
    };

    std::cout << std::fixed << std::setprecision(2)
        << "\n"
        << "╔══════════════════════════════════════════════╗\n"
        << "║       LAYERED PIP BENCHMARK (per building)    ║\n"
        << "╠══════════════════════════════════════════════╣\n"
        << "║  Buildings tested      : " << std::setw(10) << r.building_count        << "\n"
        << "║  Non-buildings skipped : " << std::setw(10) << r.non_building_skipped  << "\n"
        << "║  Setup (untimed)       : " << std::setw(10) << r.setup_ms              << " ms\n"
        << "╠══════════════════════════════════════════════╣\n";
    layer("T1: building → city polygon",   r.t1_building_in_city);
    std::cout << "╠══════════════════════════════════════════════╣\n";
    layer("T2: city → state polygon",      r.t2_city_in_state);
    std::cout << "╠══════════════════════════════════════════════╣\n";
    layer("T3: state → country polygon",   r.t3_state_in_country);
    std::cout << "╚══════════════════════════════════════════════╝\n";
}

// Run a handful of reverse-geocode queries on the first few houses,
// just to sanity-check that AddressInfo is being filled correctly.
void print_sample_addresses(const DES::OsmData& db, const PolygonHierarchy& H,
                            size_t n_samples) {
    std::cout << "\n  Sample reverse-geocode (first " << n_samples << " points with admin chain):\n";
    size_t shown = 0;
    for (const auto& p : db.points) {
        if (shown >= n_samples) break;
        AddressInfo a = H.query_address(p.lat_e7, p.lon_e7, db);
        if (!a.any()) continue;
        std::cout << "    (" << (p.lat_e7 / 1e7) << ", " << (p.lon_e7 / 1e7) << ")  →  "
                  << a.format() << "\n";
        ++shown;
    }
    if (shown == 0)
        std::cout << "    (none — no point fell inside any tier-classified polygon)\n";
}

} // namespace geo
