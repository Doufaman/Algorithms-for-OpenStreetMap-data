#include "slab_polygon.h"
#include "data_extraction_and_storage/data_extraction_storage.h"
#include <algorithm>
#include <cstdlib>

namespace geo {

// Shoelace formula on integer coords → twice the |signed area|.
static int64_t shoelace_area(const std::vector<std::pair<int32_t,int32_t>>& r) {
    if (r.size() < 3) return 0;
    int64_t a = 0;
    for (size_t i = 0, n = r.size(); i < n; ++i) {
        size_t j = (i + 1) % n;
        int64_t x1 = r[i].second, y1 = r[i].first;
        int64_t x2 = r[j].second, y2 = r[j].first;
        a += x1 * y2 - x2 * y1;
    }
    return std::llabs(a);
}

void build_slab_node(const DES::AdminArea& a, uint32_t idx, PolygonNode& out) {
    out.admin_idx   = idx;
    out.admin_level = a.admin_level;
    out.tier        = admin_level_to_tier(a.admin_level);
    out.min_lat = a.min_lat_e7; out.max_lat = a.max_lat_e7;
    out.min_lon = a.min_lon_e7; out.max_lon = a.max_lon_e7;

    const auto& ring = a.outer_ring;
    if (ring.empty()) return;

    out.area_abs = shoelace_area(ring);

    // A vertex always lies on the boundary; under half-open ray casting
    // boundary points yield a deterministic answer, so this is safe to
    // use as the polygon's representative point in hierarchy construction.
    out.rep_lat = ring[0].first;
    out.rep_lon = ring[0].second;

    out.slab_lat_min  = out.min_lat;
    out.slab_lat_span = (int64_t)out.max_lat - (int64_t)out.min_lat + 1;
    if (out.slab_lat_span <= 0) out.slab_lat_span = 1;

    auto slab_idx = [&](int32_t lat) -> int {
        int64_t s = (int64_t)(lat - out.slab_lat_min) * NUM_SLABS / out.slab_lat_span;
        if (s < 0) return 0;
        if (s >= NUM_SLABS) return NUM_SLABS - 1;
        return (int)s;
    };

    // Pass 1: count edges per slab
    std::vector<uint32_t> cnt(NUM_SLABS, 0);
    for (size_t i = 0, n = ring.size(); i < n; ++i) {
        size_t j = (i + 1) % n;
        int32_t y0 = ring[i].first, y1 = ring[j].first;
        if (y0 == y1) continue;   // horizontal edge: contributes no crossings
        int s0 = slab_idx(std::min(y0, y1));
        int s1 = slab_idx(std::max(y0, y1));
        for (int s = s0; s <= s1; ++s) ++cnt[s];
    }

    out.slab_offsets.assign(NUM_SLABS + 1, 0);
    for (int s = 0; s < NUM_SLABS; ++s)
        out.slab_offsets[s + 1] = out.slab_offsets[s] + cnt[s];

    out.edges.assign(out.slab_offsets[NUM_SLABS], SlabEdge{});

    // Pass 2: bucket edges
    std::vector<uint32_t> cur(NUM_SLABS, 0);
    for (size_t i = 0, n = ring.size(); i < n; ++i) {
        size_t j = (i + 1) % n;
        int32_t y0 = ring[i].first, x0 = ring[i].second;
        int32_t y1 = ring[j].first, x1 = ring[j].second;
        if (y0 == y1) continue;
        int s0 = slab_idx(std::min(y0, y1));
        int s1 = slab_idx(std::max(y0, y1));
        SlabEdge e{y0, x0, y1, x1};
        for (int s = s0; s <= s1; ++s)
            out.edges[out.slab_offsets[s] + cur[s]++] = e;
    }
}

bool pip_single(const PolygonNode& n, int32_t lat, int32_t lon) {
    if (n.slab_offsets.empty()) return false;

    int64_t s = (int64_t)(lat - n.slab_lat_min) * NUM_SLABS / n.slab_lat_span;
    if (s < 0) s = 0;
    if (s >= NUM_SLABS) s = NUM_SLABS - 1;

    int crossings = 0;
    uint32_t lo = n.slab_offsets[(int)s];
    uint32_t hi = n.slab_offsets[(int)s + 1];
    for (uint32_t k = lo; k < hi; ++k) {
        const SlabEdge& e = n.edges[k];

        // Half-open straddle test: the horizontal ray at y=lat crosses the
        // edge iff exactly one endpoint is strictly above lat.
        bool c0 = (e.lat0 > lat);
        bool c1 = (e.lat1 > lat);
        if (c0 == c1) continue;

        // x_int = e.lon0 + (lat - e.lat0) * (e.lon1 - e.lon0) / (e.lat1 - e.lat0)
        // Want: x_int > lon.  Rewritten without division:
        //   sign((e.lon0 - lon)*dy + (lat - e.lat0)*dx) == sign(dy)
        int64_t dy  = (int64_t)e.lat1 - (int64_t)e.lat0;
        int64_t dx  = (int64_t)e.lon1 - (int64_t)e.lon0;
        int64_t lhs = (int64_t)(e.lon0 - lon) * dy + (int64_t)(lat - e.lat0) * dx;
        bool right  = (dy > 0) ? (lhs > 0) : (lhs < 0);
        if (right) ++crossings;
    }
    return (crossings & 1) != 0;
}

} // namespace geo
