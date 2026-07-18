// ============================================================
//  Sheet 3 Task 2 — Inverted Index base routine.
//
//  Maps a normalized token to the list of objects whose name
//  contains that token. Supports OSM Points (POIs), Lines
//  (streets) and Admin polygons. Uses the DIN + strip dual
//  normalization from Task 1, so a query in any spelling
//  variant (München / Muenchen / Munchen) hits the same set.
//
//  Task 2 delivers only "single-token AND"; smarter query
//  splitting and ranking come in Task 3.
// ============================================================
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "fuzzy.h"

// Forward declarations — implementations live in api_handler.h
struct PointRecord;
struct LineRecord;
struct AdminRecord;

namespace geocoder {

enum class ObjectKind : uint8_t {
    POI    = 0,   // db.points with a `name` tag (amenity, shop, etc.)
    Street = 1,   // db.lines whose name is non-empty
    Admin  = 2,   // db.admin_areas (city, state, ...)
};

const char* kind_name(ObjectKind k);

struct ObjectRef {
    ObjectKind kind;
    uint32_t   id;      // index into the corresponding vector

    bool operator==(const ObjectRef& o) const {
        return kind == o.kind && id == o.id;
    }
};

struct ObjectRefHash {
    size_t operator()(const ObjectRef& r) const noexcept {
        return (size_t)r.id * 4u + (size_t)r.kind;
    }
};

// -----------------------------------------------------------
//  InvertedIndex
// -----------------------------------------------------------
class InvertedIndex {
public:
    // Build from the ApiHandler's already-loaded records.
    // Traverses every point with a `name`, every line with a `name`,
    // and every admin. For each name, tokenises with BOTH the DIN
    // and the strip normalizer, then adds the (deduplicated) token
    // set to the index.
    void build(const std::vector<PointRecord>& points,
               const std::vector<LineRecord>&  lines,
               const std::vector<AdminRecord>& admins);

    // Look up one already-normalized token. Empty vector if miss.
    const std::vector<ObjectRef>& query_token(const std::string& tok) const;

    // Multi-token AND query. `tokens` is the tokenised, normalized
    // form of the user query. Returns the intersection of hits per
    // token (i.e. every returned object contains all query tokens).
    // Empty input → empty result.
    std::vector<ObjectRef> query_all(const std::vector<std::string>& tokens) const;

    // Stats for the console report.
    size_t token_count()   const { return map_.size(); }
    size_t total_entries() const;

    // Fuzzy-search fallback: BK-tree over the token vocabulary.
    // Populated at the end of build() so a Geocoder can correct
    // typos in query tokens.
    const BKTree& bktree() const { return bktree_; }

private:
    std::unordered_map<std::string, std::vector<ObjectRef>> map_;
    BKTree                                                  bktree_;
    static const std::vector<ObjectRef> kEmpty;
};

} // namespace geocoder
