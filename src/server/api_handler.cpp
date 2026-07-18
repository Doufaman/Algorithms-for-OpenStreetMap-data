#include "api_handler.h"
#include "httplib.h"
#include "io/binary_io.h"
#include "geocoder/inverted_index.h"
#include "geocoder/normalizer.h"
#include "geocoder/geocoder.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <queue>

// ============================================================
//  Minimal pull-style JSON parser
//  Only parses the flat structure produced by our storage code.
//  Avoids a full JSON library dependency.
// ============================================================

// Skip whitespace
static void skip_ws(const char*& p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
}

// Read a quoted JSON string; advances p past closing "
static std::string read_string(const char*& p) {
    if (*p != '"') return {};
    ++p;
    std::string out;
    while (*p && *p != '"') {
        if (*p == '\\') {
            ++p;
            switch (*p) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                default:   out += *p;  break;
            }
        } else {
            out += *p;
        }
        ++p;
    }
    if (*p == '"') ++p;
    return out;
}

// Read a JSON number as double; advances p
static double read_number(const char*& p) {
    char* end;
    double v = std::strtod(p, &end);
    p = end;
    return v;
}

// Expect a specific character, skip it
static bool expect(const char*& p, char c) {
    skip_ws(p);
    if (*p == c) { ++p; return true; }
    return false;
}

// ── Load points.json ─────────────────────────────────────────
// Format: [ {"id":N,"lat":F,"lon":F,"tags":{...}}, ... ]
void ApiHandler::load_points(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "Cannot open " << path << "\n"; return; }
    std::string src((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    const char* p = src.c_str();

    expect(p, '[');
    while (true) {
        skip_ws(p);
        if (*p == ']' || *p == '\0') break;
        if (*p == ',') { ++p; continue; }
        if (*p != '{') { ++p; continue; }
        ++p; // skip {

        PointRecord rec{};
        while (true) {
            skip_ws(p);
            if (*p == '}' || *p == '\0') { if (*p=='}') ++p; break; }
            if (*p == ',') { ++p; continue; }
            if (*p != '"') { ++p; continue; }

            std::string key = read_string(p);
            expect(p, ':');
            skip_ws(p);

            if      (key == "id")  rec.id  = (int64_t)read_number(p);
            else if (key == "lat") rec.lat = read_number(p);
            else if (key == "lon") rec.lon = read_number(p);
            else if (key == "tags") {
                // parse inner object
                expect(p, '{');
                while (true) {
                    skip_ws(p);
                    if (*p == '}' || *p == '\0') { if (*p=='}') ++p; break; }
                    if (*p == ',') { ++p; continue; }
                    if (*p != '"') { ++p; continue; }
                    std::string tk = read_string(p);
                    expect(p, ':');
                    skip_ws(p);
                    std::string tv = (*p == '"') ? read_string(p) : "";
                    if      (tk == "name")        rec.name        = tv;
                    else if (tk == "type")        rec.type        = tv;
                    else if (tk == "street")      rec.street      = tv;
                    else if (tk == "housenumber") rec.housenumber = tv;
                    else if (tk == "postcode")    rec.postcode    = tv;
                    else if (tk == "country")     rec.country     = tv;
                    else if (tk == "state")       rec.state       = tv;
                    else if (tk == "city")        rec.city        = tv;
                    else if (tk == "suburb")      rec.suburb      = tv;
                }
            } else {
                // skip unknown value
                if (*p == '"') read_string(p);
                else if (*p == '{' || *p == '[') {
                    // skip nested structure
                    int depth = 0;
                    while (*p) {
                        if (*p=='{' || *p=='[') ++depth;
                        else if (*p=='}' || *p==']') { --depth; if(depth==0){++p;break;} }
                        ++p;
                    }
                } else read_number(p);
            }
        }
        points_.push_back(std::move(rec));
    }
    std::cout << "  Loaded " << points_.size() << " points\n";
}

// ── Load lines.json ───────────────────────────────────────────
// Format: [ {"id":N,"name":"...","type":"...","bbox":[...],
//            "coords":[[lat,lon],...] }, ... ]
void ApiHandler::load_lines(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "Cannot open " << path << "\n"; return; }
    std::string src((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    const char* p = src.c_str();

    expect(p, '[');
    while (true) {
        skip_ws(p);
        if (*p == ']' || *p == '\0') break;
        if (*p == ',') { ++p; continue; }
        if (*p != '{') { ++p; continue; }
        ++p;

        LineRecord rec{};
        while (true) {
            skip_ws(p);
            if (*p == '}' || *p == '\0') { if (*p=='}') ++p; break; }
            if (*p == ',') { ++p; continue; }
            if (*p != '"') { ++p; continue; }

            std::string key = read_string(p);
            expect(p, ':');
            skip_ws(p);

            if      (key == "id")   rec.id   = (int64_t)read_number(p);
            else if (key == "name") rec.name = read_string(p);
            else if (key == "type") rec.type = read_string(p);
            else if (key == "ref")  rec.ref  = read_string(p);
            else if (key == "bbox") {
                expect(p, '[');
                rec.min_lat = read_number(p); expect(p,',');
                rec.max_lat = read_number(p); expect(p,',');
                rec.min_lon = read_number(p); expect(p,',');
                rec.max_lon = read_number(p);
                expect(p, ']');
            }
            else if (key == "coords") {
                expect(p, '[');
                while (true) {
                    skip_ws(p);
                    if (*p == ']' || *p == '\0') { if(*p==']') ++p; break; }
                    if (*p == ',') { ++p; continue; }
                    if (*p == '[') {
                        ++p;
                        double lat = read_number(p); expect(p,',');
                        double lon = read_number(p);
                        expect(p, ']');
                        rec.coords.push_back(lat);
                        rec.coords.push_back(lon);
                    } else ++p;
                }
            }
            else {
                if (*p == '"') read_string(p);
                else { int d=0; while(*p){if(*p=='{'||*p=='[')++d;else if(*p=='}'||*p==']'){--d;if(d<=0&&(*p=='}'||*p==']')){if(d==0)++p;break;}}++p;} }
            }
        }
        lines_.push_back(std::move(rec));
    }
    std::cout << "  Loaded " << lines_.size() << " lines\n";
}

// ── Load admin_areas.json ─────────────────────────────────────
void ApiHandler::load_admin(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "Cannot open " << path << "\n"; return; }
    std::string src((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    const char* p = src.c_str();

    expect(p, '[');
    while (true) {
        skip_ws(p);
        if (*p == ']' || *p == '\0') break;
        if (*p == ',') { ++p; continue; }
        if (*p != '{') { ++p; continue; }
        ++p;

        AdminRecord rec{};
        while (true) {
            skip_ws(p);
            if (*p == '}' || *p == '\0') { if (*p=='}') ++p; break; }
            if (*p == ',') { ++p; continue; }
            if (*p != '"') { ++p; continue; }

            std::string key = read_string(p);
            expect(p, ':');
            skip_ws(p);

            if      (key == "id")          rec.id          = (int64_t)read_number(p);
            else if (key == "admin_level") rec.admin_level = (int)read_number(p);
            else if (key == "name")        rec.name        = read_string(p);
            else if (key == "bbox") {
                expect(p,'[');
                rec.min_lat = read_number(p); expect(p,',');
                rec.max_lat = read_number(p); expect(p,',');
                rec.min_lon = read_number(p); expect(p,',');
                rec.max_lon = read_number(p);
                expect(p,']');
            }
            else if (key == "outer_ring") {
                expect(p,'[');
                while (true) {
                    skip_ws(p);
                    if (*p==']'||*p=='\0'){if(*p==']')++p;break;}
                    if (*p==','){++p;continue;}
                    if (*p=='[') {
                        ++p;
                        double lat = read_number(p); expect(p,',');
                        double lon = read_number(p);
                        expect(p,']');
                        rec.ring.push_back(lat);
                        rec.ring.push_back(lon);
                    } else ++p;
                }
            }
            else {
                if (*p=='"') read_string(p);
                else read_number(p);
            }
        }
        admins_.push_back(std::move(rec));
    }
    std::cout << "  Loaded " << admins_.size() << " admin areas\n";
}

// ── Constructors ──────────────────────────────────────────────
// Single-dir ctor delegates to the multi-dir one.
ApiHandler::ApiHandler(const std::string& data_dir)
    : ApiHandler(std::vector<std::string>{ data_dir }) {}

// Multi-dataset ctor: loads every directory in order, appending records
// into the shared pools. bin::read_* clears its output vector, so each
// artifact is read into a temp and then appended.
ApiHandler::ApiHandler(const std::vector<std::string>& data_dirs) {
    for (const auto& dir : data_dirs) {
        std::string sep = dir;
        if (!sep.empty() && sep.back() != '/' && sep.back() != '\\') sep += '/';
        std::cout << "  Loading dataset dir: " << sep << "\n";

        auto t0 = std::chrono::steady_clock::now();

        // ---- points ----
        {
            std::vector<PointRecord> tmp;
            if (bin::read_points(sep + "points.bin", tmp)) {
                points_.insert(points_.end(),
                               std::make_move_iterator(tmp.begin()),
                               std::make_move_iterator(tmp.end()));
            } else {
                std::cout << "    points.bin missing, falling back to JSON\n";
                load_points(sep + "points.json");   // appends directly
            }
        }
        // ---- lines ----
        {
            std::vector<LineRecord> tmp;
            if (bin::read_lines(sep + "lines.bin", tmp)) {
                lines_.insert(lines_.end(),
                              std::make_move_iterator(tmp.begin()),
                              std::make_move_iterator(tmp.end()));
            } else {
                std::cout << "    lines.bin missing, falling back to JSON\n";
                load_lines(sep + "lines.json");
            }
        }
        // ---- admins ----
        {
            std::vector<AdminRecord> tmp;
            if (bin::read_admin(sep + "admin_areas.bin", tmp)) {
                admins_.insert(admins_.end(),
                               std::make_move_iterator(tmp.begin()),
                               std::make_move_iterator(tmp.end()));
            } else {
                std::cout << "    admin_areas.bin missing, falling back to JSON\n";
                load_admin(sep + "admin_areas.json");
            }
        }

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();
        std::cout << "    dir done in " << ms << " ms  (cumulative: "
                  << points_.size() << " points, "
                  << lines_.size()  << " lines, "
                  << admins_.size() << " admins)\n";
    }

    std::cout << "  Building spatial indices ...\n";
    point_index_.build(points_);
    admin_index_.build(admins_);
    std::cout << "    point grid : " << point_index_.cells.size() << " cells\n"
              << "    admin grid : " << admin_index_.cells.size() << " cells\n";

    // ── Sheet 3 Task 2: inverted-index geocoder ────────────
    std::cout << "  Building inverted index (Sheet 3 Task 2) ...\n";
    auto ti0 = std::chrono::steady_clock::now();
    geoc_index_ = std::make_unique<geocoder::InvertedIndex>();
    geoc_index_->build(points_, lines_, admins_);
    auto ti_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - ti0).count();
    std::cout << "    unique tokens : " << geoc_index_->token_count()
              << "   entries : "        << geoc_index_->total_entries()
              << "   BK-tree : "        << geoc_index_->bktree().size()
              << "   (" << ti_ms << " ms)\n";

    // ── Sheet 3 Task 3: query orchestrator (ranker + splitter) ──
    geocoder_ = std::make_unique<geocoder::Geocoder>(
                    *geoc_index_, points_, lines_, admins_, point_index_);
    std::cout << "  Geocoder orchestrator ready (Sheet 3 Task 3)\n";
}

// Out-of-line destructor so unique_ptr<InvertedIndex> can call the
// full destructor (definition seen only via inverted_index.h include).
ApiHandler::~ApiHandler() = default;

// ============================================================
//  Spatial-index implementations
// ============================================================

#include <cmath>

static double haversine_m(double lat1, double lon1, double lat2, double lon2) {
    constexpr double R = 6371000.0;
    constexpr double D2R = 3.14159265358979323846 / 180.0;
    double dlat = (lat2 - lat1) * D2R;
    double dlon = (lon2 - lon1) * D2R;
    double a = std::sin(dlat/2)*std::sin(dlat/2)
             + std::cos(lat1*D2R)*std::cos(lat2*D2R)
             * std::sin(dlon/2)*std::sin(dlon/2);
    return 2.0 * R * std::asin(std::sqrt(a));
}

// ---- PointGridIndex ----------------------------------------
void PointGridIndex::build(const std::vector<PointRecord>& pts) {
    if (pts.empty()) return;
    min_lat = pts[0].lat; max_lat = pts[0].lat;
    min_lon = pts[0].lon; max_lon = pts[0].lon;
    for (const auto& p : pts) {
        if (p.lat < min_lat) min_lat = p.lat;
        if (p.lat > max_lat) max_lat = p.lat;
        if (p.lon < min_lon) min_lon = p.lon;
        if (p.lon > max_lon) max_lon = p.lon;
    }
    // Target ~100 points/cell
    int target = std::max(1, (int)std::sqrt((double)pts.size() / 100.0));
    n_lat = std::clamp(target, 8, 4096);
    n_lon = std::clamp(target, 8, 4096);
    lat_step = (max_lat - min_lat + 1e-9) / n_lat;
    lon_step = (max_lon - min_lon + 1e-9) / n_lon;
    cells.assign((size_t)n_lat * n_lon, {});
    for (uint32_t i = 0; i < pts.size(); ++i) {
        int la = cell_lat(pts[i].lat);
        int lo = cell_lon(pts[i].lon);
        cells[(size_t)la * n_lon + lo].push_back(i);
    }
}

int PointGridIndex::cell_lat(double lat) const {
    int s = (int)((lat - min_lat) / lat_step);
    if (s < 0) return 0;
    if (s >= n_lat) return n_lat - 1;
    return s;
}
int PointGridIndex::cell_lon(double lon) const {
    int s = (int)((lon - min_lon) / lon_step);
    if (s < 0) return 0;
    if (s >= n_lon) return n_lon - 1;
    return s;
}

uint32_t PointGridIndex::nearest(const std::vector<PointRecord>& pts,
                                 double lat, double lon,
                                 double* out_dist_m) const {
    if (cells.empty()) return UINT32_MAX;
    int cx = cell_lat(lat), cy = cell_lon(lon);
    uint32_t best = UINT32_MAX;
    double   best_d = 1e30;

    // Expanding ring search. At ring r the closest possible candidate is
    // at distance r * min(cell_lat_m, cell_lon_m). Stop when this exceeds best.
    double cell_lat_m = haversine_m(lat, lon, lat + lat_step, lon);
    double cell_lon_m = haversine_m(lat, lon, lat, lon + lon_step);
    double cell_min_m = std::min(cell_lat_m, cell_lon_m);

    for (int r = 0; ; ++r) {
        int la0 = std::max(0, cx - r), la1 = std::min(n_lat - 1, cx + r);
        int lo0 = std::max(0, cy - r), lo1 = std::min(n_lon - 1, cy + r);

        // Only the ring cells (skip interior of previous rings).
        for (int i = la0; i <= la1; ++i) {
            for (int j = lo0; j <= lo1; ++j) {
                if (r > 0 && i != la0 && i != la1 && j != lo0 && j != lo1) continue;
                for (uint32_t idx : cells[(size_t)i * n_lon + j]) {
                    double d = haversine_m(lat, lon, pts[idx].lat, pts[idx].lon);
                    if (d < best_d) { best_d = d; best = idx; }
                }
            }
        }
        // Termination: when next ring can't beat current best.
        if (best != UINT32_MAX && (double)r * cell_min_m > best_d) break;
        if (la0 == 0 && la1 == n_lat - 1 && lo0 == 0 && lo1 == n_lon - 1) break;
    }
    if (out_dist_m) *out_dist_m = (best == UINT32_MAX) ? -1.0 : best_d;
    return best;
}

// ---- PointGridIndex::k_nearest -----------------------------
// Same ring-expansion search but keeps a max-heap of size k.
void PointGridIndex::k_nearest(const std::vector<PointRecord>& pts,
                               double lat, double lon, size_t k,
                               std::vector<std::pair<double, uint32_t>>& out) const {
    out.clear();
    if (cells.empty() || k == 0) return;
    int cx = cell_lat(lat), cy = cell_lon(lon);
    using Entry = std::pair<double, uint32_t>;          // (distance, idx)
    std::priority_queue<Entry> heap;                    // max-heap by distance

    double cell_lat_m = haversine_m(lat, lon, lat + lat_step, lon);
    double cell_lon_m = haversine_m(lat, lon, lat, lon + lon_step);
    double cell_min_m = std::min(cell_lat_m, cell_lon_m);

    for (int r = 0; ; ++r) {
        int la0 = std::max(0, cx - r), la1 = std::min(n_lat - 1, cx + r);
        int lo0 = std::max(0, cy - r), lo1 = std::min(n_lon - 1, cy + r);

        for (int i = la0; i <= la1; ++i) {
            for (int j = lo0; j <= lo1; ++j) {
                if (r > 0 && i != la0 && i != la1 && j != lo0 && j != lo1) continue;
                for (uint32_t idx : cells[(size_t)i * n_lon + j]) {
                    double d = haversine_m(lat, lon, pts[idx].lat, pts[idx].lon);
                    if (heap.size() < k)        heap.push({d, idx});
                    else if (d < heap.top().first) { heap.pop(); heap.push({d, idx}); }
                }
            }
        }
        if (heap.size() == k && (double)r * cell_min_m > heap.top().first) break;
        if (la0 == 0 && la1 == n_lat - 1 && lo0 == 0 && lo1 == n_lon - 1) break;
    }

    out.reserve(heap.size());
    while (!heap.empty()) { out.push_back(heap.top()); heap.pop(); }
    std::sort(out.begin(), out.end(),
              [](const Entry& a, const Entry& b){ return a.first < b.first; });
}

// ---- AdminGridIndex ----------------------------------------
void AdminGridIndex::build(const std::vector<AdminRecord>& ads) {
    if (ads.empty()) return;
    min_lat = ads[0].min_lat; max_lat = ads[0].max_lat;
    min_lon = ads[0].min_lon; max_lon = ads[0].max_lon;
    for (const auto& a : ads) {
        if (a.min_lat < min_lat) min_lat = a.min_lat;
        if (a.max_lat > max_lat) max_lat = a.max_lat;
        if (a.min_lon < min_lon) min_lon = a.min_lon;
        if (a.max_lon > max_lon) max_lon = a.max_lon;
    }
    int target = std::clamp((int)std::sqrt((double)ads.size()) * 2, 8, 256);
    n_lat = target; n_lon = target;
    lat_step = (max_lat - min_lat + 1e-9) / n_lat;
    lon_step = (max_lon - min_lon + 1e-9) / n_lon;
    cells.assign((size_t)n_lat * n_lon, {});
    for (uint32_t i = 0; i < ads.size(); ++i) {
        int la0 = std::clamp((int)((ads[i].min_lat - min_lat) / lat_step), 0, n_lat - 1);
        int la1 = std::clamp((int)((ads[i].max_lat - min_lat) / lat_step), 0, n_lat - 1);
        int lo0 = std::clamp((int)((ads[i].min_lon - min_lon) / lon_step), 0, n_lon - 1);
        int lo1 = std::clamp((int)((ads[i].max_lon - min_lon) / lon_step), 0, n_lon - 1);
        for (int la = la0; la <= la1; ++la)
            for (int lo = lo0; lo <= lo1; ++lo)
                cells[(size_t)la * n_lon + lo].push_back(i);
    }
}

// Standard half-open ray casting on a ring stored as flat (lat, lon) pairs.
static bool pip_ring(const std::vector<double>& ring, double lat, double lon) {
    int crossings = 0;
    size_t n = ring.size() / 2;
    if (n < 3) return false;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        double yi = ring[2*i], xi = ring[2*i + 1];
        double yj = ring[2*j], xj = ring[2*j + 1];
        bool ci = (yi > lat);
        bool cj = (yj > lat);
        if (ci == cj) continue;
        double x_int = xi + (lat - yi) * (xj - xi) / (yj - yi);
        if (x_int > lon) ++crossings;
    }
    return (crossings & 1) != 0;
}

uint32_t AdminGridIndex::pip_smallest(const std::vector<AdminRecord>& ads,
                                      double lat, double lon, int admin_level) const {
    if (cells.empty()) return UINT32_MAX;
    int la = std::clamp((int)((lat - min_lat) / lat_step), 0, n_lat - 1);
    int lo = std::clamp((int)((lon - min_lon) / lon_step), 0, n_lon - 1);
    uint32_t best = UINT32_MAX;
    double   best_area = 1e30;
    for (uint32_t idx : cells[(size_t)la * n_lon + lo]) {
        const auto& a = ads[idx];
        if (admin_level >= 0 && a.admin_level != admin_level) continue;
        if (lat < a.min_lat || lat > a.max_lat) continue;
        if (lon < a.min_lon || lon > a.max_lon) continue;
        if (!pip_ring(a.ring, lat, lon)) continue;
        double area = (a.max_lat - a.min_lat) * (a.max_lon - a.min_lon); // rough proxy
        if (area < best_area) { best_area = area; best = idx; }
    }
    return best;
}

// ============================================================
//  Reverse geocoder
// ============================================================
//
// Zoom-based dispatch:
//   zoom ≥ 15 : nearest building (point)
//   zoom 10-14: smallest admin containing point (typically city)
//   zoom 7-9  : admin at level 4 (state)
//   zoom < 7  : admin at level 2 (country)
//
// The returned GeoJSON always carries the FULL admin chain in properties
// (country / state / city / suburb), no matter what object level was chosen.
//
// Forward declaration — esc() itself is defined further down with the
// other GeoJSON helpers.
static std::string esc(const std::string& s);

static std::string field(const char* k, const std::string& v, bool& first) {
    if (v.empty()) return {};
    std::string s;
    if (!first) s += ',';
    s += '"'; s += k; s += "\":\"";
    s += esc(v); s += '"';
    first = false;
    return s;
}

std::string ApiHandler::reverse_geojson(double lat, double lon, int zoom) const {
    std::string out;
    out.reserve(2048);
    out += "{\"type\":\"FeatureCollection\",\"features\":[";

    // Always look up nearest building so we can fill its admin chain
    // even when the visible object is an admin polygon.
    double dist_m = -1.0;
    uint32_t nearest_idx = point_index_.nearest(points_, lat, lon, &dist_m);

    // Pull admin chain from nearest building (preferred) or from PIP if no
    // building was attached with chain info.
    std::string country, state, city, suburb;
    if (nearest_idx != UINT32_MAX) {
        const auto& p = points_[nearest_idx];
        country = p.country; state = p.state; city = p.city; suburb = p.suburb;
    }

    // Tier 1 fallback: PIP each admin level directly. Works when the
    // admin polygon's outer ring is well-formed.
    auto fill_from_admin = [&](int lvl, std::string& out_name) {
        if (!out_name.empty()) return;
        uint32_t idx = admin_index_.pip_smallest(admins_, lat, lon, lvl);
        if (idx != UINT32_MAX) out_name = admins_[idx].name;
    };
    fill_from_admin(2,  country);
    fill_from_admin(4,  state);
    fill_from_admin(8,  city);
    fill_from_admin(10, suburb);
    if (suburb.empty()) fill_from_admin(9,  suburb);
    if (suburb.empty()) fill_from_admin(11, suburb);

    // Tier 2 fallback: vote among K nearest houses. Robust to broken
    // admin polygons — nearby houses almost always share city/state/country.
    if (country.empty() || state.empty() || city.empty() || suburb.empty()) {
        std::vector<std::pair<double, uint32_t>> knn;
        point_index_.k_nearest(points_, lat, lon, 20, knn);
        for (const auto& [d, idx] : knn) {
            const auto& p = points_[idx];
            if (country.empty() && !p.country.empty()) country = p.country;
            if (state.empty()   && !p.state.empty())   state   = p.state;
            if (city.empty()    && !p.city.empty())    city    = p.city;
            if (suburb.empty()  && !p.suburb.empty())  suburb  = p.suburb;
            if (!country.empty() && !state.empty() &&
                !city.empty()    && !suburb.empty())     break;
        }
    }

    // Decide what object to return based on zoom
    enum class Obj { House, City, State, Country };
    Obj obj_kind;
    if      (zoom >= 15) obj_kind = Obj::House;
    else if (zoom >= 10) obj_kind = Obj::City;
    else if (zoom >= 7 ) obj_kind = Obj::State;
    else                 obj_kind = Obj::Country;

    bool first_feat = true;
    auto emit_admin_props = [&](std::string& s) {
        bool f = true;
        s += "\"country\":\""+esc(country)+"\""; (void)f; f = false;
        s += ",\"state\":\""+esc(state)+"\"";
        s += ",\"city\":\""+esc(city)+"\"";
        s += ",\"suburb\":\""+esc(suburb)+"\"";
    };

    // House
    if (obj_kind == Obj::House && nearest_idx != UINT32_MAX) {
        const auto& p = points_[nearest_idx];
        if (!first_feat) out += ',';
        first_feat = false;
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\","
                 "\"coordinates\":[%.7f,%.7f]},\"properties\":{",
                 p.lon, p.lat);
        out += buf;
        out += "\"object_kind\":\"house\"";
        out += ",\"id\":" + std::to_string(p.id);
        out += ",\"admin_level\":null";
        out += ",\"distance_m\":";
        char db[32]; snprintf(db, sizeof(db), "%.2f", dist_m); out += db;
        bool ff = false;
        out += field("name",        p.name,        ff);
        out += field("type",        p.type,        ff);
        out += field("street",      p.street,      ff);
        out += field("housenumber", p.housenumber, ff);
        out += field("postcode",    p.postcode,    ff);
        out += ",";
        emit_admin_props(out);
        out += "}}";
    }
    else {
        // Admin polygon (city/state/country)
        int want_level = (obj_kind == Obj::City) ? 8 :
                         (obj_kind == Obj::State) ? 4 : 2;
        uint32_t aidx = admin_index_.pip_smallest(admins_, lat, lon, want_level);
        // Fall-back: any level if exact one missing
        if (aidx == UINT32_MAX)
            aidx = admin_index_.pip_smallest(admins_, lat, lon, -1);

        if (aidx != UINT32_MAX) {
            const auto& a = admins_[aidx];
            if (!first_feat) out += ',';
            first_feat = false;
            out += "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Polygon\","
                   "\"coordinates\":[[";
            bool rfirst = true;
            for (size_t i = 0; i + 1 < a.ring.size(); i += 2) {
                if (!rfirst) out += ',';
                rfirst = false;
                char rb[64];
                snprintf(rb, sizeof(rb), "[%.7f,%.7f]", a.ring[i+1], a.ring[i]);
                out += rb;
            }
            out += "]]},\"properties\":{";
            const char* okind = (obj_kind == Obj::City) ? "city" :
                                (obj_kind == Obj::State) ? "state" : "country";
            out += "\"object_kind\":\""; out += okind; out += "\"";
            out += ",\"id\":" + std::to_string(a.id);
            out += ",\"admin_level\":" + std::to_string(a.admin_level);
            out += ",\"name\":\"" + esc(a.name) + "\"";
            out += ",";
            emit_admin_props(out);
            out += "}}";
        }
    }

    out += "]}";
    return out;
}

// ============================================================
//  GeoJSON builders
// ============================================================

static std::string esc(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else                out += c;
    }
    return out;
}

static char coord_buf[64];
static const char* fc(double v) {
    snprintf(coord_buf, sizeof(coord_buf), "%.7f", v);
    return coord_buf;
}

std::string ApiHandler::points_geojson(const BBox& bbox, int limit) const {
    std::string out;
    out.reserve(1 << 20);
    out += "{\"type\":\"FeatureCollection\",\"features\":[";
    bool first = true;
    int  count = 0;
    for (const auto& pt : points_) {
        if (!bbox.contains(pt.lat, pt.lon)) continue;
        if (count >= limit) break;
        if (!first) out += ',';
        first = false;
        out += "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\","
               "\"coordinates\":[";
        char buf[64];
        snprintf(buf,64,"%.7f,%.7f",pt.lon,pt.lat);
        out += buf;
        out += "]},\"properties\":{\"id\":";
        out += std::to_string(pt.id);
        if (!pt.name.empty())        { out += ",\"name\":\""        + esc(pt.name)        + "\""; }
        if (!pt.type.empty())        { out += ",\"type\":\""        + esc(pt.type)        + "\""; }
        if (!pt.street.empty())      { out += ",\"street\":\""      + esc(pt.street)      + "\""; }
        if (!pt.housenumber.empty()) { out += ",\"housenumber\":\"" + esc(pt.housenumber) + "\""; }
        if (!pt.postcode.empty())    { out += ",\"postcode\":\""    + esc(pt.postcode)    + "\""; }
        out += "}}";
        ++count;
    }
    out += "]}";
    return out;
}

std::string ApiHandler::lines_geojson(const BBox& bbox, int limit) const {
    std::string out;
    out.reserve(1 << 20);
    out += "{\"type\":\"FeatureCollection\",\"features\":[";
    bool first = true;
    int  count = 0;
    for (const auto& ln : lines_) {
        if (!bbox.intersects(ln.min_lat, ln.max_lat, ln.min_lon, ln.max_lon)) continue;
        if (count >= limit) break;
        if (!first) out += ',';
        first = false;
        out += "{\"type\":\"Feature\",\"geometry\":{\"type\":\"LineString\","
               "\"coordinates\":[";
        bool fc_first = true;
        for (size_t i = 0; i + 1 < ln.coords.size(); i += 2) {
            if (!fc_first) out += ',';
            fc_first = false;
            char buf[64];
            snprintf(buf,64,"[%.7f,%.7f]", ln.coords[i+1], ln.coords[i]);
            out += buf;
        }
        out += "]},\"properties\":{\"id\":";
        out += std::to_string(ln.id);
        if (!ln.name.empty()) { out += ",\"name\":\"" + esc(ln.name) + "\""; }
        if (!ln.type.empty()) { out += ",\"type\":\"" + esc(ln.type) + "\""; }
        if (!ln.ref.empty())  { out += ",\"ref\":\""  + esc(ln.ref)  + "\""; }
        out += "}}";
        ++count;
    }
    out += "]}";
    return out;
}

std::string ApiHandler::admin_geojson(const BBox& bbox) const {
    std::string out;
    out.reserve(1 << 18);
    out += "{\"type\":\"FeatureCollection\",\"features\":[";
    bool first = true;
    for (const auto& ad : admins_) {
        if (!bbox.intersects(ad.min_lat, ad.max_lat, ad.min_lon, ad.max_lon)) continue;
        if (!first) out += ',';
        first = false;
        out += "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Polygon\","
               "\"coordinates\":[[";
        bool rc_first = true;
        for (size_t i = 0; i + 1 < ad.ring.size(); i += 2) {
            if (!rc_first) out += ',';
            rc_first = false;
            char buf[64];
            snprintf(buf,64,"[%.7f,%.7f]", ad.ring[i+1], ad.ring[i]);
            out += buf;
        }
        out += "]]},\"properties\":{\"id\":";
        out += std::to_string(ad.id);
        out += ",\"admin_level\":";
        out += std::to_string(ad.admin_level);
        if (!ad.name.empty()) { out += ",\"name\":\"" + esc(ad.name) + "\""; }
        out += "}}";
    }
    out += "]}";
    return out;
}

// ============================================================
//  Sheet 3 Task 2 — Forward geocoder via inverted index.
//
//  Query flow (Task 2 minimum — no smart splitting yet):
//    1. tokenize the query using the DIN normalizer (Task 1)
//    2. look up the intersection of all tokens in the inverted index
//    3. build a GeoJSON feature for each hit, up to `limit`
//
//  Object → geometry mapping:
//    POI    → Point
//    Street → LineString (using its coord list)
//    Admin  → Polygon    (using its ring)
// ============================================================
std::string ApiHandler::search_geojson(const std::string& q, int limit) const {
    // Timing for /api/stats — Sheet 3 Task 4
    auto t_start = std::chrono::steady_clock::now();

    std::string out;
    out.reserve(4096);
    out += "{\"type\":\"FeatureCollection\",\"query\":\"" + esc(q) + "\",\"features\":[";

    if (!geocoder_) { out += "]}"; return out; }
    if (limit <= 0) limit = 20;

    // Sheet 3 Task 3: use the orchestrator (ranker + split enumeration
    // + housenumber refinement).  Returns Scored records already sorted
    // by descending score.
    auto scored = geocoder_->search(q, limit);

    bool first = true;
    for (const auto& sc : scored) {
        const auto& r = sc.ref;
        if (!first) out += ',';
        first = false;

        switch (r.kind) {
        case geocoder::ObjectKind::POI: {
            if (r.id >= points_.size()) continue;
            const auto& p = points_[r.id];
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\","
                     "\"coordinates\":[%.7f,%.7f]},\"properties\":{",
                     p.lon, p.lat);
            out += buf;
            out += "\"kind\":\"poi\",\"id\":" + std::to_string(p.id);
            if (!p.name.empty())    out += ",\"name\":\""    + esc(p.name)    + "\"";
            if (!p.type.empty())    out += ",\"type\":\""    + esc(p.type)    + "\"";
            if (!p.street.empty())      out += ",\"street\":\""      + esc(p.street)      + "\"";
            if (!p.housenumber.empty()) out += ",\"housenumber\":\"" + esc(p.housenumber) + "\"";
            if (!p.city.empty())    out += ",\"city\":\""    + esc(p.city)    + "\"";
            if (!p.state.empty())   out += ",\"state\":\""   + esc(p.state)   + "\"";
            if (!p.country.empty()) out += ",\"country\":\"" + esc(p.country) + "\"";
            char sb[96];
            snprintf(sb, sizeof(sb), ",\"score\":%.2f,\"matched\":%d",
                     sc.score, sc.matched_tokens);
            out += sb;
            if (!sc.reason.empty()) out += ",\"reason\":\"" + esc(sc.reason) + "\"";
            out += "}}";
            break;
        }
        case geocoder::ObjectKind::Street: {
            if (r.id >= lines_.size()) continue;
            const auto& l = lines_[r.id];
            out += "{\"type\":\"Feature\",\"geometry\":{\"type\":\"LineString\","
                   "\"coordinates\":[";
            bool cf = true;
            for (size_t i = 0; i + 1 < l.coords.size(); i += 2) {
                if (!cf) out += ',';
                cf = false;
                char cb[64];
                snprintf(cb, sizeof(cb), "[%.7f,%.7f]", l.coords[i+1], l.coords[i]);
                out += cb;
            }
            out += "]},\"properties\":{\"kind\":\"street\",\"id\":" + std::to_string(l.id);
            if (!l.name.empty()) out += ",\"name\":\"" + esc(l.name) + "\"";
            if (!l.type.empty()) out += ",\"type\":\"" + esc(l.type) + "\"";
            char sb[96];
            snprintf(sb, sizeof(sb), ",\"score\":%.2f,\"matched\":%d",
                     sc.score, sc.matched_tokens);
            out += sb;
            if (!sc.reason.empty()) out += ",\"reason\":\"" + esc(sc.reason) + "\"";
            out += "}}";
            break;
        }
        case geocoder::ObjectKind::Admin: {
            if (r.id >= admins_.size()) continue;
            const auto& a = admins_[r.id];
            out += "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Polygon\","
                   "\"coordinates\":[[";
            bool rf = true;
            for (size_t i = 0; i + 1 < a.ring.size(); i += 2) {
                if (!rf) out += ',';
                rf = false;
                char cb[64];
                snprintf(cb, sizeof(cb), "[%.7f,%.7f]", a.ring[i+1], a.ring[i]);
                out += cb;
            }
            out += "]]},\"properties\":{\"kind\":\"admin\",\"id\":" + std::to_string(a.id);
            out += ",\"admin_level\":" + std::to_string(a.admin_level);
            if (!a.name.empty()) out += ",\"name\":\"" + esc(a.name) + "\"";
            char sb[96];
            snprintf(sb, sizeof(sb), ",\"score\":%.2f,\"matched\":%d",
                     sc.score, sc.matched_tokens);
            out += sb;
            if (!sc.reason.empty()) out += ",\"reason\":\"" + esc(sc.reason) + "\"";
            out += "}}";
            break;
        }
        }
    }

    // Sheet 3 Task 4: expose query time in ms for the frontend / grader.
    auto t_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - t_start).count() / 1000.0;
    char tail[128];
    snprintf(tail, sizeof(tail),
             "],\"returned\":%zu,\"query_ms\":%.2f}",
             scored.size(), t_ms);
    out += tail;
    return out;
}

