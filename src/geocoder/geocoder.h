// ============================================================
//  Sheet 3 Task 3 — Query orchestrator ("smart" geocoder).
//
//  Sits in front of the InvertedIndex + Ranker and adds:
//
//    • numeric extraction    — 1..4-digit tokens are housenumber
//                              candidates, 5-digit tokens are also
//                              tried as postcodes
//    • split enumeration     — for a query like "Aalen Hauptstrasse"
//                              try each contiguous split of the tokens
//                              into (object_tokens, admin_tokens);
//                              keep whichever combination scores best
//    • housenumber lookup    — if a housenumber candidate exists and
//                              the best object is a street, look up
//                              houses on that street via the point
//                              grid index and return the matching
//                              building instead
//    • aggregation           — same object can appear under multiple
//                              splits; keep the highest score
//
//  This is the class the ApiHandler calls for /api/search.
// ============================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "inverted_index.h"
#include "ranker.h"

struct PointRecord;
struct LineRecord;
struct AdminRecord;
struct PointGridIndex;

namespace geocoder {

class Geocoder {
public:
    Geocoder(const InvertedIndex& index,
             const std::vector<PointRecord>& points,
             const std::vector<LineRecord>&  lines,
             const std::vector<AdminRecord>& admins,
             const PointGridIndex& point_grid)
        : index_(index), points_(points), lines_(lines),
          admins_(admins), point_grid_(point_grid) {}

    // Full geocoder query. Applies normalization + split enumeration
    // + ranking + optional housenumber refinement. Returns top-`limit`
    // scored results, best score first.
    std::vector<Scored> search(const std::string& q, int limit) const;

private:
    const InvertedIndex&               index_;
    const std::vector<PointRecord>&    points_;
    const std::vector<LineRecord>&     lines_;
    const std::vector<AdminRecord>&    admins_;
    const PointGridIndex&              point_grid_;
    Ranker                             ranker_;

    // Extract numeric tokens (housenumber / postcode candidates) from
    // the input token list. `nums` gets the digit strings, `words` gets
    // the remaining word tokens.
    static void split_numeric(const std::vector<std::string>& in,
                              std::vector<std::string>&       words,
                              std::vector<std::string>&       nums);

    // For a given (object_tokens, admin_tokens) split and its query hits,
    // score every hit and merge into `best` keyed by ObjectRef.
    void score_split(const std::vector<std::string>& obj_tokens,
                     const std::vector<std::string>& adm_tokens,
                     std::vector<Scored>&            out_scored) const;

    // Housenumber refinement.  If any Street-kind result is in `results`
    // and the query contains a housenumber, replace the top street match
    // with the specific house (if found) that lies on that street with
    // that housenumber.  Otherwise leaves `results` alone.
    void refine_housenumber(std::vector<Scored>&           results,
                            const std::string&             housenumber,
                            const std::vector<std::string>& adm_tokens) const;
};

} // namespace geocoder
