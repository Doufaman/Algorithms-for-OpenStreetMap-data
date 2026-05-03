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

class ApiHandler {
public:
    explicit ApiHandler(const std::string& data_dir);

    std::string points_geojson(const BBox& bbox, int limit) const;
    std::string lines_geojson (const BBox& bbox, int limit) const;
    std::string admin_geojson (const BBox& bbox)            const;

    size_t point_count() const { return points_.size(); }
    size_t line_count()  const { return lines_.size();  }
    size_t admin_count() const { return admins_.size(); }

private:
    std::vector<PointRecord> points_;
    std::vector<LineRecord>  lines_;
    std::vector<AdminRecord> admins_;

    void load_points(const std::string& path);
    void load_lines (const std::string& path);
    void load_admin (const std::string& path);
};
