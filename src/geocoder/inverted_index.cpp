#include "inverted_index.h"
#include "normalizer.h"
#include "server/api_handler.h"

#include <algorithm>
#include <unordered_set>

namespace geocoder {

const std::vector<ObjectRef> InvertedIndex::kEmpty;

const char* kind_name(ObjectKind k) {
    switch (k) {
        case ObjectKind::POI:    return "poi";
        case ObjectKind::Street: return "street";
        case ObjectKind::Admin:  return "admin";
    }
    return "?";
}

// ------------------------------------------------------------
//  build
// ------------------------------------------------------------

namespace {

// Add one object's name into the index using both normalization
// variants. Duplicate tokens (which appear across the two variants
// when a name has no diacritics) are silently deduplicated per name.
void ingest(std::unordered_map<std::string, std::vector<ObjectRef>>& map,
            const std::string& name, ObjectRef ref) {
    if (name.empty()) return;

    // Collect unique tokens from both normalizations.
    std::unordered_set<std::string> tokens;
    for (auto& t : tokenize_din(name))   tokens.insert(std::move(t));
    for (auto& t : tokenize_strip(name)) tokens.insert(std::move(t));

    for (const auto& tok : tokens) {
        if (tok.empty()) continue;
        map[tok].push_back(ref);
    }
}

} // anonymous namespace

void InvertedIndex::build(const std::vector<PointRecord>& points,
                          const std::vector<LineRecord>&  lines,
                          const std::vector<AdminRecord>& admins) {
    map_.clear();
    map_.reserve(1 << 20);            // ~1M unique tokens is a fair upper bound

    // -------- Points (POIs) --------
    for (uint32_t i = 0; i < points.size(); ++i) {
        const auto& p = points[i];
        // A point is index-worthy if it has any searchable text.
        // Houses without a name are indexed under their street name so
        // "Hauptstrasse" search returns street candidates directly.
        // Skip pure-address points here; Task 3 will look those up via
        // the street object rather than duplicating them here.
        if (!p.name.empty())
            ingest(map_, p.name, { ObjectKind::POI, i });
    }

    // -------- Lines (streets) --------
    for (uint32_t i = 0; i < lines.size(); ++i) {
        const auto& l = lines[i];
        if (!l.name.empty())
            ingest(map_, l.name, { ObjectKind::Street, i });
    }

    // -------- Admin areas (cities, states, ...) --------
    for (uint32_t i = 0; i < admins.size(); ++i) {
        const auto& a = admins[i];
        if (!a.name.empty())
            ingest(map_, a.name, { ObjectKind::Admin, i });
    }

    // A single street is often broken into many OSM ways sharing the
    // same name. That's fine — the index will contain many entries per
    // token, and Task 3's aggregation step can bundle them by name.

    // -------- Fuzzy-search BK-tree ---------------------------------
    // Populate the BK-tree with every unique token. Very short tokens
    // (< 3 bytes) are skipped — they generate too many false matches
    // and don't need typo correction anyway.
    bktree_ = BKTree{};
    bktree_.reserve(map_.size());
    for (const auto& kv : map_) {
        if (kv.first.size() < 3) continue;
        bktree_.insert(kv.first);
    }
}

// ------------------------------------------------------------
//  query
// ------------------------------------------------------------

const std::vector<ObjectRef>& InvertedIndex::query_token(const std::string& tok) const {
    auto it = map_.find(tok);
    return (it != map_.end()) ? it->second : kEmpty;
}

std::vector<ObjectRef> InvertedIndex::query_all(const std::vector<std::string>& tokens) const {
    if (tokens.empty()) return {};

    // Look up each token; if any miss, the AND result is empty.
    std::vector<const std::vector<ObjectRef>*> hit_lists;
    hit_lists.reserve(tokens.size());
    for (const auto& tk : tokens) {
        const auto& v = query_token(tk);
        if (v.empty()) return {};        // any token miss → 0 hits
        hit_lists.push_back(&v);
    }

    // Start from the smallest posting list (classic inverted-index trick).
    std::sort(hit_lists.begin(), hit_lists.end(),
              [](const std::vector<ObjectRef>* a, const std::vector<ObjectRef>* b){
                  return a->size() < b->size();
              });

    // Seed with the smallest list, deduplicated.
    std::unordered_set<ObjectRef, ObjectRefHash> current(
        hit_lists[0]->begin(), hit_lists[0]->end());

    // Intersect against remaining lists.
    for (size_t k = 1; k < hit_lists.size() && !current.empty(); ++k) {
        std::unordered_set<ObjectRef, ObjectRefHash> other(
            hit_lists[k]->begin(), hit_lists[k]->end());
        std::unordered_set<ObjectRef, ObjectRefHash> next;
        for (const auto& r : current) if (other.count(r)) next.insert(r);
        current = std::move(next);
    }

    return { current.begin(), current.end() };
}

size_t InvertedIndex::total_entries() const {
    size_t total = 0;
    for (const auto& [_, v] : map_) total += v.size();
    return total;
}

} // namespace geocoder
