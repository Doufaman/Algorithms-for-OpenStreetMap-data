# Geocoder Heuristics — Ranking & Query Interpretation

Sheet 3 mandatory Task 3 requires that we (a) explain how multiple
search results are ordered and (b) explain how natural-language input
is interpreted. This document is that explanation.

---

## 1. Query pipeline overview

```
free-form query string
        │
        ▼
[Task 1] normalize + tokenize        DIN 5007 + strip variants
        │
        ▼
extract numeric tokens               housenumber / postcode candidates
        │
        ▼
[Task 3] enumerate contiguous
         splits of the word tokens   (object_tokens, admin_tokens)
        │
        ▼
[Task 2] AND-intersect over the      inverted-index lookup per split
         inverted index
        │
        ▼
[Task 3] weighted scoring            score = f(match, spec, admin, exact)
        │
        ▼
aggregate best score per object      dedup across splits
        │
        ▼
[Task 3] optional housenumber        street → nearby house on that
         refinement                  street with matching number
        │
        ▼
sort by score, cut to limit
```

---

## 2. Ranking

Every candidate is scored by a weighted sum of four independent
factors, each first normalised to `[0, 1]`. Weights are exposed in
[`src/geocoder/ranker.cpp`](../src/geocoder/ranker.cpp) so a grader
can inspect and re-tune them without recompiling too much:

```
score = 10·match_quality
      +  3·specificity
      +  8·admin_consistency
      +  5·exactness
```

Final score is scaled to `0..100` for display.

### Factors

| Factor | Range | Meaning |
|---|---|---|
| **match_quality** | 0..1 | fraction of query tokens that appear either as tokens of the object's own name OR anywhere in its admin chain |
| **specificity** | 0..1 | how "specific" the object kind is — Street 0.7, POI 0.6, Admin city (L8) 0.5, state (L4) 0.3, country (L2) 0.2 |
| **admin_consistency** | 0..1 | fraction of user's admin tokens that appear in the object's `country/state/city/suburb` chain (neutral 0.5 if user provided no admin tokens) |
| **exactness** | 0..1 | 1.0 if the object's whole normalized name equals the object bucket token-by-token; 0.5 if all object tokens appear but with extras; 0 otherwise |

### Why these weights?

* **match_quality** is the highest because a result that missed half
  the query tokens is almost never what the user wanted.
* **admin_consistency** is second because it decides between the
  many "Hauptstraße" candidates spread across every German town.
* **exactness** helps prefer `"Stuttgart"` (whole name) over
  `"Stuttgart-Mitte"` when the user typed just `Stuttgart`.
* **specificity** is a mild tiebreaker — user-visible bias toward
  clickable objects when everything else is equal.

---

## 3. Natural-language interpretation

### 3.1 Numeric tokens

Every token that is purely digits is treated as a **structured**
piece of the query rather than a word:

| Token | Interpretation |
|---|---|
| 1-4 digits | **housenumber** candidate (`addr:housenumber`) |
| 5 digits | **postcode** candidate first, housenumber fallback |
| 6+ digits | ignored (probably telephone or noise) |

The numeric token is *removed* from the token list that goes to the
inverted-index intersection — the index only stores string tokens.
Housenumbers are handled by a separate refinement step (§3.3).

### 3.2 Split enumeration

The tokenized word list is split into two contiguous buckets:

- `object_tokens` — what we guess names the target (street / POI)
- `admin_tokens`  — what we guess names the surrounding admin

For a query with N word tokens we try **all N + 1 splits**:

```
words = ["aalen", "haupt", "strasse"]
split=0  →  obj=[]                      adm=["aalen","haupt","strasse"]
split=1  →  obj=["aalen"]               adm=["haupt","strasse"]
split=2  →  obj=["aalen","haupt"]       adm=["strasse"]
split=3  →  obj=["aalen","haupt","strasse"] adm=[]
```

For each split we run the AND intersection over ALL word tokens
(that gate is the same regardless of split), then score every hit
under that split's `(obj, adm)` context. The **admin_consistency**
factor is what makes one split win over another.

The same object typically appears under multiple splits — we keep
its **highest** score across all splits (§ aggregation).

We considered non-contiguous splits (subset enumeration) but for
N tokens that's O(2^N); contiguous splits are O(N) with essentially
the same real-world coverage because users type left-to-right and
tend to say "OBJECT then ADMIN" or "ADMIN then OBJECT".

### 3.3 Housenumber refinement

If the query contained a housenumber candidate AND the top-scored
Street result exists, we replace it with the specific building:

1. Take the street's bbox from its `LineRecord`.
2. Scan `db.points` with that bbox filter, looking for points whose
   `addr:street` (normalised) equals the street's normalised name
   AND whose `addr:housenumber` equals the candidate.
3. If the admin_tokens contain the house's city name, small tiebreak
   bonus.
4. Emit the resulting house with a `+15` score boost so it out-ranks
   the street it came from.

This is what makes `"Aalen Hauptstrasse 10"` return an actual
building rather than a street object with 10 as an extra token.

### 3.4 Language variants (Task 1 recap)

The tokenizer feeds the ranker with tokens already normalised via
DIN 5007 (`ä→ae`, `ß→ss` etc.) AND a "strip" variant (`ä→a`,
`ß→s`). The inverted index is built dual-form, so a query in any
of these spellings

```
Düsseldorf   Duesseldorf   Dusseldorf   DUSSELDORF
Straße       Strasse       Str.         str
```

reaches the same index entries with zero query-time cost.

---

## 4. Worked examples

### 4.1 `"Aalen Hauptstrasse 10"`

```
tokenize → ["aalen", "haupt", "strasse", "10"]
extract  → words=["aalen","haupt","strasse"], nums=["10"]

split enumeration:
  ┌─ obj=[]                     adm=["aalen","haupt","strasse"] → weak
  ├─ obj=["aalen"]              adm=["haupt","strasse"]        → weak
  ├─ obj=["aalen","haupt"]      adm=["strasse"]                → weak
  └─ obj=["aalen","haupt","strasse"] adm=[]                    → weak

Only inverted-index AND hits: streets named "Hauptstraße" in various cities.
Best-scoring one is in Aalen because Street "Hauptstraße" in Aalen has
its admin chain (Aalen · Baden-Württemberg) match "aalen" from user query.

Housenumber refinement: replace street with house on Hauptstraße whose
housenumber == "10".

Final result: [ house #12345 (Hauptstraße 10, Aalen), score 91 ]
```

### 4.2 `"Oberer Grundweg Vaihingen"`

The ambiguous case mentioned in the assignment.

```
tokenize → ["oberer", "grundweg", "vaihingen"]

split enumeration:
  ┌─ split=0  adm=["oberer","grundweg","vaihingen"]  → 0 admin polygons match
  ├─ split=1  obj=["oberer"] adm=["grundweg","vaihingen"]  → weak, "oberer" isn't a name
  ├─ split=2  obj=["oberer","grundweg"] adm=["vaihingen"]  → STRONG:
  │                street "Oberer Grundweg" in Vaihingen exists AND its
  │                admin_chain contains "vaihingen" → admin_consistency = 1.0
  └─ split=3  obj=[all] adm=[]                              → medium

Winning interpretation: obj=Oberer Grundweg, adm=Vaihingen
```

### 4.3 `"Stuttgart"` (single-token query)

```
tokenize → ["stuttgart"]

split enumeration:
  ┌─ obj=[]           adm=["stuttgart"]  → hits all admin polygons/streets/POIs
  │                                        with "stuttgart" in their chain
  └─ obj=["stuttgart"] adm=[]            → hits objects whose OWN name is Stuttgart

Both splits produce hits; scoring compares them:
  Admin polygon "Stuttgart" (L8) itself:
    match_quality = 1/1, admin_consistency = 1.0, exactness = 1.0, spec = 0.5
    score ≈ (10 + 8 + 5 + 1.5) / 26 · 100 = 94
  Street "Stuttgarter Straße" in some other city:
    match_quality = 1/1, admin_consistency = 0.0, exactness = 0.5, spec = 0.7
    score ≈ (10 + 0 + 2.5 + 2.1) / 26 · 100 = 56

Result order: Stuttgart admin polygon first, then related streets/POIs.
```

---

## 5. Known limits (and where they'd be lifted)

* **Streets carry no admin chain.**  We attach country/state/city
  to `db.points` but not to `db.lines`. A `Line`-side attach in a
  follow-up would let the ranker treat street→city consistency the
  same as house→city.
* **Non-contiguous splits.**  If a user really writes
  `"Hauptstraße in the middle of Aalen"`, the "in the middle of"
  connectors interleave object and admin words.  A stop-word list
  (already sketched in `geocoder_features_design.md` Feature 2b)
  would fix this.
* **Language.**  Task 1 normaliser handles German + a good chunk of
  Latin-1/Extended-A diacritics; Slavic multi-byte characters
  outside that range fall back to identity.
* **Fuzzy typos.**  Not attempted here — that is Sheet 3 Optional
  Task 3.  The dual-index (`DIN` + `strip`) already covers the vast
  majority of "wrong but reasonable" spellings without needing
  edit-distance search.

---

## 6. Where in the code

| Piece | File |
|---|---|
| DIN + strip normalizer, suffix / abbreviation | [`src/geocoder/normalizer.{h,cpp}`](../src/geocoder/normalizer.cpp) |
| Dual-form inverted index | [`src/geocoder/inverted_index.{h,cpp}`](../src/geocoder/inverted_index.cpp) |
| Weighted ranker | [`src/geocoder/ranker.{h,cpp}`](../src/geocoder/ranker.cpp) |
| Query orchestrator (splits + aggregation + hn refinement) | [`src/geocoder/geocoder.{h,cpp}`](../src/geocoder/geocoder.cpp) |
| HTTP endpoint `/api/search` | [`src/server/api_handler.cpp::search_geojson`](../src/server/api_handler.cpp) |
| Frontend search UI | [`src/web/map.js`](../src/web/map.js), [`src/web/index.html`](../src/web/index.html), [`src/web/style.css`](../src/web/style.css) |
