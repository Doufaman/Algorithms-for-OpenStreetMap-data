#include "ranker.h"
#include "normalizer.h"
#include "config.h"
#include "server/api_handler.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace geocoder {

// Default weights — see README "Geocoder Heuristics" for the reasoning.
// Overridable via config.h if the user wants to experiment.
namespace {
    constexpr double W_MATCH   = 10.0;   // how many tokens matched
    constexpr double W_SPEC    = 3.0;    // house > street > POI > admin
    constexpr double W_ADMIN   =  8.0;   // admin chain agreement
    constexpr double W_EXACT   = 5.0;    // exact whole-name match

    // Specificity bonus per object kind (normalised to 0..1 later).
    // Bigger = more specific = ranks higher.
    double spec_bonus(ObjectKind k, int admin_level) {
        switch (k) {
            case ObjectKind::Street: return 0.7;
            case ObjectKind::POI:    return 0.6;
            case ObjectKind::Admin:
                // Level 8 (city) is more useful than level 4 (state) or 2 (country).
                if (admin_level >= 8)  return 0.5;
                if (admin_level >= 4)  return 0.3;
                return 0.2;
        }
        return 0.0;
    }
}

// ------------------------------------------------------------
//  Helpers
// ------------------------------------------------------------

std::string Ranker::object_name(const ObjectRef& ref, const RankContext& ctx) const {
    switch (ref.kind) {
        case ObjectKind::POI:
            if (ctx.points && ref.id < ctx.points->size())
                return (*ctx.points)[ref.id].name;
            break;
        case ObjectKind::Street:
            if (ctx.lines && ref.id < ctx.lines->size())
                return (*ctx.lines)[ref.id].name;
            break;
        case ObjectKind::Admin:
            if (ctx.admins && ref.id < ctx.admins->size())
                return (*ctx.admins)[ref.id].name;
            break;
    }
    return {};
}

std::string Ranker::object_admin_blob(const ObjectRef& ref, const RankContext& ctx) const {
    // Assemble "country state city suburb" (all lowercase, DIN-normalised)
    // for containment checks.
    std::string parts;
    auto append = [&](const std::string& s) {
        if (s.empty()) return;
        if (!parts.empty()) parts += ' ';
        parts += normalize_din(s);
    };

    switch (ref.kind) {
        case ObjectKind::POI:
            if (ctx.points && ref.id < ctx.points->size()) {
                const auto& p = (*ctx.points)[ref.id];
                append(p.country); append(p.state);
                append(p.city);    append(p.suburb);
            }
            break;
        case ObjectKind::Street:
            // Streets carry no admin chain in our records — a Task 3+
            // improvement would attach a chain during line ingest.
            // For now streets score lower on admin consistency.
            break;
        case ObjectKind::Admin:
            // Admins are their own admin — score consistency by name.
            if (ctx.admins && ref.id < ctx.admins->size())
                append((*ctx.admins)[ref.id].name);
            break;
    }
    return parts;
}

// ------------------------------------------------------------
//  Score
// ------------------------------------------------------------
//
// Every axis returns a value in [0,1]; final score is the weighted sum,
// which we normalise so the max is 100 for easy display / debug.
//
Scored Ranker::score(const ObjectRef& ref, const RankContext& ctx) const {
    Scored s{ ref, 0.0, 0, {} };

    // Full pool of query tokens (union of object + admin buckets)
    std::vector<std::string> q = ctx.object_tokens;
    q.insert(q.end(), ctx.admin_tokens.begin(), ctx.admin_tokens.end());
    if (q.empty()) return s;

    // Precompute the object's own token list (from its display name).
    std::string name = object_name(ref, ctx);
    auto obj_tokens = tokenize_din(name);
    std::unordered_set<std::string> obj_set(obj_tokens.begin(), obj_tokens.end());
    std::string admin_blob = object_admin_blob(ref, ctx);

    // ── Factor A: match_quality ─────────────────────────
    // Count query tokens that appear as tokens of the object's own name
    // or anywhere in its admin blob. Both count as "matched".
    int matched = 0;
    for (const auto& t : q) {
        if (obj_set.count(t)) { ++matched; continue; }
        if (!admin_blob.empty() &&
            admin_blob.find(t) != std::string::npos) { ++matched; continue; }
    }
    s.matched_tokens = matched;
    double match_quality = (double)matched / (double)q.size();

    // ── Factor B: specificity ──────────────────────────
    int alvl = 0;
    if (ref.kind == ObjectKind::Admin && ctx.admins && ref.id < ctx.admins->size())
        alvl = (*ctx.admins)[ref.id].admin_level;
    double spec = spec_bonus(ref.kind, alvl);

    // ── Factor C: admin consistency ────────────────────
    // If the user typed admin tokens, do they show up in this object's chain?
    double admin_consistency = 0.0;
    if (!ctx.admin_tokens.empty()) {
        int adm_hits = 0;
        for (const auto& t : ctx.admin_tokens)
            if (!admin_blob.empty() && admin_blob.find(t) != std::string::npos)
                ++adm_hits;
        admin_consistency = (double)adm_hits / (double)ctx.admin_tokens.size();
    } else {
        // No admin tokens ⇒ neutral (don't penalise or reward)
        admin_consistency = 0.5;
    }

    // ── Factor D: exactness ────────────────────────────
    // Bonus if the object's full normalised name equals the concatenation
    // of the object bucket, OR if the whole query bucket matches the name
    // token-for-token.
    double exactness = 0.0;
    if (!ctx.object_tokens.empty() && !obj_tokens.empty()) {
        if (ctx.object_tokens.size() == obj_tokens.size() &&
            std::equal(ctx.object_tokens.begin(), ctx.object_tokens.end(),
                       obj_tokens.begin())) {
            exactness = 1.0;                    // full match
        } else if (matched == (int)ctx.object_tokens.size()) {
            exactness = 0.5;                    // substring match
        }
    }

    // ── Weighted sum, normalised to 0..100 ──────────────
    double raw = W_MATCH * match_quality
               + W_SPEC  * spec
               + W_ADMIN * admin_consistency
               + W_EXACT * exactness;
    double max = W_MATCH + W_SPEC + W_ADMIN + W_EXACT;
    s.score = 100.0 * raw / max;

    // Short human-readable reason (debug + frontend display)
    std::ostringstream why;
    why << "match " << matched << "/" << q.size();
    if (admin_consistency > 0.5) why << ", admin ok";
    if (exactness >= 1.0)        why << ", exact";
    else if (exactness > 0.0)    why << ", substr";
    s.reason = why.str();
    return s;
}

} // namespace geocoder
