// ============================================================
//  Binary I/O – Sheet 3 Task 4 (foundation).
//
//  Each artifact (points / lines / admin) is written to its own
//  self-contained binary file:
//
//    [Header]                    fixed-size, contains record count
//    [N x Record]                fixed-size POD per record
//    [Variable payload]          coord arrays, ring arrays
//    [String pool]               null-terminated UTF-8 strings,
//                                referenced by offsets in records
//
//  All multi-byte integers are little-endian (assumed by C++ on
//  x86 / ARM64 / WSL). No alignment padding beyond field order.
// ============================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace DES { struct OsmData; }
struct PointRecord;
struct LineRecord;
struct AdminRecord;

namespace bin {

// ------- File magics -------
constexpr uint32_t MAGIC_POINTS = 0x31544E50u;   // "PNT1"
constexpr uint32_t MAGIC_LINES  = 0x314E494Cu;   // "LIN1"
constexpr uint32_t MAGIC_ADMIN  = 0x31314D44u;   // "DM11"  (any 4-char value)
constexpr uint32_t FORMAT_VERSION = 1;

// ------- Record structs (on-disk layout) -------
#pragma pack(push, 1)
struct PointRec {
    int64_t  id;
    int32_t  lat_e7;
    int32_t  lon_e7;
    uint32_t name_off, type_off, street_off, housenumber_off, postcode_off;
    uint32_t country_off, state_off, city_off, suburb_off;
};  // = 52 bytes

struct LineRec {
    int64_t  id;
    uint32_t name_off, type_off, ref_off;
    int32_t  min_lat, max_lat, min_lon, max_lon;
    uint64_t coord_offset;   // index into shared coord blob (pairs)
    uint32_t coord_count;    // number of (lat, lon) pairs
};  // = 52 bytes

struct AdminRec {
    int64_t  id;
    uint32_t name_off;
    int32_t  min_lat, max_lat, min_lon, max_lon;
    uint64_t ring_offset;
    uint32_t ring_count;
    uint8_t  admin_level;
    uint8_t  _pad[3];
};  // = 48 bytes

struct Header {
    uint32_t magic;
    uint32_t version;
    uint64_t n_records;
    uint64_t payload_size;   // bytes of variable payload (coords/ring)
    uint64_t strings_size;
};  // = 32 bytes
#pragma pack(pop)

// ------- Writers (DES → binary) -------
void write_points(const DES::OsmData& db, const std::string& path);
void write_lines (const DES::OsmData& db, const std::string& path);
void write_admin (const DES::OsmData& db, const std::string& path);

// ------- Readers (binary → ApiHandler types) -------
bool read_points(const std::string& path, std::vector<PointRecord>& out);
bool read_lines (const std::string& path, std::vector<LineRecord>&  out);
bool read_admin (const std::string& path, std::vector<AdminRecord>& out);

} // namespace bin
