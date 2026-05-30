// ============================================================
//  Single-polygon PIP with slab acceleration.
//  Sheet 2 Mandatory Task 1.
//
//  Each polygon's outer ring is partitioned into NUM_SLABS
//  horizontal bands by latitude. A query point only scans
//  edges whose y-range covers its latitude — O(few) per PIP
//  instead of O(|E|).
// ============================================================
#pragma once
#include <cstdint>
#include <vector>

namespace DES { struct AdminArea; }

namespace geo {

constexpr int      NUM_SLABS  = 64;
constexpr uint32_t INVALID_ID = 0xFFFFFFFFu;

// ---------------------------------------------------------------
//  Administrative tier (Europe-wide normalisation).
//  Collapses OSM's 10-or-so admin_level values into 4 semantic
//  tiers that matter for reverse geocoding and front-end display.
//  Anything outside the target set is dropped during build().
// ---------------------------------------------------------------
enum class Tier : uint8_t {
    NONE    = 0,
    COUNTRY = 1,   // admin_level == 2  (DE, FR, IT, ...)
    STATE   = 2,   // admin_level == 4  (Bundesland / Région / Regione)
    CITY    = 3,   // admin_level == 8  (Gemeinde / Commune / Comune)
    SUBURB  = 4,   // admin_level ∈ {9,10,11} (Stadtteil / Quartier / PLZ)
};

inline Tier admin_level_to_tier(uint8_t lvl) {
    switch (lvl) {
        case 2:                       return Tier::COUNTRY;
        case 4:                       return Tier::STATE;
        case 8:                       return Tier::CITY;
        case 9: case 10: case 11:     return Tier::SUBURB;
        default:                      return Tier::NONE;
    }
}

inline const char* tier_name(Tier t) {
    switch (t) {
        case Tier::COUNTRY: return "country";
        case Tier::STATE:   return "state";
        case Tier::CITY:    return "city";
        case Tier::SUBURB:  return "suburb";
        default:            return "none";
    }
}

struct SlabEdge {
    int32_t lat0, lon0;
    int32_t lat1, lon1;
};

struct PolygonNode {
    uint32_t admin_idx   = INVALID_ID;   // index into OsmData::admin_areas
    uint8_t  admin_level = 0;
    Tier     tier        = Tier::NONE;   // normalised semantic tier

    int32_t  min_lat = 0, max_lat = 0;
    int32_t  min_lon = 0, max_lon = 0;
    int64_t  area_abs = 0;               // |signed area| via Shoelace (lat_e7 * lon_e7)

    int32_t  rep_lat = 0, rep_lon = 0;   // representative point (lies on boundary)

    // Hierarchy (Optional Task 4)
    uint32_t              parent = INVALID_ID;
    std::vector<uint32_t> children;

    // Slab index (Mandatory Task 1)
    int32_t  slab_lat_min  = 0;
    int64_t  slab_lat_span = 1;          // (max_lat - min_lat + 1)
    std::vector<SlabEdge>  edges;        // duplicated across slabs they cross
    std::vector<uint32_t>  slab_offsets; // size = NUM_SLABS + 1
};

// Build slab index, bbox, area and representative point for one AdminArea.
void build_slab_node(const DES::AdminArea& a, uint32_t idx, PolygonNode& out);

// Point-in-polygon test (half-open ray casting). lat/lon in 1e7 fixed-point.
bool pip_single(const PolygonNode& n, int32_t lat, int32_t lon);

inline bool bbox_contains(const PolygonNode& n, int32_t lat, int32_t lon) {
    return lat >= n.min_lat && lat <= n.max_lat &&
           lon >= n.min_lon && lon <= n.max_lon;
}

} // namespace geo
