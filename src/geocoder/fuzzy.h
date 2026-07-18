// ============================================================
//  Sheet 3 Optional Task — Fuzzy Search.
//
//  Provides two things:
//
//    1. A Damerau-Levenshtein edit distance function (the standard
//       Levenshtein with an extra rule for adjacent-character swaps,
//       which covers ~15% of real typos on top of pure Levenshtein).
//
//    2. A BK-tree over normalized tokens.  Insertion is O(log N)
//       average.  Query for "all words within distance k" uses the
//       triangle inequality on Levenshtein distance to prune
//       branches — most queries visit √N nodes.
//
//  Both are used by the geocoder as a FALLBACK: only invoked when
//  the main (exact + synonym) search returns empty or very weak
//  results.  This preserves precision when the user's spelling is
//  fine while still rescuing them when it isn't.
// ============================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace geocoder {

// Full-DP Damerau-Levenshtein distance. Bytes are compared as-is —
// callers must pass DIN-normalised ASCII strings (Task 1 output).
int damerau_levenshtein(const std::string& a, const std::string& b);

// Adaptive maximum edit distance based on the length of a token.
// Longer tokens tolerate more edits without exploding the false-positive
// rate; very short tokens (e.g. "am", "der") only allow 1 edit.
int fuzzy_threshold(std::size_t len);

class BKTree {
public:
    struct Match {
        int         dist;
        std::string word;
    };

    void reserve(std::size_t hint);
    void insert(std::string word);

    // Collect every dictionary word within `max_dist` of `query`.
    // Results are in tree-traversal order, not sorted by distance.
    void search(const std::string& query, int max_dist,
                std::vector<Match>& out) const;

    // Convenience: return the single closest word (by distance),
    // or the empty string if none is within `max_dist`.
    std::string best_match(const std::string& query, int max_dist) const;

    std::size_t size()  const { return nodes_.size(); }
    bool        empty() const { return nodes_.empty(); }

private:
    struct Node {
        std::string word;
        // (edit distance to child, child index into nodes_)
        std::vector<std::pair<int, std::uint32_t>> children;
    };
    std::vector<Node> nodes_;
};

} // namespace geocoder
