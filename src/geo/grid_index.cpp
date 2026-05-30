#include "grid_index.h"
#include <algorithm>

namespace geo {

void GridIndex::build(int32_t mn_lat, int32_t mx_lat,
                      int32_t mn_lon, int32_t mx_lon,
                      int     n_lat,  int n_lon) {
    bb_min_lat_ = mn_lat; bb_max_lat_ = mx_lat;
    bb_min_lon_ = mn_lon; bb_max_lon_ = mx_lon;
    lat_span_   = (int64_t)mx_lat - (int64_t)mn_lat + 1;
    lon_span_   = (int64_t)mx_lon - (int64_t)mn_lon + 1;
    if (lat_span_ <= 0) lat_span_ = 1;
    if (lon_span_ <= 0) lon_span_ = 1;
    n_lat_ = std::max(1, n_lat);
    n_lon_ = std::max(1, n_lon);
    cells_.assign((size_t)n_lat_ * n_lon_, {});
}

void GridIndex::insert_bbox(uint32_t id,
                            int32_t mn_lat, int32_t mx_lat,
                            int32_t mn_lon, int32_t mx_lon) {
    if (cells_.empty()) return;
    int la0 = cell_lat(std::max(mn_lat, bb_min_lat_));
    int la1 = cell_lat(std::min(mx_lat, bb_max_lat_));
    int lo0 = cell_lon(std::max(mn_lon, bb_min_lon_));
    int lo1 = cell_lon(std::min(mx_lon, bb_max_lon_));
    for (int i = la0; i <= la1; ++i)
        for (int j = lo0; j <= lo1; ++j)
            cells_[(size_t)i * n_lon_ + j].push_back(id);
}

void GridIndex::query_point(int32_t lat, int32_t lon,
                            std::vector<uint32_t>& out) const {
    if (cells_.empty()) return;
    if (lat < bb_min_lat_ || lat > bb_max_lat_ ||
        lon < bb_min_lon_ || lon > bb_max_lon_) return;
    int i = cell_lat(lat), j = cell_lon(lon);
    const auto& c = cells_[(size_t)i * n_lon_ + j];
    out.insert(out.end(), c.begin(), c.end());
}

void GridIndex::query_bbox(int32_t mn_lat, int32_t mx_lat,
                           int32_t mn_lon, int32_t mx_lon,
                           std::vector<uint32_t>& out) const {
    if (cells_.empty()) return;
    if (mx_lat < bb_min_lat_ || mn_lat > bb_max_lat_ ||
        mx_lon < bb_min_lon_ || mn_lon > bb_max_lon_) return;
    int la0 = cell_lat(std::max(mn_lat, bb_min_lat_));
    int la1 = cell_lat(std::min(mx_lat, bb_max_lat_));
    int lo0 = cell_lon(std::max(mn_lon, bb_min_lon_));
    int lo1 = cell_lon(std::min(mx_lon, bb_max_lon_));
    for (int i = la0; i <= la1; ++i)
        for (int j = lo0; j <= lo1; ++j) {
            const auto& c = cells_[(size_t)i * n_lon_ + j];
            out.insert(out.end(), c.begin(), c.end());
        }
}

} // namespace geo
