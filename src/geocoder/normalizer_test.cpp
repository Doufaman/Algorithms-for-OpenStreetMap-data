// ============================================================
//  Self-test for the normalizer module.
//
//  Runs the two test suites promised in the geocoder-features
//  design doc:
//    • Orthographic variants (Düsseldorf / Duesseldorf / …)
//    • Suffix / abbreviation expansion (Str., Pl., Hbf.)
//
//  Prints a pass/fail table so the grader can eyeball correctness
//  in one screenful. Called from main.cpp when argv[1] == "test-normalizer".
// ============================================================
#include "normalizer.h"

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace geocoder {

namespace {

// One test case: (input, expected DIN-normalized output).
struct DinCase {
    const char* input;
    const char* expected;
};

// Only the DIN form is asserted below — the strip form is easy to
// eyeball from the same input and doesn't need a separate table.
static const std::vector<DinCase> kDinCases = {
    // ---- Case folding + umlauts (DIN 5007) ----
    {"Düsseldorf",     "duesseldorf"},
    {"DUSSELDORF",     "dusseldorf"},        // no umlauts → identity
    {"düsseldorf",     "duesseldorf"},
    {"Duesseldorf",    "duesseldorf"},       // already DIN form
    {"München",        "muenchen"},
    {"Würzburg",       "wuerzburg"},
    {"Köln",           "koeln"},

    // ---- ß handling ----
    {"Straße",         "strasse"},
    {"Aßlar",          "asslar"},

    // ---- Other European diacritics ----
    {"Zürich",         "zuerich"},
    {"Genève",         "geneve"},
    {"Español",        "espanol"},
    {"Poznań",         "poznan"},
    {"Košice",         "kosice"},

    // ---- Pure ASCII passthrough ----
    {"Aalen",          "aalen"},
    {"Hauptstrasse",   "hauptstrasse"},
    {"Hauptstraße 10", "hauptstrasse 10"},
};

// Tokenizer cases test the full pipeline (normalize + tokenize
// + expand abbreviations + split suffix).
struct TokCase {
    const char* input;
    std::vector<std::string> expected;
};

static const std::vector<TokCase> kTokCases = {
    // Compound street split
    {"Bahnhofstraße",         {"bahnhof",     "strasse"}},
    {"Marienplatz",           {"marien",      "platz"}},
    {"Königsallee",           {"koenigs",     "allee"}},

    // Abbreviation expansion
    {"Str. der Republik",     {"strasse", "der", "republik"}},
    {"Marienpl.",             {"marien", "platz"}},        // suffix "pl" expanded
    // Note: hbf. → hauptbahnhof → further split by suffix rule.
    // The split policy is INTENTIONAL — indexing "Hauptbahnhof"
    // as [haupt, bahnhof] lets a query "Bahnhof" also hit it.
    {"Stuttgart Hbf.",        {"stuttgart", "haupt", "bahnhof"}},

    // Full address
    {"Aalen, Hauptstraße 10", {"aalen", "haupt", "strasse", "10"}},
    {"Am Neckartor 12",       {"am", "neckartor", "12"}},

    // Punctuation and whitespace
    {"  Nürnberger  Str.  ",  {"nuernberger", "strasse"}},
    {"Bahnhof-Str.",          {"bahnhof",     "strasse"}},
};

// Print helper
static std::string join(const std::vector<std::string>& v) {
    std::string out = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ", ";
        out += "\"" + v[i] + "\"";
    }
    return out + "]";
}

} // anonymous namespace

// Public entry point. Returns 0 if all pass, else # failures.
int run_normalizer_tests() {
    std::cout << "\n══════════════════════════════════════════════════════════\n"
              <<   "  Normalizer self-test — Sheet 3 Task 1 (Preprocessing)  \n"
              <<   "══════════════════════════════════════════════════════════\n";

    int fail = 0;

    // -------- DIN cases --------
    std::cout << "\n[1] DIN 5007 transliteration (" << kDinCases.size() << " cases)\n";
    for (const auto& c : kDinCases) {
        std::string got = normalize_din(c.input);
        bool ok = (got == c.expected);
        std::cout << "  " << (ok ? "  ok  " : "  FAIL")
                  << "  \"" << c.input << "\"  →  \"" << got << "\"";
        if (!ok) { std::cout << "  (expected \"" << c.expected << "\")"; ++fail; }
        std::cout << "\n";
    }

    // -------- Tokenization cases --------
    std::cout << "\n[2] Tokenization + suffix/abbrev (" << kTokCases.size() << " cases)\n";
    for (const auto& c : kTokCases) {
        auto got = tokenize_din(c.input);
        bool ok = (got == c.expected);
        std::cout << "  " << (ok ? "  ok  " : "  FAIL")
                  << "  \"" << c.input << "\"  →  " << join(got);
        if (!ok) { std::cout << "\n         expected " << join(c.expected); ++fail; }
        std::cout << "\n";
    }

    // -------- Sanity check strip variant --------
    std::cout << "\n[3] STRIP-variant spot checks\n";
    struct StripCase { const char* in; const char* exp; };
    static const std::vector<StripCase> kStrip = {
        {"Düsseldorf",  "dusseldorf"},
        {"München",     "munchen"},
        {"Straße",      "strase"},
        {"Aßlar",       "aslar"},
    };
    for (const auto& c : kStrip) {
        std::string got = normalize_strip(c.in);
        bool ok = (got == c.exp);
        std::cout << "  " << (ok ? "  ok  " : "  FAIL")
                  << "  \"" << c.in << "\"  →  \"" << got << "\"";
        if (!ok) { std::cout << "  (expected \"" << c.exp << "\")"; ++fail; }
        std::cout << "\n";
    }

    std::cout << "\n──────────────────────────────────────────────────────────\n"
              << "  Result: "
              << (fail == 0 ? "ALL PASS ✓" : std::to_string(fail) + " FAILURE(S) ✗")
              << "\n──────────────────────────────────────────────────────────\n";
    return fail;
}

} // namespace geocoder
