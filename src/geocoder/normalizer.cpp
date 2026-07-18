#include "normalizer.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace geocoder {

// ============================================================
//  UTF-8 transliteration tables
//
//  We handle three UTF-8 byte-2 ranges directly:
//    0xC3 xx  — Latin-1 Supplement (contains the German umlauts)
//    0xC4 xx  — Latin Extended-A (part 1)   — some Slavic letters
//    0xC5 xx  — Latin Extended-A (part 2)   — more Slavic letters
//
//  Each table has 64 entries covering the second byte range
//  0x80..0xBF (the payload of a 2-byte UTF-8 codepoint).
//
//  Two variants are provided per range — DIN uses ae/oe/ue/ss for
//  the umlauts, STRIP just drops them to a/o/u/s.
// ============================================================

namespace {

// Helper: table entry is a short C-string (usually 1 char, sometimes 2 for
// umlauts under DIN). Empty string means "drop this codepoint".
using MapRow = const char* const [64];

// ----- 0xC3 (Latin-1 Supplement) -----
// Indexes 0..31  = uppercase Latin-1 (0xC3 0x80..0x9F: À..ß)
// Indexes 32..63 = lowercase Latin-1 (0xC3 0xA0..0xBF: à..ÿ)
static const char* const T_C3_DIN[64] = {
    // 0xC3 0x80..9F  =>  À Á Â Ã Ä Å Æ Ç  È É Ê Ë Ì Í Î Ï
    //                    Ð Ñ Ò Ó Ô Õ Ö ×  Ø Ù Ú Û Ü Ý Þ ß
    "a","a","a","a","ae","a","ae","c",  "e","e","e","e","i","i","i","i",
    "d","n","o","o","o","o","oe","",   "o","u","u","u","ue","y","th","ss",
    // 0xC3 0xA0..BF  =>  à á â ã ä å æ ç  è é ê ë ì í î ï
    //                    ð ñ ò ó ô õ ö ÷  ø ù ú û ü ý þ ÿ
    "a","a","a","a","ae","a","ae","c",  "e","e","e","e","i","i","i","i",
    "d","n","o","o","o","o","oe","",   "o","u","u","u","ue","y","th","y",
};

static const char* const T_C3_STRIP[64] = {
    "a","a","a","a","a","a","ae","c",  "e","e","e","e","i","i","i","i",
    "d","n","o","o","o","o","o","",    "o","u","u","u","u","y","th","s",
    "a","a","a","a","a","a","ae","c",  "e","e","e","e","i","i","i","i",
    "d","n","o","o","o","o","o","",    "o","u","u","u","u","y","th","y",
};

// ----- 0xC4 (Latin Extended-A, first half) -----
// Contains: Ā ā Ă ă Ą ą Ć ć Ĉ ĉ Ċ ċ Č č Ď ď Đ đ Ē ē Ĕ ĕ Ė ė Ę ę Ě ě
// (Slavic-heavy). We give a best-effort ASCII fold; both DIN and STRIP
// behave the same here — no umlaut-specific rule applies.
static const char* const T_C4[64] = {
    // 0xC4 0x80..8F  Ā ā Ă ă Ą ą Ć ć Ĉ ĉ Ċ ċ Č č Ď ď
    "a","a","a","a","a","a","c","c",  "c","c","c","c","c","c","d","d",
    // 0xC4 0x90..9F  Đ đ Ē ē Ĕ ĕ Ė ė  Ę ę Ě ě Ĝ ĝ Ğ ğ
    "d","d","e","e","e","e","e","e",  "e","e","e","e","g","g","g","g",
    // 0xC4 0xA0..AF  Ġ ġ Ģ ģ Ĥ ĥ Ħ ħ  Ĩ ĩ Ī ī Ĭ ĭ Į į
    "g","g","g","g","h","h","h","h",  "i","i","i","i","i","i","i","i",
    // 0xC4 0xB0..BF  İ ı Ĳ ĳ Ĵ ĵ Ķ ķ  ĸ Ĺ ĺ Ļ ļ Ľ ľ Ŀ
    "i","i","ij","ij","j","j","k","k",  "k","l","l","l","l","l","l","l",
};

// ----- 0xC5 (Latin Extended-A, second half) -----
// Continues: ŀ Ł ł Ń ń Ņ ņ Ň ň ŉ Ŋ ŋ Ō ō Ŏ ŏ Ő ő Œ œ Ŕ ŕ Ŗ ŗ Ř ř Ś ś Ŝ ŝ Ş ş Š š ...
static const char* const T_C5[64] = {
    // 0xC5 0x80..8F  ŀ Ł ł Ń ń Ņ ņ Ň  ň ŉ Ŋ ŋ Ō ō Ŏ ŏ
    "l","l","l","n","n","n","n","n",  "n","n","n","n","o","o","o","o",
    // 0xC5 0x90..9F  Ő ő Œ œ Ŕ ŕ Ŗ ŗ  Ř ř Ś ś Ŝ ŝ Ş ş
    "o","o","oe","oe","r","r","r","r",  "r","r","s","s","s","s","s","s",
    // 0xC5 0xA0..AF  Š š Ţ ţ Ť ť Ŧ ŧ  Ũ ũ Ū ū Ŭ ŭ Ů ů
    "s","s","t","t","t","t","t","t",  "u","u","u","u","u","u","u","u",
    // 0xC5 0xB0..BF  Ű ű Ų ų Ŵ ŵ Ŷ ŷ  Ÿ Ź ź Ż ż Ž ž ſ
    "u","u","u","u","w","w","y","y",  "y","z","z","z","z","z","z","s",
};

// ---------- Core scanning loop --------------------------------
// Iterates UTF-8, writes ASCII output.  variant=false → DIN, true → STRIP.
static std::string normalize_impl(std::string_view s, bool strip_variant) {
    std::string out;
    out.reserve(s.size());
    const unsigned char* p   = reinterpret_cast<const unsigned char*>(s.data());
    const unsigned char* end = p + s.size();

    while (p < end) {
        unsigned char c = *p;

        if (c < 0x80) {
            // Pure ASCII fast path
            if (c >= 'A' && c <= 'Z') out.push_back((char)(c + 32));
            else                       out.push_back((char)c);
            ++p;
            continue;
        }

        // Multi-byte UTF-8: we handle 0xC3 / 0xC4 / 0xC5 explicitly,
        // skip / passthrough anything else.
        if ((c == 0xC3 || c == 0xC4 || c == 0xC5) && (p + 1 < end)) {
            unsigned char b2 = p[1];
            if (b2 >= 0x80 && b2 <= 0xBF) {
                int idx = b2 - 0x80;
                const char* rep = nullptr;
                if (c == 0xC3) rep = strip_variant ? T_C3_STRIP[idx] : T_C3_DIN[idx];
                else if (c == 0xC4) rep = T_C4[idx];
                else                 rep = T_C5[idx];
                if (rep && *rep) out.append(rep);
                p += 2;
                continue;
            }
        }

        // Other multi-byte UTF-8: skip the codepoint safely.
        // 0xC0..DF = 2-byte, 0xE0..EF = 3-byte, 0xF0..F7 = 4-byte
        int skip = 1;
        if      ((c & 0xE0) == 0xC0) skip = 2;
        else if ((c & 0xF0) == 0xE0) skip = 3;
        else if ((c & 0xF8) == 0xF0) skip = 4;
        p += skip;
    }
    return out;
}

} // anonymous namespace

std::string normalize_din  (std::string_view s) { return normalize_impl(s, false); }
std::string normalize_strip(std::string_view s) { return normalize_impl(s, true ); }

// ============================================================
//  Abbreviation expansion
// ============================================================

// The lookup key is the abbreviation without a trailing dot, already
// lowercased and DIN-normalized. Small enough that a linear table with
// std::unordered_map lookup is fine.
static const std::unordered_map<std::string, std::string>& abbreviation_table() {
    static const std::unordered_map<std::string, std::string> tbl = {
        // Street type
        {"str",     "strasse"},
        {"strass",  "strasse"},
        {"pl",      "platz"},
        {"gass",    "gasse"},
        {"g",       "gasse"},        // Austrian usage
        // Railway / infrastructure
        {"hbf",     "hauptbahnhof"},
        {"bhf",     "bahnhof"},
        {"bf",      "bahnhof"},
        // Public buildings
        {"rathaus", "rathaus"},
        {"kirche",  "kirche"},
    };
    return tbl;
}

std::string expand_abbreviation(std::string_view token) {
    if (token.empty()) return {};

    // Strip a single trailing dot: "str." → "str"
    std::string key(token);
    if (!key.empty() && key.back() == '.') key.pop_back();

    const auto& tbl = abbreviation_table();
    auto it = tbl.find(key);
    return (it != tbl.end()) ? it->second : std::string(token);
}

// ============================================================
//  Suffix splitting
// ============================================================
//
// Longest-suffix match strategy. If the token ends with a known suffix
// AND the remaining prefix is at least MIN_PREFIX bytes, we split.
// This prevents mangling short standalone words that happen to end in
// a suffix character (e.g. "der weg" — we don't want to touch "der").
//
namespace {

constexpr size_t MIN_PREFIX = 3;

// Suffixes ordered longest first so we match the most specific rule.
static const std::vector<std::string>& suffix_list() {
    static const std::vector<std::string> lst = {
        // longest first
        "hauptbahnhof",
        "bahnhof",
        "bruecke",
        "strasse",
        "platz",
        "allee",
        "gasse",
        "steig",
        "damm",
        "berg",
        "ring",
        "ufer",
        "weg",
    };
    return lst;
}

} // anonymous namespace

// Short abbreviated suffixes.  These need a stricter prefix length
// requirement (≥ 4 chars) because they are only 2-3 chars long, so
// there is more risk of false matches on ordinary words.
namespace {
static const std::vector<std::pair<std::string, std::string>>& short_suffix_list() {
    static const std::vector<std::pair<std::string, std::string>> lst = {
        // suffix,  canonical expansion
        { "str",   "strasse" },   // "hauptstr"  → ["haupt", "strasse"]
        { "pl",    "platz"   },   // "marienpl"  → ["marien", "platz"  ]
    };
    return lst;
}
constexpr size_t SHORT_MIN_PREFIX = 4;
} // anonymous namespace

std::vector<std::string> split_suffix(std::string_view token) {
    // Long, already-canonical suffixes (strasse, platz, bahnhof, ...)
    for (const auto& sfx : suffix_list()) {
        if (token.size() >= sfx.size() + MIN_PREFIX &&
            token.compare(token.size() - sfx.size(), sfx.size(), sfx) == 0) {
            std::string prefix(token.substr(0, token.size() - sfx.size()));
            return { std::move(prefix), sfx };
        }
    }
    // Short abbreviated suffixes (str, pl) — expand to canonical form.
    for (const auto& [sfx, canonical] : short_suffix_list()) {
        if (token.size() >= sfx.size() + SHORT_MIN_PREFIX &&
            token.compare(token.size() - sfx.size(), sfx.size(), sfx) == 0) {
            std::string prefix(token.substr(0, token.size() - sfx.size()));
            return { std::move(prefix), canonical };
        }
    }
    return { std::string(token) };
}

// ============================================================
//  Tokenization
// ============================================================
//
// Steps:
//   1. run the requested normalizer over the whole string
//   2. scan byte-by-byte, splitting on any non-alphanumeric
//   3. for each raw token: expand abbreviation, then split suffix,
//      append the resulting 1-or-2 tokens to the output
//
namespace {

static bool is_word_byte(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

// Common tokenizer body — normalizer_fn is either normalize_din or _strip.
using NormFn = std::string(*)(std::string_view);

static std::vector<std::string> tokenize_impl(std::string_view s, NormFn nfn) {
    std::string norm = nfn(s);
    std::vector<std::string> out;
    out.reserve(8);

    size_t i = 0;
    const size_t n = norm.size();
    while (i < n) {
        // Skip separators
        while (i < n && !is_word_byte((unsigned char)norm[i])
                     && norm[i] != '.') ++i;
        if (i >= n) break;

        // Collect one token (word bytes only; consume the trailing dot as
        // a marker but drop it before further processing).
        size_t start = i;
        while (i < n && is_word_byte((unsigned char)norm[i])) ++i;
        bool had_trailing_dot = (i < n && norm[i] == '.');
        if (had_trailing_dot) ++i;                        // skip the dot
        std::string raw(norm.data() + start, i - start - (had_trailing_dot ? 1 : 0));
        if (raw.empty()) continue;

        // Expand common abbreviations (str → strasse, hbf → hauptbahnhof).
        // The abbreviation table is keyed by the dot-less form, so this
        // works whether or not the user wrote the dot.
        std::string expanded = expand_abbreviation(raw);

        // Split compound suffix (bahnhofstrasse → [bahnhof, strasse],
        // marienpl → [marien, platz]).
        auto parts = split_suffix(expanded);
        for (auto& p : parts) {
            if (!p.empty()) out.push_back(std::move(p));
        }
    }
    return out;
}

} // anonymous namespace

std::vector<std::string> tokenize_din  (std::string_view s) { return tokenize_impl(s, &normalize_din);   }
std::vector<std::string> tokenize_strip(std::string_view s) { return tokenize_impl(s, &normalize_strip); }

} // namespace geocoder
