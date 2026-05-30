// ============================================================
//  Polygon hierarchy + top-down PIP query.
//  Sheet 2 Optional Task 4 (with mandatory T1 PIP underneath).
//
//  Builds parent/child links between admin polygons by PIP-ing
//  each polygon's representative point against potential parents
//  at lower admin_level. The resulting tree lets PIP queries
//  prune entire sub-branches: a point that is not in Germany
//  won't be tested against any city inside Germany.
// ============================================================
#pragma once
#include "slab_polygon.h"
#include "grid_index.h"
#include <unordered_map>
#include <string>
#include <utility>

namespace DES { struct AdminArea; struct OsmData; }

namespace geo {

struct PIPResult {
    // Node IDs from outermost tier down to innermost match.
    //   [country] → [state] → [city] → [suburb]
    std::vector<uint32_t> chain;
};

// Reverse-geocoded address — exactly the 4 tiers the project standardises on.
struct AddressInfo {
    std::string country;
    std::string state;
    std::string city;
    std::string suburb;

    bool any() const {
        return !country.empty() || !state.empty()
            || !city.empty()    || !suburb.empty();
    }

    // "suburb, city, state, country" — empty fields skipped.
    std::string format() const {
        std::string s;
        auto add = [&](const std::string& part) {
            if (part.empty()) return;
            if (!s.empty()) s += ", ";
            s += part;
        };
        add(suburb); add(city); add(state); add(country);
        return s;
    }
};

struct HierarchyStats {
    size_t node_count         = 0;
    size_t root_count         = 0;
    size_t intersection_pairs = 0;
    int    max_depth          = 0;
    double slab_build_ms      = 0;
    double level_grid_ms      = 0;
    double hierarchy_ms       = 0;
    double intersect_ms       = 0;

    // Per-tier counts (post-filtering)
    size_t tier_country = 0;
    size_t tier_state   = 0;
    size_t tier_city    = 0;
    size_t tier_suburb  = 0;
    size_t tier_dropped = 0;   // polygons skipped because admin_level didn't match a target tier
};

class PolygonHierarchy {
public:
    void build(const std::vector<DES::AdminArea>& admins);

    // Top-down PIP. Returns empty chain if no root contains the point.
    PIPResult query(int32_t lat_e7, int32_t lon_e7) const;

    PIPResult query_deg(double lat_deg, double lon_deg) const {
        return query((int32_t)(lat_deg * 1e7), (int32_t)(lon_deg * 1e7));
    }

    // Reverse-geocode: returns the 4-tier address of the given point.
    // For the SUBURB tier we keep the deepest match (highest admin_level).
    AddressInfo query_address(int32_t lat_e7, int32_t lon_e7,
                              const DES::OsmData& db) const;

    // Independent PIP at a single admin_level — bypasses parent/child
    // chains. Returns the smallest polygon at that level that contains
    // the point, or INVALID_ID. Robust when the hierarchy linkage is
    // broken because of mis-assembled outer rings.
    uint32_t pip_at_level(int32_t lat_e7, int32_t lon_e7,
                          uint8_t admin_level) const;

    AddressInfo query_address_deg(double lat_deg, double lon_deg,
                                  const DES::OsmData& db) const {
        return query_address((int32_t)(lat_deg * 1e7),
                             (int32_t)(lon_deg * 1e7), db);
    }

    const std::vector<PolygonNode>&                  nodes()         const { return nodes_; }
    const std::vector<uint32_t>&                     roots()         const { return roots_; }
    const std::vector<std::pair<uint32_t,uint32_t>>& intersections() const { return intersections_; }
    const HierarchyStats&                            stats()         const { return stats_; }

private:
    std::vector<PolygonNode>                       nodes_;
    std::vector<uint32_t>                          roots_;
    std::unordered_map<uint8_t, GridIndex>         level_grids_;
    std::vector<std::pair<uint32_t,uint32_t>>      intersections_;
    HierarchyStats                                 stats_;

    void build_slabs        (const std::vector<DES::AdminArea>& admins);
    void build_level_grids  ();
    void build_hierarchy    ();
    void detect_intersections(const std::vector<DES::AdminArea>& admins);

    void descend(uint32_t node_id, int32_t lat, int32_t lon, PIPResult& r) const;
    int  compute_max_depth() const;
};

} // namespace geo
