#include "polygon_hierarchy.h"
#include "data_extraction_and_storage/data_extraction_storage.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace geo {

using Clock = std::chrono::steady_clock;
static double ms_since(std::chrono::time_point<Clock> t) {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t).count() / 1000.0;
}

// ───────────────────────── Phase 1: slab build ──────────────
//
// Filters admins to keep only those with a target tier (COUNTRY,
// STATE, CITY, SUBURB) and builds slab indices for them only.
// Polygons outside the target tier set are silently dropped.
//
void PolygonHierarchy::build_slabs(const std::vector<DES::AdminArea>& admins) {
    auto t0 = Clock::now();

    // Phase 1a: pick out the admins we care about.
    std::vector<uint32_t> kept;
    kept.reserve(admins.size());
    size_t dropped = 0;
    for (uint32_t i = 0; i < admins.size(); ++i) {
        if (admin_level_to_tier(admins[i].admin_level) != Tier::NONE)
            kept.push_back(i);
        else
            ++dropped;
    }
    stats_.tier_dropped = dropped;

    // Phase 1b: parallel slab construction.
    nodes_.assign(kept.size(), {});
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 64)
#endif
    for (long long k = 0; k < (long long)kept.size(); ++k) {
        uint32_t admin_idx = kept[(size_t)k];
        build_slab_node(admins[admin_idx], admin_idx, nodes_[(size_t)k]);
    }

    // Phase 1c: tally tier counts for the report.
    for (const auto& n : nodes_) {
        switch (n.tier) {
            case Tier::COUNTRY: ++stats_.tier_country; break;
            case Tier::STATE:   ++stats_.tier_state;   break;
            case Tier::CITY:    ++stats_.tier_city;    break;
            case Tier::SUBURB:  ++stats_.tier_suburb;  break;
            default: break;
        }
    }

    stats_.slab_build_ms = ms_since(t0);
}

// ───────────────────────── Phase 2: per-level grids ─────────
void PolygonHierarchy::build_level_grids() {
    auto t0 = Clock::now();
    std::unordered_map<uint8_t, std::vector<uint32_t>> by_level;
    by_level.reserve(16);
    for (uint32_t i = 0; i < nodes_.size(); ++i)
        by_level[nodes_[i].admin_level].push_back(i);

    for (auto& [level, ids] : by_level) {
        int32_t mn_la = INT32_MAX, mx_la = INT32_MIN;
        int32_t mn_lo = INT32_MAX, mx_lo = INT32_MIN;
        for (uint32_t id : ids) {
            const auto& n = nodes_[id];
            if (n.min_lat < mn_la) mn_la = n.min_lat;
            if (n.max_lat > mx_la) mx_la = n.max_lat;
            if (n.min_lon < mn_lo) mn_lo = n.min_lon;
            if (n.max_lon > mx_lo) mx_lo = n.max_lon;
        }
        int n_cells = std::clamp((int)(std::sqrt((double)ids.size()) * 2.0), 4, 128);
        GridIndex g;
        g.build(mn_la, mx_la, mn_lo, mx_lo, n_cells, n_cells);
        for (uint32_t id : ids) {
            const auto& n = nodes_[id];
            g.insert_bbox(id, n.min_lat, n.max_lat, n.min_lon, n.max_lon);
        }
        level_grids_[level] = std::move(g);
    }
    stats_.level_grid_ms = ms_since(t0);
}

// ───────────────────────── Phase 3: hierarchy ───────────────
void PolygonHierarchy::build_hierarchy() {
    auto t0 = Clock::now();

    // Distinct admin levels, ascending (2, 4, 6, ...)
    std::vector<uint8_t> levels_asc;
    levels_asc.reserve(level_grids_.size());
    for (auto& kv : level_grids_) levels_asc.push_back(kv.first);
    std::sort(levels_asc.begin(), levels_asc.end());

    // Process nodes ordered by their admin_level
    std::vector<uint32_t> order(nodes_.size());
    for (uint32_t i = 0; i < nodes_.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        return nodes_[a].admin_level < nodes_[b].admin_level;
    });

    std::vector<uint32_t> cand;
    cand.reserve(16);

    for (uint32_t id : order) {
        PolygonNode& self = nodes_[id];
        int32_t lat = self.rep_lat, lon = self.rep_lon;

        uint32_t best_parent = INVALID_ID;
        uint8_t  best_level  = 0;
        int64_t  best_area   = INT64_MAX;

        for (uint8_t lvl : levels_asc) {
            if (lvl >= self.admin_level) break;
            auto it = level_grids_.find(lvl);
            if (it == level_grids_.end()) continue;
            cand.clear();
            it->second.query_point(lat, lon, cand);
            std::sort(cand.begin(), cand.end());
            cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
            for (uint32_t cid : cand) {
                if (cid == id) continue;
                const PolygonNode& p = nodes_[cid];
                if (!bbox_contains(p, lat, lon)) continue;
                if (!pip_single(p, lat, lon))    continue;
                // Prefer deepest level; within same level, smallest area.
                if (p.admin_level >  best_level ||
                   (p.admin_level == best_level && p.area_abs < best_area)) {
                    best_parent = cid;
                    best_level  = p.admin_level;
                    best_area   = p.area_abs;
                }
            }
        }
        self.parent = best_parent;
        if (best_parent != INVALID_ID) nodes_[best_parent].children.push_back(id);
        else                            roots_.push_back(id);
    }
    stats_.hierarchy_ms = ms_since(t0);
}

// ───────── Phase 4: same-level intersection detection ───────
//
// Only flags PROPER crossings — admin polygons that share a border
// (touch but don't cross) produce no false positives.
//
static bool segments_proper_cross(int32_t ax0, int32_t ay0,
                                  int32_t ax1, int32_t ay1,
                                  int32_t bx0, int32_t by0,
                                  int32_t bx1, int32_t by1) {
    auto sgn = [](int64_t v) -> int { return v > 0 ? 1 : (v < 0 ? -1 : 0); };
    auto cross = [](int32_t ox, int32_t oy,
                    int32_t px, int32_t py,
                    int32_t qx, int32_t qy) -> int64_t {
        return (int64_t)(px - ox) * (qy - oy) - (int64_t)(py - oy) * (qx - ox);
    };
    int d1 = sgn(cross(bx0, by0, bx1, by1, ax0, ay0));
    int d2 = sgn(cross(bx0, by0, bx1, by1, ax1, ay1));
    int d3 = sgn(cross(ax0, ay0, ax1, ay1, bx0, by0));
    int d4 = sgn(cross(ax0, ay0, ax1, ay1, bx1, by1));
    return (d1 != 0 && d2 != 0 && d1 != d2) &&
           (d3 != 0 && d4 != 0 && d3 != d4);
}

static bool rings_intersect(const std::vector<std::pair<int32_t,int32_t>>& ra,
                            const std::vector<std::pair<int32_t,int32_t>>& rb) {
    const size_t na = ra.size(), nb = rb.size();
    if (na < 2 || nb < 2) return false;
    for (size_t i = 0; i < na; ++i) {
        size_t j = (i + 1) % na;
        int32_t ax0 = ra[i].second, ay0 = ra[i].first;
        int32_t ax1 = ra[j].second, ay1 = ra[j].first;
        int32_t a_mn_x = std::min(ax0, ax1), a_mx_x = std::max(ax0, ax1);
        int32_t a_mn_y = std::min(ay0, ay1), a_mx_y = std::max(ay0, ay1);
        for (size_t k = 0; k < nb; ++k) {
            size_t l = (k + 1) % nb;
            int32_t bx0 = rb[k].second, by0 = rb[k].first;
            int32_t bx1 = rb[l].second, by1 = rb[l].first;
            int32_t b_mn_x = std::min(bx0, bx1), b_mx_x = std::max(bx0, bx1);
            int32_t b_mn_y = std::min(by0, by1), b_mx_y = std::max(by0, by1);
            if (b_mx_x < a_mn_x || b_mn_x > a_mx_x) continue;
            if (b_mx_y < a_mn_y || b_mn_y > a_mx_y) continue;
            if (segments_proper_cross(ax0, ay0, ax1, ay1,
                                      bx0, by0, bx1, by1)) return true;
        }
    }
    return false;
}

void PolygonHierarchy::detect_intersections(const std::vector<DES::AdminArea>& admins) {
    auto t0 = Clock::now();
    std::unordered_map<uint8_t, std::vector<uint32_t>> by_level;
    by_level.reserve(16);
    for (uint32_t i = 0; i < nodes_.size(); ++i)
        by_level[nodes_[i].admin_level].push_back(i);

    std::vector<uint32_t> cand;
    cand.reserve(32);
    for (auto& [lvl, ids] : by_level) {
        auto git = level_grids_.find(lvl);
        if (git == level_grids_.end()) continue;
        const GridIndex& g = git->second;

        for (uint32_t a : ids) {
            const PolygonNode& na = nodes_[a];
            cand.clear();
            g.query_bbox(na.min_lat, na.max_lat, na.min_lon, na.max_lon, cand);
            std::sort(cand.begin(), cand.end());
            cand.erase(std::unique(cand.begin(), cand.end()), cand.end());

            for (uint32_t b : cand) {
                if (b <= a) continue;     // each pair tested once
                const PolygonNode& nb = nodes_[b];
                if (na.max_lat < nb.min_lat || na.min_lat > nb.max_lat) continue;
                if (na.max_lon < nb.min_lon || na.min_lon > nb.max_lon) continue;
                if (rings_intersect(admins[na.admin_idx].outer_ring,
                                    admins[nb.admin_idx].outer_ring)) {
                    intersections_.emplace_back(a, b);
                }
            }
        }
    }
    stats_.intersect_ms       = ms_since(t0);
    stats_.intersection_pairs = intersections_.size();
}

// ───────────────────────── Query ────────────────────────────
void PolygonHierarchy::descend(uint32_t id, int32_t lat, int32_t lon, PIPResult& r) const {
    r.chain.push_back(id);
    for (uint32_t c : nodes_[id].children) {
        const auto& ch = nodes_[c];
        if (!bbox_contains(ch, lat, lon)) continue;
        if (!pip_single(ch, lat, lon))    continue;
        descend(c, lat, lon, r);
        break;     // siblings expected disjoint
    }
}

// ───────── Independent per-level PIP (bypasses hierarchy) ───
uint32_t PolygonHierarchy::pip_at_level(int32_t lat, int32_t lon,
                                        uint8_t admin_level) const {
    auto it = level_grids_.find(admin_level);
    if (it == level_grids_.end()) return INVALID_ID;

    std::vector<uint32_t> cand;
    it->second.query_point(lat, lon, cand);
    if (cand.empty()) return INVALID_ID;
    std::sort(cand.begin(), cand.end());
    cand.erase(std::unique(cand.begin(), cand.end()), cand.end());

    uint32_t best = INVALID_ID;
    int64_t  best_area = INT64_MAX;
    for (uint32_t id : cand) {
        const PolygonNode& n = nodes_[id];
        if (!bbox_contains(n, lat, lon)) continue;
        if (!pip_single(n, lat, lon))    continue;
        if (n.area_abs < best_area) {
            best_area = n.area_abs;
            best      = id;
        }
    }
    return best;
}

PIPResult PolygonHierarchy::query(int32_t lat, int32_t lon) const {
    PIPResult r;
    for (uint32_t root : roots_) {
        const auto& n = nodes_[root];
        if (!bbox_contains(n, lat, lon)) continue;
        if (!pip_single(n, lat, lon))    continue;
        descend(root, lat, lon, r);
        return r;
    }
    return r;
}

// ───────────────────── Reverse-geocode to 4 tiers ───────────
AddressInfo PolygonHierarchy::query_address(int32_t lat, int32_t lon,
                                            const DES::OsmData& db) const {
    AddressInfo info;
    PIPResult r = query(lat, lon);

    uint8_t deepest_suburb = 0;     // largest admin_level seen for SUBURB tier
    for (uint32_t node_id : r.chain) {
        const PolygonNode& n = nodes_[node_id];
        const DES::AdminArea& a = db.admin_areas[n.admin_idx];
        const char* name = db.pool.get(a.name_off);
        if (!name || !*name) continue;

        switch (n.tier) {
            case Tier::COUNTRY: info.country = name; break;
            case Tier::STATE:   info.state   = name; break;
            case Tier::CITY:    info.city    = name; break;
            case Tier::SUBURB:
                // Multiple levels (9/10/11) may all match; prefer the deepest.
                if (n.admin_level > deepest_suburb) {
                    info.suburb     = name;
                    deepest_suburb  = n.admin_level;
                }
                break;
            default: break;
        }
    }
    return info;
}

// ───────────────────────── Depth ────────────────────────────
int PolygonHierarchy::compute_max_depth() const {
    if (nodes_.empty()) return 0;
    int max_d = 0;
    std::vector<int> depth(nodes_.size(), 0);
    std::vector<uint32_t> stack;
    for (uint32_t r : roots_) {
        depth[r] = 1;
        stack.push_back(r);
        while (!stack.empty()) {
            uint32_t x = stack.back(); stack.pop_back();
            if (depth[x] > max_d) max_d = depth[x];
            for (uint32_t c : nodes_[x].children) {
                depth[c] = depth[x] + 1;
                stack.push_back(c);
            }
        }
    }
    return max_d;
}

// ───────────────────────── Entry ────────────────────────────
void PolygonHierarchy::build(const std::vector<DES::AdminArea>& admins) {
    nodes_.clear(); roots_.clear(); level_grids_.clear(); intersections_.clear();
    stats_ = HierarchyStats{};

    build_slabs(admins);
    build_level_grids();
    build_hierarchy();
    detect_intersections(admins);

    stats_.node_count = nodes_.size();
    stats_.root_count = roots_.size();
    stats_.max_depth  = compute_max_depth();
}

} // namespace geo
