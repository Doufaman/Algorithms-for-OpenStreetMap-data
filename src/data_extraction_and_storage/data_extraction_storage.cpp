#include "data_extraction_storage.h"
#include "io/binary_io.h"
#include "config.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <iomanip>
#include <unordered_map>

#include <osmium/io/pbf_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/osm/relation.hpp>

namespace DES {

    // ============================================================
    //  Timing
    // ============================================================
    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    static double elapsed_ms(TimePoint t0) {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - t0).count() / 1000.0;
    }

    // ============================================================
    //  Buffered writer
    //  Accumulates output in a string buffer and flushes to disk
    //  in large chunks, reducing syscall overhead ~10x vs direct
    //  ofstream << for millions of small writes.
    // ============================================================
    struct BufferedWriter {
        std::ofstream file;
        std::string   buf;
        static constexpr size_t FLUSH_THRESHOLD = 1u << 16; // 64 KB

        explicit BufferedWriter(const std::string& path) : file(path) {
            buf.reserve(FLUSH_THRESHOLD + 4096);
        }
        ~BufferedWriter() { flush(); }

        void write(const char* s, size_t n) {
            buf.append(s, n);
            if (buf.size() >= FLUSH_THRESHOLD) flush();
        }
        void write(const std::string& s) { write(s.data(), s.size()); }
        void write(const char* s) { write(s, std::strlen(s)); }

        void flush() {
            if (!buf.empty()) { file << buf; buf.clear(); }
        }
    };

    // ============================================================
    //  JSON helpers
    // ============================================================
    static void json_escape_into(const char* s, std::string& out) {
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
            switch (*p) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (*p < 0x20) { char tmp[8]; snprintf(tmp, 8, "\\u%04x", *p); out += tmp; }
                else out += static_cast<char>(*p);
            }
        }
    }

    static std::string json_escape(const char* s) {
        std::string out; out.reserve(std::strlen(s));
        json_escape_into(s, out); return out;
    }

    // Fixed-precision coordinate formatter (no heap allocation)
    static void write_coord(BufferedWriter& w, int32_t e7) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.7f", e7 / 1e7);
        w.write(buf);
    }

    // ============================================================
    //  Memory estimators
    // ============================================================
    static size_t estimate_points(const std::vector<Point>& v) {
        size_t t = sizeof(Point) * v.capacity();
        for (auto& p : v) t += p.extra_tags.capacity() * sizeof(Tag);
        return t;
    }
    static size_t estimate_lines(const std::vector<Line>& v) {
        size_t t = sizeof(Line) * v.capacity();
        for (auto& l : v) t += l.coords.capacity() * sizeof(CoordSeq::value_type);
        return t;
    }
    static size_t estimate_admin(const std::vector<AdminArea>& v) {
        size_t t = sizeof(AdminArea) * v.capacity();
        for (auto& a : v) t += a.outer_ring.capacity() * sizeof(CoordSeq::value_type);
        return t;
    }
    static size_t estimate_raw_relations(const std::vector<RawRelation>& v) {
        size_t t = sizeof(RawRelation) * v.capacity();
        for (auto& r : v) {
            t += r.members.capacity() * sizeof(RawMember);
            t += r.tags.capacity() * sizeof(Tag);
        }
        return t;
    }

    // ============================================================
    //  Tag helpers
    // ============================================================

    // Intern all tags from an osmium TagList
    static std::vector<Tag> intern_tags(const osmium::TagList& tl, StringPool& pool) {
        std::vector<Tag> out;
        out.reserve(tl.size());
        for (const auto& t : tl)
            out.push_back({ pool.intern(t.key()), pool.intern(t.value()) });
        return out;
    }

    // Find a tag value by raw C-string key (no pool lookup needed)
    static const char* tag_value(const std::vector<Tag>& tags,
        const StringPool& pool,
        const char* key) {
        for (const auto& t : tags)
            if (std::strcmp(pool.get(t.key_off), key) == 0)
                return pool.get(t.val_off);
        return nullptr;
    }

    // ============================================================
    //  Geometry helpers
    // ============================================================
    static void build_bbox(const CoordSeq& pts,
        int32_t& min_lat, int32_t& max_lat,
        int32_t& min_lon, int32_t& max_lon) {
        min_lat = max_lat = pts[0].first;
        min_lon = max_lon = pts[0].second;
        for (const auto& p : pts) {
            if (p.first < min_lat) min_lat = p.first;
            if (p.first > max_lat) max_lat = p.first;
            if (p.second < min_lon) min_lon = p.second;
            if (p.second > max_lon) max_lon = p.second;
        }
    }

    // Populate a Point's named fields from its tag list
    static void fill_point_tags(Point& p, const std::vector<Tag>& tags,
        StringPool& pool) {
        for (const auto& t : tags) {
            const char* k = pool.get(t.key_off);
            if (!strcmp(k, "name"))             p.name_off = t.val_off;
            else if (!strcmp(k, "building"))         p.type_off = t.val_off;
            else if (!strcmp(k, "amenity"))          p.type_off = t.val_off;
            else if (!strcmp(k, "leisure"))          p.type_off = t.val_off;
            else if (!strcmp(k, "addr:street"))      p.street_off = t.val_off;
            else if (!strcmp(k, "addr:housenumber")) p.housenumber_off = t.val_off;
            else if (!strcmp(k, "addr:postcode"))    p.postcode_off = t.val_off;
            // OSM may carry explicit address tags — when present these are
            // more reliable than PIP-derived admin attribution.
            else if (!strcmp(k, "addr:country"))     p.country_off = t.val_off;
            else if (!strcmp(k, "addr:state"))       p.state_off   = t.val_off;
            else if (!strcmp(k, "addr:city"))        p.city_off    = t.val_off;
            else if (!strcmp(k, "addr:suburb"))      p.suburb_off  = t.val_off;
            else p.extra_tags.push_back(t);
        }
    }

    // ============================================================
    //  PASS 0 HANDLER – Relation pre-scan
    //  Collects IDs of all ways that are members of admin-boundary
    //  relations. These IDs are read again in Pass 2 to force their
    //  geometry into the cache even when they carry no tags of their
    //  own — without this step admin polygons cannot be assembled.
    // ============================================================
    struct RelationPrescanHandler : public osmium::handler::Handler {
        OsmData& db;
        explicit RelationPrescanHandler(OsmData& d) : db(d) {}

        void relation(const osmium::Relation& r) {
            const char* type = r.tags()["type"];
            if (!type || std::strcmp(type, "boundary") != 0) return;
            if (!r.tags()["admin_level"]) return;
            for (const auto& m : r.members())
                if (m.type() == osmium::item_type::way)
                    db.needed_way_ids.insert(m.ref());
        }
    };

    // ============================================================
    //  PASS 1 HANDLER – Nodes only
    //  • Geometry nodes  → NodeCache (coords only, 8 bytes/node)
    //  • Semantic nodes  → Point (direct, no raw storage)
    // ============================================================
    struct Pass1Handler : public osmium::handler::Handler {
        OsmData& db;
        Stats& stats;

        Pass1Handler(OsmData& d, Stats& s) : db(d), stats(s) {}

        void node(const osmium::Node& n) {
            ++stats.raw_node_count;

            int32_t lat = static_cast<int32_t>(n.location().lat() * 1e7);
            int32_t lon = static_cast<int32_t>(n.location().lon() * 1e7);

            // Always cache coordinates (needed by ways)
            db.node_cache.insert(n.id(), lat, lon);

            // If the node carries semantic tags, emit a Point immediately
            if (!n.tags().empty()) {
                auto tags = intern_tags(n.tags(), db.pool);
                Point p;
                p.osm_id = n.id();
                p.lat_e7 = lat;
                p.lon_e7 = lon;
                fill_point_tags(p, tags, db.pool);
                db.points.push_back(std::move(p));
            }
        }
    };

    // ============================================================
    //  PASS 2 HANDLER – Ways and Relations
    //  Ways are classified on the fly:
    //    area tags  → centroid Point
    //    line tags  → Line with full coords
    //    both       → both outputs
    //  Relations with type=boundary are buffered as RawRelation
    //  so the admin polygon can be assembled after all ways are
    //  processed (way geom cache is complete at that point).
    // ============================================================
    struct Pass2Handler : public osmium::handler::Handler {
        OsmData& db;
        Stats& stats;

        Pass2Handler(OsmData& d, Stats& s) : db(d), stats(s) {}

        // Resolve node refs → coordinate sequence using NodeCache
        CoordSeq resolve_coords(const osmium::WayNodeList& nodes) const {
            CoordSeq coords;
            coords.reserve(nodes.size());
            for (const auto& nr : nodes) {
                int32_t lat, lon;
                if (db.node_cache.lookup(nr.ref(), lat, lon))
                    coords.push_back({ lat, lon });
            }
            return coords;
        }

        void way(const osmium::Way& w) {
            ++stats.raw_way_count;
            const bool needed_by_boundary = db.needed_way_ids.contains(w.id());

            // Fast path: tagless way that's only useful as a boundary segment.
            // Resolve coords and cache, then bail — no semantic output to emit.
            if (w.tags().empty()) {
                if (needed_by_boundary) {
                    CoordSeq coords = resolve_coords(w.nodes());
                    if (!coords.empty())
                        db.way_geom_cache.insert(w.id(), std::move(coords));
                }
                return;
            }

            auto tags = intern_tags(w.tags(), db.pool);
            const StringPool& pool = db.pool;

            const char* building = tag_value(tags, pool, "building");
            const char* amenity = tag_value(tags, pool, "amenity");
            const char* leisure = tag_value(tags, pool, "leisure");
            const char* natural_t = tag_value(tags, pool, "natural");
            const char* landuse = tag_value(tags, pool, "landuse");
            const char* highway = tag_value(tags, pool, "highway");
            const char* bridge = tag_value(tags, pool, "bridge");
            const char* railway = tag_value(tags, pool, "railway");
            const char* waterway = tag_value(tags, pool, "waterway");

            bool is_area = building || amenity || leisure || natural_t || landuse;
            bool is_line = highway || bridge || railway || waterway;

            // Resolve geometry once if needed
            CoordSeq coords;
            bool coords_resolved = false;
            auto ensure_coords = [&]() {
                if (!coords_resolved) {
                    coords = resolve_coords(w.nodes());
                    coords_resolved = true;
                }
                };

            // ── Area → Point (centroid) ───────────────────────────
            if (is_area) {
                ensure_coords();
                if (!coords.empty()) {
                    int64_t lat_sum = 0, lon_sum = 0;
                    for (auto& c : coords) { lat_sum += c.first; lon_sum += c.second; }
                    int32_t clat = static_cast<int32_t>(lat_sum / (int64_t)coords.size());
                    int32_t clon = static_cast<int32_t>(lon_sum / (int64_t)coords.size());

                    Point p;
                    p.osm_id = w.id();
                    p.lat_e7 = clat;
                    p.lon_e7 = clon;
                    fill_point_tags(p, tags, db.pool);
                    db.points.push_back(std::move(p));
                }
            }

            // ── Line → Line (full geometry) ───────────────────────
            if (is_line) {
                ensure_coords();
                if (!coords.empty()) {
                    Line l;
                    l.osm_id = w.id();
                    l.coords = coords;   // copy – coords may also be needed for cache
                    build_bbox(l.coords, l.min_lat_e7, l.max_lat_e7,
                        l.min_lon_e7, l.max_lon_e7);
                    for (const auto& t : tags) {
                        const char* k = pool.get(t.key_off);
                        if (!strcmp(k, "name"))    l.name_off = t.val_off;
                        else if (!strcmp(k, "highway")) l.type_off = t.val_off;
                        else if (!strcmp(k, "railway")) l.type_off = t.val_off;
                        else if (!strcmp(k, "bridge"))  l.type_off = t.val_off;
                        else if (!strcmp(k, "ref"))     l.ref_off = t.val_off;
                    }
                    db.lines.push_back(std::move(l));
                }
            }

            // ── Cache geometry for boundary relations ─────────────
            // A way tagged with e.g. boundary=administrative carries tags
            // but is neither an area nor a line — make sure its coords
            // are resolved if a boundary relation needs them.
            if (needed_by_boundary) ensure_coords();

            // We cache ALL ways with coords so relation assembly is O(1).
            // Only store if coords were already resolved (avoid extra work).
            if (coords_resolved && !coords.empty())
                db.way_geom_cache.insert(w.id(), std::move(coords));
        }

        void relation(const osmium::Relation& r) {
            ++stats.raw_relation_count;

            // Only keep boundary relations for admin area assembly
            const char* type = r.tags()["type"];
            if (!type || std::strcmp(type, "boundary") != 0) return;
            const char* level = r.tags()["admin_level"];
            if (!level) return;

            RawRelation rr;
            rr.id = r.id();
            rr.tags = intern_tags(r.tags(), db.pool);
            rr.members.reserve(r.members().size());
            for (const auto& m : r.members()) {
                rr.members.push_back({
                    osmium::item_type_to_char(m.type()),
                    m.ref(),
                    db.pool.intern(m.role())
                    });
            }
            db.raw_relations.push_back(std::move(rr));
        }
    };

    // ============================================================
    //  EXTRACTION
    //  Pass 1: nodes only  (entity filter skips way/relation blobs)
    //  Pass 2: ways + relations
    // ============================================================
    void extraction(const std::string& pbf_path, OsmData& db, Stats& stats) {
        std::cout << "[1/3] Extraction\n";

        // Pre-allocate to reduce rehashing (Stuttgart ~8M nodes)
        db.node_cache.data.reserve(10'000'000);
        db.points.reserve(500'000);
        db.lines.reserve(200'000);
        db.raw_relations.reserve(20'000);
        db.needed_way_ids.reserve(50'000);

        // ── Pass 0: Pre-scan relations to find boundary-way IDs ──
        std::cout << "  Pass 0: pre-scanning boundary relations ...\n";
        auto t0 = Clock::now();
        {
            osmium::io::Reader reader{ pbf_path, osmium::osm_entity_bits::relation };
            RelationPrescanHandler h{ db };
            osmium::apply(reader, h);
            reader.close();
        }
        stats.prescan_ms           = elapsed_ms(t0);
        stats.needed_way_ids_count = db.needed_way_ids.size();
        std::cout << "    needed ways=" << db.needed_way_ids.size()
                  << "  (" << stats.prescan_ms << " ms)\n";

        // ── Pass 1: Nodes ────────────────────────────────────────
        std::cout << "  Pass 1: reading nodes ...\n";
        auto t1 = Clock::now();
        {
            osmium::io::Reader reader{ pbf_path, osmium::osm_entity_bits::node };
            Pass1Handler h{ db, stats };
            osmium::apply(reader, h);
            reader.close();
        }
        stats.pass1_ms = elapsed_ms(t1);
        std::cout << "    nodes=" << stats.raw_node_count
            << "  (" << stats.pass1_ms << " ms)\n";

        // ── Pass 2: Ways + Relations ─────────────────────────────
        std::cout << "  Pass 2: reading ways + relations ...\n";
        auto t2 = Clock::now();
        {
            osmium::io::Reader reader{ pbf_path,
                osmium::osm_entity_bits::way | osmium::osm_entity_bits::relation };
            Pass2Handler h{ db, stats };
            osmium::apply(reader, h);
            reader.close();
        }
        stats.pass2_ms = elapsed_ms(t2);
        std::cout << "    ways=" << stats.raw_way_count
            << "  relations(boundary)=" << db.raw_relations.size()
            << "  (" << stats.pass2_ms << " ms)\n";

        // Snapshot memory before processing frees caches
        stats.node_cache_bytes = db.node_cache.byte_size();
        stats.way_cache_bytes = db.way_geom_cache.byte_size();
        stats.raw_rel_bytes = estimate_raw_relations(db.raw_relations);
        stats.string_pool_bytes = db.pool.byte_size();
    }

    // ============================================================
    //  Ring stitching (Sheet 3 followup — fixes Sheet 1 concat bug)
    //
    //  OSM multipolygon boundaries are stored as a list of way
    //  members. Each way is an ordered coord sequence, but the
    //  ways themselves are NOT in ring order — you have to stitch
    //  them by matching endpoints (start / end coords).
    //
    //  Old code just concat'd in relation-member order, which drew
    //  spurious edges through the interior and broke PIP + display.
    //
    //  Algorithm:
    //    • pick each unused segment as a seed
    //    • greedily extend: find a segment whose start OR end matches
    //      the current ring's tail; append (or reverse-append)
    //    • keep the longest ring produced across all seeds
    //
    //  Complexity per relation: O(N²) in segment count, but N is
    //  typically < 20 so this is negligible.
    // ============================================================
    static CoordSeq stitch_ring(std::vector<const CoordSeq*> segments) {
        // Filter out null / empty segments
        segments.erase(std::remove_if(segments.begin(), segments.end(),
                       [](const CoordSeq* s){ return !s || s->empty(); }),
                       segments.end());
        if (segments.empty()) return {};
        if (segments.size() == 1) return *segments[0];

        auto pack = [](std::pair<int32_t,int32_t> p) -> uint64_t {
            return ((uint64_t)(uint32_t)p.first << 32) | (uint32_t)p.second;
        };

        CoordSeq best;

        for (size_t seed = 0; seed < segments.size(); ++seed) {
            std::vector<bool> used(segments.size(), false);
            used[seed] = true;
            CoordSeq ring = *segments[seed];
            if (ring.empty()) continue;

            // Greedy forward extension
            bool extended = true;
            while (extended) {
                extended = false;
                uint64_t tail = pack(ring.back());
                for (size_t i = 0; i < segments.size(); ++i) {
                    if (used[i]) continue;
                    const CoordSeq& s = *segments[i];
                    bool match_start = (pack(s.front()) == tail);
                    bool match_end   = (pack(s.back())  == tail);
                    if (!match_start && !match_end) continue;

                    used[i] = true;
                    if (match_start) {
                        // Append skipping the first coord (== ring.back())
                        ring.insert(ring.end(), s.begin() + 1, s.end());
                    } else {
                        // Reverse-append, skipping the last coord
                        for (auto it = s.rbegin() + 1; it != s.rend(); ++it)
                            ring.push_back(*it);
                    }
                    extended = true;
                    break;
                }
            }

            if (ring.size() > best.size()) best = std::move(ring);
        }

        // If stitching totally failed (shouldn't happen with good data),
        // fall back to naive concat so we at least have a bbox.
        if (best.empty()) {
            for (const auto* s : segments)
                best.insert(best.end(), s->begin(), s->end());
        }
        return best;
    }

    // ============================================================
    //  PROCESSING
    //  Assembles AdminAreas from buffered RawRelations using the
    //  WayGeomCache, then releases all temporary data.
    // ============================================================
    void processing(OsmData& db, Stats& stats) {
        std::cout << "[2/3] Processing – assembling admin areas ...\n";
        auto t0 = Clock::now();

        StringPool& pool = db.pool;
        size_t n_open_rings = 0;   // rings that didn't close cleanly

        for (const auto& rr : db.raw_relations) {
            const char* level = tag_value(rr.tags, pool, "admin_level");
            if (!level) continue;

            AdminArea a;
            a.osm_id = rr.id;
            a.admin_level = static_cast<uint8_t>(std::atoi(level));

            const char* name = tag_value(rr.tags, pool, "name");
            if (name && *name) a.name_off = pool.intern(name);

            // Collect segment pointers for outer role
            std::vector<const CoordSeq*> segs;
            segs.reserve(rr.members.size());
            for (const auto& m : rr.members) {
                if (m.type != 'w') continue;
                const char* role = pool.get(m.role_off);
                if (std::strcmp(role, "outer") != 0 &&
                    std::strcmp(role, "") != 0) continue;
                segs.push_back(db.way_geom_cache.find(m.ref));
            }

            // Stitch segments into a proper ring by endpoint matching
            a.outer_ring = stitch_ring(std::move(segs));

            if (a.outer_ring.empty()) continue;

            // Track ring quality
            if (a.outer_ring.size() >= 2 &&
                a.outer_ring.front() != a.outer_ring.back()) ++n_open_rings;

            build_bbox(a.outer_ring, a.min_lat_e7, a.max_lat_e7,
                a.min_lon_e7, a.max_lon_e7);
            db.admin_areas.push_back(std::move(a));
        }

        // Release temporary caches – free significant memory
        { NodeCache    tmp; std::swap(db.node_cache, tmp); }
        { WayGeomCache tmp; std::swap(db.way_geom_cache, tmp); }
        { std::vector<RawRelation> tmp; std::swap(db.raw_relations, tmp); }
        { std::unordered_set<int64_t> tmp; std::swap(db.needed_way_ids, tmp); }

        stats.processing_ms = elapsed_ms(t0);
        stats.point_count = db.points.size();
        stats.line_count = db.lines.size();
        stats.admin_count = db.admin_areas.size();
        stats.points_bytes = estimate_points(db.points);
        stats.lines_bytes = estimate_lines(db.lines);
        stats.admin_bytes = estimate_admin(db.admin_areas);

        std::cout << "    points=" << stats.point_count
            << "  lines=" << stats.line_count
            << "  admin_areas=" << stats.admin_count
            << "  (" << stats.processing_ms << " ms)\n";
        if (n_open_rings) {
            std::cout << "    warning: " << n_open_rings
                      << " admin ring(s) did not close cleanly\n";
        }
    }

    // ============================================================
    //  STORAGE – write JSON + print stats
    // ============================================================

    static void write_points(const OsmData& db, const std::string& path) {
        BufferedWriter w(path);
        const StringPool& pool = db.pool;
        w.write("[\n");
        for (size_t i = 0; i < db.points.size(); ++i) {
            const Point& p = db.points[i];
            if (i) w.write(",\n");
            std::string entry;
            entry.reserve(128);
            entry += "  {\"id\":"; entry += std::to_string(p.osm_id);
            entry += ",\"lat\":";  // coord written separately
            w.write(entry);
            write_coord(w, p.lat_e7);
            w.write(",\"lon\":");
            write_coord(w, p.lon_e7);
            w.write(",\"tags\":{");

            bool first = true;
            auto emit = [&](const char* key, uint32_t off) {
                if (off == StringPool::NONE) return;
                std::string s;
                if (!first) s += ',';
                s += '"'; s += key; s += "\":\"";
                json_escape_into(pool.get(off), s);
                s += '"';
                w.write(s);
                first = false;
                };
            emit("name", p.name_off);
            emit("type", p.type_off);
            emit("street", p.street_off);
            emit("housenumber", p.housenumber_off);
            emit("postcode", p.postcode_off);
            emit("country", p.country_off);
            emit("state",   p.state_off);
            emit("city",    p.city_off);
            emit("suburb",  p.suburb_off);
            for (const auto& t : p.extra_tags) {
                std::string s;
                if (!first) s += ',';
                s += '"'; json_escape_into(pool.get(t.key_off), s);
                s += "\":\""; json_escape_into(pool.get(t.val_off), s); s += '"';
                w.write(s);
                first = false;
            }
            w.write("}}");
        }
        w.write("\n]\n");
    }

    static void write_lines(const OsmData& db, const std::string& path) {
        BufferedWriter w(path);
        const StringPool& pool = db.pool;
        w.write("[\n");
        for (size_t i = 0; i < db.lines.size(); ++i) {
            const Line& l = db.lines[i];
            if (i) w.write(",\n");
            std::string hdr;
            hdr += "  {\"id\":"; hdr += std::to_string(l.osm_id);
            if (l.name_off != StringPool::NONE) {
                hdr += ",\"name\":\""; json_escape_into(pool.get(l.name_off), hdr); hdr += '"';
            }
            if (l.type_off != StringPool::NONE) {
                hdr += ",\"type\":\""; json_escape_into(pool.get(l.type_off), hdr); hdr += '"';
            }
            if (l.ref_off != StringPool::NONE) {
                hdr += ",\"ref\":\""; json_escape_into(pool.get(l.ref_off), hdr); hdr += '"';
            }
            w.write(hdr);
            // bbox
            w.write(",\"bbox\":[");
            write_coord(w, l.min_lat_e7); w.write(",");
            write_coord(w, l.max_lat_e7); w.write(",");
            write_coord(w, l.min_lon_e7); w.write(",");
            write_coord(w, l.max_lon_e7);
            w.write("],\"coords\":[");
            for (size_t j = 0; j < l.coords.size(); ++j) {
                if (j) w.write(",");
                w.write("["); write_coord(w, l.coords[j].first);
                w.write(","); write_coord(w, l.coords[j].second); w.write("]");
            }
            w.write("]}");
        }
        w.write("\n]\n");
    }

    static void write_admin(const OsmData& db, const std::string& path) {
        BufferedWriter w(path);
        const StringPool& pool = db.pool;
        w.write("[\n");
        for (size_t i = 0; i < db.admin_areas.size(); ++i) {
            const AdminArea& a = db.admin_areas[i];
            if (i) w.write(",\n");
            std::string hdr;
            hdr += "  {\"id\":"; hdr += std::to_string(a.osm_id);
            hdr += ",\"admin_level\":"; hdr += std::to_string((int)a.admin_level);
            if (a.name_off != StringPool::NONE) {
                hdr += ",\"name\":\""; json_escape_into(pool.get(a.name_off), hdr); hdr += '"';
            }
            w.write(hdr);
            w.write(",\"bbox\":[");
            write_coord(w, a.min_lat_e7); w.write(",");
            write_coord(w, a.max_lat_e7); w.write(",");
            write_coord(w, a.min_lon_e7); w.write(",");
            write_coord(w, a.max_lon_e7);
            w.write("],\"outer_ring\":[");
            for (size_t j = 0; j < a.outer_ring.size(); ++j) {
                if (j) w.write(",");
                w.write("["); write_coord(w, a.outer_ring[j].first);
                w.write(","); write_coord(w, a.outer_ring[j].second); w.write("]");
            }
            w.write("]}");
        }
        w.write("\n]\n");
    }

    static std::string fmt_mb(size_t bytes) {
        char buf[32]; snprintf(buf, sizeof(buf), "%.2f MB", bytes / 1048576.0); return buf;
    }

    static void print_stats(const Stats& s) {
        double total_ms = s.prescan_ms + s.pass1_ms + s.pass2_ms + s.processing_ms + s.storage_ms;

        std::cout << "\n"
            << "╔══════════════════════════════════════════════╗\n"
            << "║             OSM Import Stats                  ║\n"
            << "╠══════════════════════════════════════════════╣\n"
            << "║  RAW COUNTS                                   ║\n"
            << "║    Nodes     : " << std::setw(14) << s.raw_node_count << "            ║\n"
            << "║    Ways      : " << std::setw(14) << s.raw_way_count << "            ║\n"
            << "║    Relations : " << std::setw(14) << s.raw_relation_count << "            ║\n"
            << "╠══════════════════════════════════════════════╣\n"
            << "║  PROCESSED COUNTS                             ║\n"
            << "║    Points    : " << std::setw(14) << s.point_count << "            ║\n"
            << "║    Lines     : " << std::setw(14) << s.line_count << "            ║\n"
            << "║    AdminAreas: " << std::setw(14) << s.admin_count << "            ║\n"
            << "╠══════════════════════════════════════════════╣\n"
            << "║  MEMORY (estimated)                           ║\n"
            << "║    Node cache    : " << fmt_mb(s.node_cache_bytes) << "\n"
            << "║    Way geo cache : " << fmt_mb(s.way_cache_bytes) << "\n"
            << "║    Raw relations : " << fmt_mb(s.raw_rel_bytes) << "\n"
            << "║    Points        : " << fmt_mb(s.points_bytes) << "\n"
            << "║    Lines         : " << fmt_mb(s.lines_bytes) << "\n"
            << "║    Admin areas   : " << fmt_mb(s.admin_bytes) << "\n"
            << "║    String pool   : " << fmt_mb(s.string_pool_bytes) << "\n"
            << "╠══════════════════════════════════════════════╣\n"
            << "║  TIMING                                       ║\n"
            << "║    Pass 0 (prescan)  : " << s.prescan_ms << " ms  (needed ways=" << s.needed_way_ids_count << ")\n"
            << "║    Pass 1 (nodes)    : " << s.pass1_ms << " ms\n"
            << "║    Pass 2 (ways/rel) : " << s.pass2_ms << " ms\n"
            << "║    Processing        : " << s.processing_ms << " ms\n"
            << "║    Storage (JSON)    : " << s.storage_ms << " ms\n"
            << "║    Total             : " << total_ms << " ms\n"
            << "╚══════════════════════════════════════════════╝\n";
    }

    void storage(const OsmData& db, Stats& stats, const std::string& out_dir) {
        std::cout << "[3/3] Storage ...\n";
        auto t0 = Clock::now();

        std::string sep = out_dir;
        if (!sep.empty() && sep.back() != '/' && sep.back() != '\\') sep += '/';

        // ---- JSON (legacy, opt-in via Config::STORE_JSON) ----
        if constexpr (Config::STORE_JSON) {
            auto tj = Clock::now();
            write_points(db, sep + "points.json");
            std::cout << "    written: " << sep << "points.json\n";

            write_lines(db, sep + "lines.json");
            std::cout << "    written: " << sep << "lines.json\n";

            write_admin(db, sep + "admin_areas.json");
            std::cout << "    written: " << sep << "admin_areas.json\n";
            std::cout << "    JSON   write took " << elapsed_ms(tj) << " ms\n";
        } else {
            std::cout << "    JSON   skipped (Config::STORE_JSON == false)\n";
        }

        // ---- Binary (preferred at load time, opt-in via Config::STORE_BIN) ----
        if constexpr (Config::STORE_BIN) {
            auto tb = Clock::now();
            bin::write_points(db, sep + "points.bin");
            std::cout << "    written: " << sep << "points.bin\n";
            bin::write_lines (db, sep + "lines.bin");
            std::cout << "    written: " << sep << "lines.bin\n";
            bin::write_admin (db, sep + "admin_areas.bin");
            std::cout << "    written: " << sep << "admin_areas.bin\n";
            std::cout << "    binary write took " << elapsed_ms(tb) << " ms\n";
        } else {
            std::cout << "    Binary skipped (Config::STORE_BIN == false)\n";
        }

        stats.storage_ms = elapsed_ms(t0);
        print_stats(stats);
    }

    // ============================================================
    //  Convenience wrapper
    // ============================================================
    void extractFromPBF(const std::string& pbf_path) {
        std::string out_dir = ".";
        auto slash = pbf_path.find_last_of("/\\");
        if (slash != std::string::npos) out_dir = pbf_path.substr(0, slash);

        OsmData db;
        Stats   stats;
        extraction(pbf_path, db, stats);
        processing(db, stats);
        storage(db, stats, out_dir);
    }

} // namespace DES
