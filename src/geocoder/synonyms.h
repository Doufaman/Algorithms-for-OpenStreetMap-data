// ============================================================
//  Sheet 3 Optional Task — Mixed Language Search (DE ↔ EN).
//
//  Non-native users often reach for the English form of a German
//  place or category — "Munich station" instead of "München
//  Hauptbahnhof". This module translates the tokenized query into
//  its German-canonical form so the inverted index can hit.
//
//  Three tables are consulted, all keyed by DIN-normalised tokens
//  (matching how tokenize_din() outputs them):
//
//    • BIGRAMS : two-token English phrases  → German target
//                "main station" → "hauptbahnhof"
//    • UNIGRAMS: one-token English category → German target
//                "station" → "bahnhof"
//    • EXONYMS : one-token English exonym   → German endonym
//                "munich" → "muenchen"
//
//  Bigrams are tried FIRST (greedy longest match); unigrams and
//  exonyms are then applied to what's left.
//
//  The output preserves the DIN-normalised form (e.g. "muenchen",
//  not "München"), so downstream tokenizer / inverted-index lookup
//  needs no further transformation.
// ============================================================
#pragma once
#include <string>
#include <vector>

namespace geocoder {

struct SynonymExpansion {
    std::vector<std::string> tokens;    // possibly transformed
    std::string              trace;     // human-readable summary of what changed
    bool                     changed = false;
};

// Apply the synonym / exonym tables to a token list. If nothing matches,
// tokens are returned unchanged and `changed` is false.  The `trace` is
// suitable for stitching into a `reason` field for frontend display.
SynonymExpansion apply_synonyms(const std::vector<std::string>& tokens);

} // namespace geocoder
