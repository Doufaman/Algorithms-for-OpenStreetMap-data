#include "binary_io.h"
#include "data_extraction_and_storage/data_extraction_storage.h"
#include "server/api_handler.h"

#include <fstream>
#include <iostream>
#include <cstring>

namespace bin {

// ============================================================
//  Small helpers
// ============================================================

static void write_bytes(std::ostream& os, const void* p, size_t n) {
    os.write(reinterpret_cast<const char*>(p), (std::streamsize)n);
}

static bool read_bytes(std::istream& is, void* p, size_t n) {
    is.read(reinterpret_cast<char*>(p), (std::streamsize)n);
    return is.good() || is.eof();
}

// Pull a null-terminated string out of a strings blob at `off`.
// Returns empty if off is NONE / out of range.
static std::string str_at(const std::vector<char>& blob, uint32_t off) {
    if (off == DES::StringPool::NONE || off >= blob.size()) return {};
    return std::string(blob.data() + off);
}

// ============================================================
//  POINTS
// ============================================================

void write_points(const DES::OsmData& db, const std::string& path) {
    std::ofstream os(path, std::ios::binary);
    if (!os) { std::cerr << "Cannot open " << path << " for writing\n"; return; }

    Header h{};
    h.magic        = MAGIC_POINTS;
    h.version      = FORMAT_VERSION;
    h.n_records    = db.points.size();
    h.payload_size = 0;
    h.strings_size = db.pool.data.size();
    write_bytes(os, &h, sizeof(h));

    PointRec rec{};
    for (const auto& p : db.points) {
        rec.id              = p.osm_id;
        rec.lat_e7          = p.lat_e7;
        rec.lon_e7          = p.lon_e7;
        rec.name_off        = p.name_off;
        rec.type_off        = p.type_off;
        rec.street_off      = p.street_off;
        rec.housenumber_off = p.housenumber_off;
        rec.postcode_off    = p.postcode_off;
        rec.country_off     = p.country_off;
        rec.state_off       = p.state_off;
        rec.city_off        = p.city_off;
        rec.suburb_off      = p.suburb_off;
        write_bytes(os, &rec, sizeof(rec));
    }
    write_bytes(os, db.pool.data.data(), db.pool.data.size());
}

bool read_points(const std::string& path, std::vector<PointRecord>& out) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;

    Header h{};
    if (!read_bytes(is, &h, sizeof(h)) || h.magic != MAGIC_POINTS) return false;

    std::vector<PointRec> recs(h.n_records);
    if (h.n_records)
        read_bytes(is, recs.data(), sizeof(PointRec) * h.n_records);

    std::vector<char> strings(h.strings_size);
    if (h.strings_size) read_bytes(is, strings.data(), h.strings_size);

    out.clear();
    out.reserve(h.n_records);
    for (const auto& r : recs) {
        PointRecord pr;
        pr.id          = r.id;
        pr.lat         = r.lat_e7 / 1e7;
        pr.lon         = r.lon_e7 / 1e7;
        pr.name        = str_at(strings, r.name_off);
        pr.type        = str_at(strings, r.type_off);
        pr.street      = str_at(strings, r.street_off);
        pr.housenumber = str_at(strings, r.housenumber_off);
        pr.postcode    = str_at(strings, r.postcode_off);
        pr.country     = str_at(strings, r.country_off);
        pr.state       = str_at(strings, r.state_off);
        pr.city        = str_at(strings, r.city_off);
        pr.suburb      = str_at(strings, r.suburb_off);
        out.push_back(std::move(pr));
    }
    std::cout << "  Loaded " << out.size() << " points (binary)\n";
    return true;
}

// ============================================================
//  LINES
// ============================================================

void write_lines(const DES::OsmData& db, const std::string& path) {
    std::ofstream os(path, std::ios::binary);
    if (!os) { std::cerr << "Cannot open " << path << " for writing\n"; return; }

    // Compute total coord pairs across all lines
    uint64_t total_coords = 0;
    for (const auto& l : db.lines) total_coords += l.coords.size();

    Header h{};
    h.magic        = MAGIC_LINES;
    h.version      = FORMAT_VERSION;
    h.n_records    = db.lines.size();
    h.payload_size = total_coords * 2 * sizeof(int32_t);
    h.strings_size = db.pool.data.size();
    write_bytes(os, &h, sizeof(h));

    // Write records
    LineRec rec{};
    uint64_t off = 0;
    for (const auto& l : db.lines) {
        rec.id           = l.osm_id;
        rec.name_off     = l.name_off;
        rec.type_off     = l.type_off;
        rec.ref_off      = l.ref_off;
        rec.min_lat      = l.min_lat_e7;
        rec.max_lat      = l.max_lat_e7;
        rec.min_lon      = l.min_lon_e7;
        rec.max_lon      = l.max_lon_e7;
        rec.coord_offset = off;
        rec.coord_count  = (uint32_t)l.coords.size();
        write_bytes(os, &rec, sizeof(rec));
        off += l.coords.size();
    }
    // Write coord blob
    for (const auto& l : db.lines)
        for (const auto& c : l.coords) {
            write_bytes(os, &c.first,  sizeof(int32_t));
            write_bytes(os, &c.second, sizeof(int32_t));
        }
    // Write string pool
    write_bytes(os, db.pool.data.data(), db.pool.data.size());
}

bool read_lines(const std::string& path, std::vector<LineRecord>& out) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;

    Header h{};
    if (!read_bytes(is, &h, sizeof(h)) || h.magic != MAGIC_LINES) return false;

    std::vector<LineRec> recs(h.n_records);
    if (h.n_records) read_bytes(is, recs.data(), sizeof(LineRec) * h.n_records);

    uint64_t total_coords = h.payload_size / (2 * sizeof(int32_t));
    std::vector<int32_t> coords(2 * total_coords);
    if (total_coords) read_bytes(is, coords.data(), h.payload_size);

    std::vector<char> strings(h.strings_size);
    if (h.strings_size) read_bytes(is, strings.data(), h.strings_size);

    out.clear();
    out.reserve(h.n_records);
    for (const auto& r : recs) {
        LineRecord lr;
        lr.id      = r.id;
        lr.name    = str_at(strings, r.name_off);
        lr.type    = str_at(strings, r.type_off);
        lr.ref     = str_at(strings, r.ref_off);
        lr.min_lat = r.min_lat / 1e7;
        lr.max_lat = r.max_lat / 1e7;
        lr.min_lon = r.min_lon / 1e7;
        lr.max_lon = r.max_lon / 1e7;
        lr.coords.reserve(r.coord_count * 2);
        for (uint32_t k = 0; k < r.coord_count; ++k) {
            int32_t lat_e7 = coords[2 * (r.coord_offset + k)];
            int32_t lon_e7 = coords[2 * (r.coord_offset + k) + 1];
            lr.coords.push_back(lat_e7 / 1e7);
            lr.coords.push_back(lon_e7 / 1e7);
        }
        out.push_back(std::move(lr));
    }
    std::cout << "  Loaded " << out.size() << " lines (binary)\n";
    return true;
}

// ============================================================
//  ADMIN
// ============================================================

void write_admin(const DES::OsmData& db, const std::string& path) {
    std::ofstream os(path, std::ios::binary);
    if (!os) { std::cerr << "Cannot open " << path << " for writing\n"; return; }

    uint64_t total_ring_pts = 0;
    for (const auto& a : db.admin_areas) total_ring_pts += a.outer_ring.size();

    Header h{};
    h.magic        = MAGIC_ADMIN;
    h.version      = FORMAT_VERSION;
    h.n_records    = db.admin_areas.size();
    h.payload_size = total_ring_pts * 2 * sizeof(int32_t);
    h.strings_size = db.pool.data.size();
    write_bytes(os, &h, sizeof(h));

    AdminRec rec{};
    uint64_t off = 0;
    for (const auto& a : db.admin_areas) {
        rec.id           = a.osm_id;
        rec.name_off     = a.name_off;
        rec.min_lat      = a.min_lat_e7;
        rec.max_lat      = a.max_lat_e7;
        rec.min_lon      = a.min_lon_e7;
        rec.max_lon      = a.max_lon_e7;
        rec.ring_offset  = off;
        rec.ring_count   = (uint32_t)a.outer_ring.size();
        rec.admin_level  = a.admin_level;
        std::memset(rec._pad, 0, sizeof(rec._pad));
        write_bytes(os, &rec, sizeof(rec));
        off += a.outer_ring.size();
    }
    for (const auto& a : db.admin_areas)
        for (const auto& c : a.outer_ring) {
            write_bytes(os, &c.first,  sizeof(int32_t));
            write_bytes(os, &c.second, sizeof(int32_t));
        }
    write_bytes(os, db.pool.data.data(), db.pool.data.size());
}

bool read_admin(const std::string& path, std::vector<AdminRecord>& out) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;

    Header h{};
    if (!read_bytes(is, &h, sizeof(h)) || h.magic != MAGIC_ADMIN) return false;

    std::vector<AdminRec> recs(h.n_records);
    if (h.n_records) read_bytes(is, recs.data(), sizeof(AdminRec) * h.n_records);

    uint64_t total_pts = h.payload_size / (2 * sizeof(int32_t));
    std::vector<int32_t> ring(2 * total_pts);
    if (total_pts) read_bytes(is, ring.data(), h.payload_size);

    std::vector<char> strings(h.strings_size);
    if (h.strings_size) read_bytes(is, strings.data(), h.strings_size);

    out.clear();
    out.reserve(h.n_records);
    for (const auto& r : recs) {
        AdminRecord ar;
        ar.id          = r.id;
        ar.admin_level = r.admin_level;
        ar.name        = str_at(strings, r.name_off);
        ar.min_lat     = r.min_lat / 1e7;
        ar.max_lat     = r.max_lat / 1e7;
        ar.min_lon     = r.min_lon / 1e7;
        ar.max_lon     = r.max_lon / 1e7;
        ar.ring.reserve(r.ring_count * 2);
        for (uint32_t k = 0; k < r.ring_count; ++k) {
            int32_t lat_e7 = ring[2 * (r.ring_offset + k)];
            int32_t lon_e7 = ring[2 * (r.ring_offset + k) + 1];
            ar.ring.push_back(lat_e7 / 1e7);
            ar.ring.push_back(lon_e7 / 1e7);
        }
        out.push_back(std::move(ar));
    }
    std::cout << "  Loaded " << out.size() << " admin areas (binary)\n";
    return true;
}

} // namespace bin
