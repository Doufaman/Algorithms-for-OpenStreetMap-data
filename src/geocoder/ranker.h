// ============================================================
//  Sheet 3 Task 3 — Ranking.
//
//  Assigns a numeric score to every geocoder candidate so that
//  multi-hit queries can be returned "best first" instead of
//  in arbitrary index order.
//
//  We use a WEIGHTED SUM of four independent factors, each
//  normalised to 0..1 before weighting:
//
//    • match_quality      : |matched query tokens| / |query tokens|
//    • specificity        : house > street > POI > admin
//                           (more specific object ranks higher)
//    • admin_consistency  : does the object's admin chain contain
//                           the admin tokens the user typed?
//    • exactness          : exact whole-name match vs. token subset
//
//  Weights are tunable via config.h — see Config::RANK_*.
// ============================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "inverted_index.h"

struct PointRecord;
struct LineRecord;
struct AdminRecord;

namespace geocoder {

// One scored candidate — kind + id + numeric score + a short
// human-readable reason for debugging / display.
struct Scored {
    ObjectRef   ref;
    double      score = 0.0;
    int         matched_tokens = 0;
    std::string reason;      // e.g. "street name match, admin=Aalen"
};

// Context passed into scoring. Carries pointers to the loaded data
// so the ranker can inspect the candidate's admin chain, etc.
struct RankContext {
    const std::vector<PointRecord>* points = nullptr;
    const std::vector<LineRecord>*  lines  = nullptr;
    const std::vector<AdminRecord>* admins = nullptr;

    // Query tokens split into two conceptual buckets:
    //   • object_tokens : what we think names the target object
    //   • admin_tokens  : what we think names the surrounding admin
    // Either bucket may be empty. Both together should reconstruct
    // the tokens (post normalization).
    std::vector<std::string> object_tokens;
    std::vector<std::string> admin_tokens;
};

// -----------------------------------------------------------
//  Ranker
// -----------------------------------------------------------
class Ranker {
public:
    // Score a single object candidate under the current context.
    // Returns a Scored record; caller can drop it if score too low.
    Scored score(const ObjectRef& ref, const RankContext& ctx) const;

private:
    // Return the display name of an object (for exactness comparison).
    std::string object_name(const ObjectRef& ref, const RankContext& ctx) const;

    // Concatenate the admin chain of the object into a lowercase blob
    // for containment tests against admin_tokens.
    std::string object_admin_blob(const ObjectRef& ref, const RankContext& ctx) const;
};

} // namespace geocoder
