// ============================================================
//  Uniform 2-D grid over (lat_e7, lon_e7).
//  Stores object IDs indexed by their bbox; used as the coarse
//  spatial filter that picks polygon candidates before PIP.
// ============================================================
#pragma once
#include <cstdint>
#include <vector>

namespace geo {

class GridIndex {
public:
    void build(int32_t min_lat, int32_t max_lat,
               int32_t min_lon, int32_t max_lon,
               int     n_lat_cells, int n_lon_cells);

    void insert_bbox(uint32_t obj_id,
                     int32_t mn_lat, int32_t mx_lat,
                     int32_t mn_lon, int32_t mx_lon);

    // Append candidates whose cell contains (lat, lon).
    void query_point(int32_t lat, int32_t lon,
                     std::vector<uint32_t>& out) const;

    // Append candidates whose cell intersects the bbox.
    void query_bbox(int32_t mn_lat, int32_t mx_lat,
                    int32_t mn_lon, int32_t mx_lon,
                    std::vector<uint32_t>& out) const;

    size_t cell_count() const { return cells_.size(); }
    bool   empty()      const { return cells_.empty(); }

private:
    int32_t bb_min_lat_ = 0, bb_max_lat_ = 0;
    int32_t bb_min_lon_ = 0, bb_max_lon_ = 0;
    int     n_lat_      = 0, n_lon_      = 0;
    int64_t lat_span_   = 1, lon_span_   = 1;
    std::vector<std::vector<uint32_t>> cells_;     // row-major n_lat_ × n_lon_

    int cell_lat(int32_t lat) const {
        int64_t s = (int64_t)(lat - bb_min_lat_) * n_lat_ / lat_span_;
        if (s < 0) return 0;
        if (s >= n_lat_) return n_lat_ - 1;
        return (int)s;
    }
    int cell_lon(int32_t lon) const {
        int64_t s = (int64_t)(lon - bb_min_lon_) * n_lon_ / lon_span_;
        if (s < 0) return 0;
        if (s >= n_lon_) return n_lon_ - 1;
        return (int)s;
    }
};

} // namespace geo
