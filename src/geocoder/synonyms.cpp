#include "synonyms.h"
#include "normalizer.h"

#include <unordered_map>

namespace geocoder {

// ============================================================
//  Static tables.  All keys and values are already in the DIN-
//  normalised form (lowercase ASCII, ß→ss, umlauts→ae/oe/ue).
//  Values that expand to multi-word German phrases are re-tokenised
//  at query time via tokenize_din, so writing them naturally is fine.
// ============================================================

// ---- Bigrams (2 English tokens → German phrase) -----------
static const std::unordered_map<std::string, std::string>& bigram_table() {
    static const std::unordered_map<std::string, std::string> tbl = {
        // Transport hubs
        {"main station",       "hauptbahnhof"},
        {"central station",    "hauptbahnhof"},
        {"main train station", "hauptbahnhof"},
        {"train station",      "bahnhof"},
        // Civic
        {"city hall",          "rathaus"},
        {"town hall",          "rathaus"},
        {"market square",      "markt"},
        {"market place",       "markt"},
        // Education / culture
        {"high school",        "gymnasium"},
        {"botanical garden",   "botanischer garten"},
        // Landmarks
        {"old town",           "altstadt"},
        {"new town",           "neustadt"},
    };
    return tbl;
}

// ---- Unigrams (1 English category word → German word) -----
static const std::unordered_map<std::string, std::string>& unigram_table() {
    static const std::unordered_map<std::string, std::string> tbl = {
        // Transport
        {"station",     "bahnhof"},
        {"airport",     "flughafen"},
        {"port",        "hafen"},
        {"harbour",     "hafen"},
        {"harbor",      "hafen"},
        // Buildings
        {"cathedral",   "dom"},
        {"castle",      "schloss"},
        {"palace",      "schloss"},
        {"fortress",    "burg"},
        {"church",      "kirche"},
        {"chapel",      "kapelle"},
        {"monastery",   "kloster"},
        // Civic
        {"square",      "platz"},
        {"plaza",       "platz"},
        {"market",      "markt"},
        {"hall",        "halle"},
        // Streets
        {"street",      "strasse"},
        {"road",        "strasse"},
        {"avenue",      "allee"},
        {"lane",        "gasse"},
        {"way",         "weg"},
        {"bridge",      "bruecke"},
        // Nature
        {"lake",        "see"},
        {"river",       "fluss"},
        {"mountain",    "berg"},
        {"hill",        "berg"},
        {"forest",      "wald"},
        {"garden",      "garten"},
        // Facilities
        {"university",  "universitaet"},
        {"hospital",    "krankenhaus"},
        {"school",      "schule"},
        {"library",     "bibliothek"},
        {"theatre",     "theater"},
        {"theater",     "theater"},
        {"stadium",     "stadion"},
    };
    return tbl;
}

// ---- Exonyms (English place name → German endonym) --------
// Only entries where the English form differs from the German form.
// (Berlin, Hamburg, Frankfurt etc. are identical in both languages so
//  don't need entries.)
static const std::unordered_map<std::string, std::string>& exonym_table() {
    static const std::unordered_map<std::string, std::string> tbl = {
        // Major cities
        {"munich",     "muenchen"},
        {"cologne",    "koeln"},
        {"nuremberg",  "nuernberg"},
        {"hanover",    "hannover"},
        {"brunswick",  "braunschweig"},
        {"aix-la-chapelle", "aachen"},   // rare but well-known
        // Federal states
        {"bavaria",    "bayern"},
        {"saxony",     "sachsen"},
        {"hesse",      "hessen"},
        {"thuringia",  "thueringen"},
        {"westphalia", "westfalen"},
        {"lower saxony","niedersachsen"},   // will be matched as bigram fallback
        // Historic regions
        {"franconia",  "franken"},
        {"swabia",     "schwaben"},
        {"palatinate", "pfalz"},
        {"rhineland",  "rheinland"},
        // Geographic
        {"black forest", "schwarzwald"},    // bigram — will be handled in bigrams
        {"rhine",      "rhein"},
        {"danube",     "donau"},
        {"moselle",    "mosel"},
        {"bodensee",   "bodensee"},         // same, but user might English it
        {"lake constance", "bodensee"},     // bigram fallback
        // Not including "main" (river) — it collides with English "main"
        // and the bigram "main station" → "hauptbahnhof" already handles
        // the common conflict case.
    };
    return tbl;
}

// Some entries above are logically bigrams; move them into the bigram
// table for consistent longest-match behaviour.  We keep them in the
// exonym table conceptually but expose them via a helper:
static const std::unordered_map<std::string, std::string>& extra_bigram_table() {
    static const std::unordered_map<std::string, std::string> tbl = {
        {"lower saxony",   "niedersachsen"},
        {"black forest",   "schwarzwald"},
        {"lake constance", "bodensee"},
    };
    return tbl;
}

// ============================================================
//  apply_synonyms — greedy longest match
// ============================================================

namespace {

// Look up a bigram (either in the main bigram table or the extras).
// Returns nullptr if no hit.
const std::string* bigram_lookup(const std::string& t1, const std::string& t2) {
    std::string key = t1 + " " + t2;
    const auto& bg = bigram_table();
    auto it = bg.find(key);
    if (it != bg.end()) return &it->second;
    const auto& ex = extra_bigram_table();
    it = ex.find(key);
    if (it != ex.end()) return &it->second;
    return nullptr;
}

// Look up a unigram (unigrams first, then exonyms).
const std::string* unigram_lookup(const std::string& t) {
    const auto& un = unigram_table();
    auto it = un.find(t);
    if (it != un.end()) return &it->second;
    const auto& ex = exonym_table();
    it = ex.find(t);
    if (it != ex.end()) return &it->second;
    return nullptr;
}

} // anonymous namespace

SynonymExpansion apply_synonyms(const std::vector<std::string>& tokens) {
    SynonymExpansion out;
    if (tokens.empty()) return out;

    std::vector<std::string> trace_parts;

    // -------- Pass 1: bigram substitution (longest match, left-to-right) ------
    std::vector<std::string> intermediate;
    intermediate.reserve(tokens.size());
    for (size_t i = 0; i < tokens.size(); ) {
        if (i + 1 < tokens.size()) {
            const std::string* rep = bigram_lookup(tokens[i], tokens[i+1]);
            if (rep) {
                // Target may be multi-word ("botanischer garten"); re-tokenise
                // to keep the pipeline consistent.
                for (auto& t : tokenize_din(*rep)) intermediate.push_back(std::move(t));
                trace_parts.push_back(
                    "'" + tokens[i] + " " + tokens[i+1] + "' → '" + *rep + "'");
                i += 2;
                continue;
            }
        }
        intermediate.push_back(tokens[i]);
        ++i;
    }

    // -------- Pass 2: unigram / exonym substitution --------------------------
    std::vector<std::string> final_tokens;
    final_tokens.reserve(intermediate.size());
    for (const auto& t : intermediate) {
        const std::string* rep = unigram_lookup(t);
        if (rep) {
            for (auto& sub : tokenize_din(*rep)) final_tokens.push_back(std::move(sub));
            trace_parts.push_back("'" + t + "' → '" + *rep + "'");
        } else {
            final_tokens.push_back(t);
        }
    }

    out.tokens  = std::move(final_tokens);
    out.changed = (out.tokens != tokens);
    for (size_t i = 0; i < trace_parts.size(); ++i) {
        if (i) out.trace += ", ";
        out.trace += trace_parts[i];
    }
    return out;
}

} // namespace geocoder
