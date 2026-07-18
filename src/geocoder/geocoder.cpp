#include "geocoder.h"
#include "normalizer.h"
#include "synonyms.h"
#include "server/api_handler.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace geocoder {

// ------------------------------------------------------------
//  Numeric extraction
// ------------------------------------------------------------
//
// A token is a number iff every byte is an ASCII digit. We treat
// short numbers (1..4 digits) as housenumber candidates and longer
// ones (5 digits) as postcodes.  Real housenumbers can go up to 4-5
// digits (Zurich has 4-digit ones); postcode / housenumber ambiguity
// at 5 digits is handled downstream by trying both.
//
void Geocoder::split_numeric(const std::vector<std::string>& in,
                             std::vector<std::string>& words,
                             std::vector<std::string>& nums) {
    for (const auto& t : in) {
        if (t.empty()) continue;
        bool all_digits = std::all_of(t.begin(), t.end(),
                              [](unsigned char c){ return std::isdigit(c); });
        if (all_digits) nums.push_back(t);
        else            words.push_back(t);
    }
}

// ------------------------------------------------------------
//  Score one (object, admin) split
// ------------------------------------------------------------

void Geocoder::score_split(const std::vector<std::string>& obj_tokens,
                           const std::vector<std::string>& adm_tokens,
                           std::vector<Scored>&            out_scored) const {
    // Combined AND intersection over ALL non-empty tokens — Task 2
    // primitive still does the heavy lifting; the split just changes
    // how we score.
    std::vector<std::string> all = obj_tokens;
    all.insert(all.end(), adm_tokens.begin(), adm_tokens.end());
    if (all.empty()) return;

    auto hits = index_.query_all(all);
    if (hits.empty()) return;

    RankContext ctx;
    ctx.points        = &points_;
    ctx.lines         = &lines_;
    ctx.admins        = &admins_;
    ctx.object_tokens = obj_tokens;
    ctx.admin_tokens  = adm_tokens;

    out_scored.reserve(out_scored.size() + hits.size());
    for (const auto& h : hits) {
        out_scored.push_back(ranker_.score(h, ctx));
    }
}

// ------------------------------------------------------------
//  Housenumber refinement
// ------------------------------------------------------------
//
// Walks the top-ranked results looking for a street.  For the first
// street found, uses PointGridIndex (built over db.points) to scan
// point candidates near the street's bbox, then keeps the one whose
// street tag matches the street name AND whose housenumber equals
// the target.
//
void Geocoder::refine_housenumber(std::vector<Scored>&            results,
                                  const std::string&              housenumber,
                                  const std::vector<std::string>& adm_tokens) const {
    if (housenumber.empty() || results.empty()) return;

    // Find first street candidate
    int street_slot = -1;
    for (int i = 0; i < (int)results.size(); ++i) {
        if (results[i].ref.kind == ObjectKind::Street) { street_slot = i; break; }
    }
    if (street_slot < 0) return;
    const Scored& s = results[street_slot];
    if (s.ref.id >= lines_.size()) return;
    const LineRecord& street = lines_[s.ref.id];
    if (street.name.empty()) return;

    // We use the street's normalized name to compare against p.street
    std::string street_norm = normalize_din(street.name);

    // Concatenate admin tokens into one blob for a coarse "in Aalen" check.
    std::string adm_blob;
    for (const auto& t : adm_tokens) {
        if (!adm_blob.empty()) adm_blob += ' ';
        adm_blob += t;
    }

    // Iterate all points (small overhead — houses without addresses are
    // skipped instantly).  If PointGridIndex could take a bbox we would
    // limit to street.bbox, but a bbox API is a small future addition.
    uint32_t best_id     = UINT32_MAX;
    int      best_score  = -1;
    for (uint32_t i = 0; i < points_.size(); ++i) {
        const PointRecord& p = points_[i];
        if (p.housenumber != housenumber) continue;
        if (p.street.empty()) continue;
        // Fast filter: bbox of the street
        if (p.lat < street.min_lat || p.lat > street.max_lat) continue;
        if (p.lon < street.min_lon || p.lon > street.max_lon) continue;
        // Match street name (normalised)
        if (normalize_din(p.street) != street_norm) continue;

        // Coarse admin match — bonus if the house's city / suburb
        // contains one of the admin tokens.
        int score = 0;
        if (!adm_blob.empty()) {
            std::string chain = normalize_din(p.city) + " " +
                                normalize_din(p.state);
            if (chain.find(adm_blob) != std::string::npos) score += 2;
            else                                          score += 0;
        } else score += 1;
        if (score > best_score) { best_score = score; best_id = i; }
    }

    if (best_id == UINT32_MAX) return;

    // Replace the street result with the specific house.
    Scored house = s;
    house.ref    = { ObjectKind::POI, best_id };
    house.reason = "street match + housenumber " + housenumber;
    // Boost the score above the street it came from
    house.score  = std::min(100.0, s.score + 15.0);
    results[street_slot] = house;
}

// ------------------------------------------------------------
//  search — the public entry
// ------------------------------------------------------------

std::vector<Scored> Geocoder::search(const std::string& q, int limit) const {
    if (limit <= 0) limit = 10;

    // 1. Tokenize with the DIN normalizer (Task 1)
    auto raw_tokens = tokenize_din(q);
    if (raw_tokens.empty()) return {};

    // 2. Extract numeric tokens (housenumber / postcode candidates)
    std::vector<std::string> words, nums;
    split_numeric(raw_tokens, words, nums);

    // 3. Enumerate contiguous splits of the word tokens into
    //    (object_tokens, admin_tokens).  For N tokens we try N+1 splits:
    //      split=0  -> obj=[], adm=[all]     (query is admin only)
    //      split=N  -> obj=[all], adm=[]     (query is object only)
    //      others   -> proper (obj, adm) pairs
    std::vector<Scored> all_scored;
    all_scored.reserve(64);

    if (words.empty()) {
        // Only numbers were typed.  Not useful without a name; bail out.
        return {};
    }

    // Helper: enumerate every contiguous split of `w` and score each one,
    // appending to all_scored.  Returns the first index this call added
    // so callers can tag those entries with an origin note.
    auto run_all_splits = [&](const std::vector<std::string>& w) -> size_t {
        size_t before = all_scored.size();
        for (size_t split = 0; split <= w.size(); ++split) {
            std::vector<std::string> obj(w.begin(), w.begin() + split);
            std::vector<std::string> adm(w.begin() + split, w.end());
            score_split(obj, adm, all_scored);
        }
        return before;
    };

    // ── Path A: original tokens ────────────────────────────
    run_all_splits(words);

    // ── Path B: DE↔EN synonym expansion (Optional task) ────
    //
    // If any exonym/synonym rule matched, run the whole split enumeration
    // again with the translated tokens.  Tag every new hit with the trace
    // so the frontend can surface "via 'main station' → 'hauptbahnhof'".
    SynonymExpansion se = apply_synonyms(words);
    if (se.changed && !se.tokens.empty() && se.tokens != words) {
        size_t path_b_start = run_all_splits(se.tokens);
        std::string tag = "via " + se.trace;
        for (size_t i = path_b_start; i < all_scored.size(); ++i) {
            if (!all_scored[i].reason.empty()) all_scored[i].reason += "  ·  ";
            all_scored[i].reason += tag;
        }
    }

    // 4. Aggregate: keep the highest score per unique ObjectRef.
    std::unordered_map<uint64_t, Scored> best;
    best.reserve(all_scored.size());
    for (const auto& sc : all_scored) {
        uint64_t key = ((uint64_t)sc.ref.id << 8) | (uint64_t)sc.ref.kind;
        auto it = best.find(key);
        if (it == best.end() || sc.score > it->second.score) best[key] = sc;
    }
    std::vector<Scored> out;
    out.reserve(best.size());
    for (auto& [_, v] : best) out.push_back(std::move(v));

    // 5. Sort by score, descending.
    std::sort(out.begin(), out.end(),
              [](const Scored& a, const Scored& b){ return a.score > b.score; });

    // ── Path C: fuzzy fallback (Optional task) ─────────────
    //
    // Only fire when the main + synonym pipeline returned nothing
    // useful.  For each original query word we ask the BK-tree for
    // its closest dictionary neighbour within an adaptive edit
    // distance.  If any word was corrected we rerun the full split
    // enumeration once with the corrected list and merge.
    constexpr double kFuzzyTrigger = 30.0;
    bool need_fuzzy = out.empty() || out.front().score < kFuzzyTrigger;
    if (need_fuzzy && !index_.bktree().empty()) {
        std::vector<std::string> fuzzy_words;
        fuzzy_words.reserve(words.size());
        std::vector<std::string> corr_notes;
        bool any_correction = false;

        for (const auto& t : words) {
            int max_d = fuzzy_threshold(t.size());
            std::string best_word = index_.bktree().best_match(t, max_d);
            if (!best_word.empty() && best_word != t) {
                fuzzy_words.push_back(best_word);
                corr_notes.push_back("'" + t + "' → '" + best_word + "'");
                any_correction = true;
            } else {
                fuzzy_words.push_back(t);
            }
        }

        if (any_correction) {
            size_t path_c_start = run_all_splits(fuzzy_words);
            std::string tag = "fuzzy: ";
            for (size_t i = 0; i < corr_notes.size(); ++i) {
                if (i) tag += ", ";
                tag += corr_notes[i];
            }
            for (size_t i = path_c_start; i < all_scored.size(); ++i) {
                if (!all_scored[i].reason.empty()) all_scored[i].reason += "  ·  ";
                all_scored[i].reason += tag;
            }

            // Re-aggregate (fresh copy from all_scored so Path C hits appear)
            std::unordered_map<uint64_t, Scored> best2;
            best2.reserve(all_scored.size());
            for (const auto& sc : all_scored) {
                uint64_t key = ((uint64_t)sc.ref.id << 8) | (uint64_t)sc.ref.kind;
                auto it = best2.find(key);
                if (it == best2.end() || sc.score > it->second.score) best2[key] = sc;
            }
            out.clear();
            out.reserve(best2.size());
            for (auto& [_, v] : best2) out.push_back(std::move(v));
            std::sort(out.begin(), out.end(),
                      [](const Scored& a, const Scored& b){ return a.score > b.score; });
        }
    }

    // 6. Housenumber refinement (uses the first housenumber candidate).
    if (!nums.empty() && !out.empty()) {
        refine_housenumber(out, nums[0], words);
    }

    if ((int)out.size() > limit) out.resize(limit);
    return out;
}

} // namespace geocoder
