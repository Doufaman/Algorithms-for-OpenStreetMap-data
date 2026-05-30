#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cstddef>

// ============================================================
//  DES = Data Extraction and Storage
//
//  Optimised two-pass pipeline:
//    extraction()  – Pass-1 nodes only, Pass-2 ways+relations
//                    Objects are classified and converted to
//                    Points/Lines/AdminAreas on the fly.
//                    No full raw_nodes / raw_ways arrays kept.
//    processing()  – Releases temporary caches; Hilbert-sorts
//                    the output arrays for later R-Tree build.
//    storage()     – Serialises to JSON + prints stats.
// ============================================================
namespace DES {

    // ------------------------------------------------------------
    //  StringPool
    // ------------------------------------------------------------
    struct StringPool {
        static constexpr uint32_t NONE = 0xFFFFFFFFu;

        std::vector<char>                         data;
        std::unordered_map<std::string, uint32_t> index;

        StringPool() { data.push_back('\0'); }

        uint32_t intern(const std::string& s) {
            if (s.empty()) return NONE;
            auto it = index.find(s);
            if (it != index.end()) return it->second;
            uint32_t off = static_cast<uint32_t>(data.size());
            data.insert(data.end(), s.begin(), s.end());
            data.push_back('\0');
            index[s] = off;
            return off;
        }

        uint32_t intern(const char* s) {
            if (!s || !*s) return NONE;
            return intern(std::string(s));
        }

        const char* get(uint32_t off) const {
            if (off == NONE || off >= data.size()) return "";
            return data.data() + off;
        }

        size_t byte_size() const { return data.size(); }
    };

    // ------------------------------------------------------------
    //  Tag
    // ------------------------------------------------------------
    struct Tag {
        uint32_t key_off;
        uint32_t val_off;
    };

    // ------------------------------------------------------------
    //  Semantic output types
    // ------------------------------------------------------------
    struct Point {
        int64_t  osm_id;
        int32_t  lat_e7;
        int32_t  lon_e7;
        uint32_t name_off = StringPool::NONE;
        uint32_t type_off = StringPool::NONE;
        uint32_t street_off = StringPool::NONE;
        uint32_t housenumber_off = StringPool::NONE;
        uint32_t postcode_off = StringPool::NONE;

        // Admin chain (Sheet 2 Task 2). Filled by geo::attach_admin_to_db
        // after PIP hierarchy is built. All are StringPool offsets.
        uint32_t country_off = StringPool::NONE;   // tier COUNTRY
        uint32_t state_off   = StringPool::NONE;   // tier STATE
        uint32_t city_off    = StringPool::NONE;   // tier CITY
        uint32_t suburb_off  = StringPool::NONE;   // tier SUBURB (deepest)

        std::vector<Tag> extra_tags;
    };

    struct Line {
        int64_t  osm_id;
        uint32_t name_off = StringPool::NONE;
        uint32_t type_off = StringPool::NONE;
        uint32_t ref_off = StringPool::NONE;
        std::vector<std::pair<int32_t, int32_t>> coords;
        int32_t min_lat_e7 = 0, max_lat_e7 = 0;
        int32_t min_lon_e7 = 0, max_lon_e7 = 0;
    };

    struct AdminArea {
        int64_t  osm_id;
        uint8_t  admin_level = 0;
        uint32_t name_off = StringPool::NONE;
        std::vector<std::pair<int32_t, int32_t>> outer_ring;
        int32_t min_lat_e7 = 0, max_lat_e7 = 0;
        int32_t min_lon_e7 = 0, max_lon_e7 = 0;
    };

    // ------------------------------------------------------------
    //  NodeCache
    //  Stores only coordinates (no tags) for geometry resolution.
    //  Packs (lat_e7, lon_e7) into one uint64_t → 8 bytes/entry.
    // ------------------------------------------------------------
    struct NodeCache {
        // id → packed coords: high 32 = lat_e7, low 32 = lon_e7
        std::unordered_map<int64_t, uint64_t> data;

        void insert(int64_t id, int32_t lat, int32_t lon) {
            data[id] = (static_cast<uint64_t>(static_cast<uint32_t>(lat)) << 32)
                | static_cast<uint64_t>(static_cast<uint32_t>(lon));
        }

        bool lookup(int64_t id, int32_t& lat, int32_t& lon) const {
            auto it = data.find(id);
            if (it == data.end()) return false;
            lat = static_cast<int32_t>(it->second >> 32);
            lon = static_cast<int32_t>(it->second & 0xFFFFFFFFull);
            return true;
        }

        size_t byte_size() const {
            // bucket overhead ≈ 1 pointer per bucket + entry itself
            return data.size() * (sizeof(int64_t) + sizeof(uint64_t) + 8);
        }
    };

    // ------------------------------------------------------------
    //  WayGeomCache
    //  For each way that is an outer-ring member of a boundary
    //  relation, we cache its resolved coordinate sequence so the
    //  relation pass can assemble polygons in O(1) per member.
    // ------------------------------------------------------------
    using CoordSeq = std::vector<std::pair<int32_t, int32_t>>;

    struct WayGeomCache {
        std::unordered_map<int64_t, CoordSeq> data;

        void insert(int64_t way_id, CoordSeq seq) {
            data.emplace(way_id, std::move(seq));
        }

        const CoordSeq* find(int64_t way_id) const {
            auto it = data.find(way_id);
            return it != data.end() ? &it->second : nullptr;
        }

        size_t byte_size() const {
            size_t total = 0;
            for (auto& kv : data)
                total += kv.second.capacity() * sizeof(CoordSeq::value_type);
            return total;
        }
    };

    // ------------------------------------------------------------
    //  RawRelation  (kept small – only needed for admin boundaries)
    // ------------------------------------------------------------
    struct RawMember {
        char     type;      // 'n' | 'w' | 'r'
        int64_t  ref;
        uint32_t role_off;
    };

    struct RawRelation {
        int64_t                id;
        std::vector<RawMember> members;
        std::vector<Tag>       tags;
    };

    // ------------------------------------------------------------
    //  OsmData – central database
    // ------------------------------------------------------------
    struct OsmData {
        StringPool pool;

        // Temporary caches (freed after processing)
        NodeCache    node_cache;
        WayGeomCache way_geom_cache;

        // Raw relations are kept until processing resolves them
        std::vector<RawRelation> raw_relations;

        // Way IDs referenced by boundary-admin relations.
        // Populated by the pre-scan pass; used in Pass 2 to ensure
        // tagless boundary segment ways still get their geometry cached.
        std::unordered_set<int64_t> needed_way_ids;

        // Final semantic output
        std::vector<Point>     points;
        std::vector<Line>      lines;
        std::vector<AdminArea> admin_areas;
    };

    // ------------------------------------------------------------
    //  Stats
    // ------------------------------------------------------------
    struct Stats {
        size_t raw_node_count = 0;
        size_t raw_way_count = 0;
        size_t raw_relation_count = 0;
        size_t point_count = 0;
        size_t line_count = 0;
        size_t admin_count = 0;

        size_t node_cache_bytes = 0;
        size_t way_cache_bytes = 0;
        size_t raw_rel_bytes = 0;
        size_t points_bytes = 0;
        size_t lines_bytes = 0;
        size_t admin_bytes = 0;
        size_t string_pool_bytes = 0;

        double prescan_ms = 0;
        double pass1_ms = 0;
        double pass2_ms = 0;
        double processing_ms = 0;
        double storage_ms = 0;

        size_t needed_way_ids_count = 0;
    };

    // ------------------------------------------------------------
    //  Pipeline
    // ------------------------------------------------------------
    void extraction(const std::string& pbf_path, OsmData& db, Stats& stats);
    void processing(OsmData& db, Stats& stats);
    void storage(const OsmData& db, Stats& stats, const std::string& out_dir);
    void extractFromPBF(const std::string& pbf_path);

} // namespace DES
