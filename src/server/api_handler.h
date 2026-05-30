#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct BBox {
    double min_lon = -180, min_lat = -90;
    double max_lon =  180, max_lat =  90;

    bool contains(double lat, double lon) const {
        return lat >= min_lat && lat <= max_lat &&
               lon >= min_lon && lon <= max_lon;
    }
    bool intersects(double lat0, double lat1,
                    double lon0, double lon1) const {
        return !(lat1 < min_lat || lat0 > max_lat ||
                 lon1 < min_lon || lon0 > max_lon);
    }
};

struct PointRecord {
    int64_t     id;
    double      lat, lon;
    std::string name, type, street, housenumber, postcode;
    // Filled by Sheet 2 Task 2 (admin chain attached to each building)
    std::string country, state, city, suburb;
};

struct LineRecord {
    int64_t     id;
    std::string name, type, ref;
    double      min_lat, max_lat, min_lon, max_lon;
    std::vector<double> coords;
};

struct AdminRecord {
    int64_t     id;
    int         admin_level;
    std::string name;
    double      min_lat, max_lat, min_lon, max_lon;
    std::vector<double> ring;
};

// ----------------------------------------------------------------
//  Spatial index over points for nearest-neighbor queries.
//  Uniform grid; cell sized so that each holds ~50-200 points on
//  average (Stuttgart-Regbez ≈ 1500×1500 cells).
// ----------------------------------------------------------------
struct PointGridIndex {
    double  min_lat = 0, max_lat = 0, min_lon = 0, max_lon = 0;
    int     n_lat = 0, n_lon = 0;
    double  lat_step = 1, lon_step = 1;
    std::vector<std::vector<uint32_t>> cells;   // row-major

    void build(const std::vector<PointRecord>& pts);
    int  cell_lat(double lat) const;
    int  cell_lon(double lon) const;

    // Returns index into points array, or UINT32_MAX if none found.
    // Searches outwards in expanding rings until a point is found
    // OR ring radius exceeds best distance found so far.
    uint32_t nearest(const std::vector<PointRecord>& pts,
                     double lat, double lon,
                     double* out_dist_m = nullptr) const;

    // Collect the K nearest points (closest first). Used to backfill
    // a sparse admin chain by polling nearby houses.
    void k_nearest(const std::vector<PointRecord>& pts,
                   double lat, double lon, size_t k,
                   std::vector<std::pair<double, uint32_t>>& out) const;
};

// Similar grid over admin bboxes (for low-zoom queries returning a polygon).
struct AdminGridIndex {
    double  min_lat = 0, max_lat = 0, min_lon = 0, max_lon = 0;
    int     n_lat = 0, n_lon = 0;
    double  lat_step = 1, lon_step = 1;
    std::vector<std::vector<uint32_t>> cells;

    void build(const std::vector<AdminRecord>& admins);
    // Returns the smallest-area admin polygon at the given level
    // whose ring contains (lat, lon). If admin_level < 0, returns
    // the smallest containing admin regardless of level.
    uint32_t pip_smallest(const std::vector<AdminRecord>& admins,
                          double lat, double lon, int admin_level = -1) const;
};

class ApiHandler {
public:
    explicit ApiHandler(const std::string& data_dir);

    std::string points_geojson(const BBox& bbox, int limit) const;
    std::string lines_geojson (const BBox& bbox, int limit) const;
    std::string admin_geojson (const BBox& bbox)            const;

    // Sheet 2 Task 3 + 4: reverse geocoder. Zoom decides the type of
    // returned object (house at high zoom → country at low zoom).
    // Always includes the full admin chain in the response.
    std::string reverse_geojson(double lat, double lon, int zoom) const;

    size_t point_count() const { return points_.size(); }
    size_t line_count()  const { return lines_.size();  }
    size_t admin_count() const { return admins_.size(); }

private:
    std::vector<PointRecord> points_;
    std::vector<LineRecord>  lines_;
    std::vector<AdminRecord> admins_;

    PointGridIndex point_index_;
    AdminGridIndex admin_index_;

    void load_points(const std::string& path);
    void load_lines (const std::string& path);
    void load_admin (const std::string& path);
};
