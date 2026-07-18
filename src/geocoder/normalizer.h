// ============================================================
//  Sheet 3 Task 1 — String Preprocessing.
//
//  Two canonical normalization functions for German geodata:
//
//    normalize_din   — DIN 5007 transliteration:
//                        ä→ae, ö→oe, ü→ue, ß→ss
//                      This is the official German substitution
//                      used in phone books, indexes, ID cards.
//
//    normalize_strip — Lazy user substitution:
//                        ä→a, ö→o, ü→u, ß→s
//                      Non-native speakers who don't know the
//                      DIN convention often just strip diacritics.
//
//  Both variants also:
//    • lowercase A-Z
//    • strip other European diacritics (é→e, ñ→n, č→c, ...)
//    • collapse whitespace / punctuation
//
//  A dual index over both forms lets the geocoder match every
//  reasonable spelling variant with zero query-time work.
//
//  Tokenization additionally:
//    • splits on whitespace and punctuation
//    • expands abbreviations (str. → strasse, hbf. → hauptbahnhof)
//    • splits compound suffixes (bahnhofstrasse → bahnhof + strasse)
// ============================================================
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace geocoder {

// ---------- Low-level normalizers ---------------------------

// DIN 5007 transliteration. UTF-8 in, ASCII-only out.
std::string normalize_din(std::string_view s);

// Strip variant: diacritics dropped, umlauts collapsed to base letter.
std::string normalize_strip(std::string_view s);

// ---------- Suffix / abbreviation helpers -------------------

// Try to expand a common German street abbreviation.
// Input is assumed already lowercase-ASCII.
// If the token matches an abbreviation (with or without a trailing dot)
// returns the expanded form; otherwise returns the input unchanged.
//
//   "str"   → "strasse"
//   "str."  → "strasse"
//   "pl."   → "platz"
//   "hbf."  → "hauptbahnhof"
//   "aalen" → "aalen"   (unchanged)
std::string expand_abbreviation(std::string_view token);

// If the token is a compound street name ending in a known suffix,
// split it into [prefix, suffix]. Otherwise returns [token].
// Input is assumed already lowercase-ASCII (after DIN normalization).
//
//   "bahnhofstrasse" → ["bahnhof", "strasse"]
//   "marienplatz"    → ["marien",  "platz"]
//   "aalen"          → ["aalen"]
//   "strasse"        → ["strasse"]   (no prefix)
std::vector<std::string> split_suffix(std::string_view token);

// ---------- High-level tokenizers ---------------------------
//
// Full pipeline: normalize → tokenize → expand → split suffix.
// Result is the list of canonical tokens the inverted index should
// see for both indexing and querying.
//
//   "Aalen, Hauptstraße 10"  → ["aalen", "haupt", "strasse", "10"]
//   "Str. der Republik"      → ["strasse", "der", "republik"]
std::vector<std::string> tokenize_din  (std::string_view s);
std::vector<std::string> tokenize_strip(std::string_view s);

// ---------- Self-test ---------------------------------------
// Runs the built-in test suite (see normalizer_test.cpp).
// Returns the number of failed cases (0 = all pass).
int run_normalizer_tests();

} // namespace geocoder

