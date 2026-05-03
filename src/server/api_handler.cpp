#include "api_handler.h"
#include "httplib.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// ============================================================
//  Minimal pull-style JSON parser
//  Only parses the flat structure produced by our storage code.
//  Avoids a full JSON library dependency.
// ============================================================

// Skip whitespace
static void skip_ws(const char*& p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
}

// Read a quoted JSON string; advances p past closing "
static std::string read_string(const char*& p) {
    if (*p != '"') return {};
    ++p;
    std::string out;
    while (*p && *p != '"') {
        if (*p == '\\') {
            ++p;
            switch (*p) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                default:   out += *p;  break;
            }
        } else {
            out += *p;
        }
        ++p;
    }
    if (*p == '"') ++p;
    return out;
}

// Read a JSON number as double; advances p
static double read_number(const char*& p) {
    char* end;
    double v = std::strtod(p, &end);
    p = end;
    return v;
}

// Expect a specific character, skip it
static bool expect(const char*& p, char c) {
    skip_ws(p);
    if (*p == c) { ++p; return true; }
    return false;
}

// ── Load points.json ─────────────────────────────────────────
// Format: [ {"id":N,"lat":F,"lon":F,"tags":{...}}, ... ]
void ApiHandler::load_points(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "Cannot open " << path << "\n"; return; }
    std::string src((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    const char* p = src.c_str();

    expect(p, '[');
    while (true) {
        skip_ws(p);
        if (*p == ']' || *p == '\0') break;
        if (*p == ',') { ++p; continue; }
        if (*p != '{') { ++p; continue; }
        ++p; // skip {

        PointRecord rec{};
        while (true) {
            skip_ws(p);
            if (*p == '}' || *p == '\0') { if (*p=='}') ++p; break; }
            if (*p == ',') { ++p; continue; }
            if (*p != '"') { ++p; continue; }

            std::string key = read_string(p);
            expect(p, ':');
            skip_ws(p);

            if      (key == "id")  rec.id  = (int64_t)read_number(p);
            else if (key == "lat") rec.lat = read_number(p);
            else if (key == "lon") rec.lon = read_number(p);
            else if (key == "tags") {
                // parse inner object
                expect(p, '{');
                while (true) {
                    skip_ws(p);
                    if (*p == '}' || *p == '\0') { if (*p=='}') ++p; break; }
                    if (*p == ',') { ++p; continue; }
                    if (*p != '"') { ++p; continue; }
                    std::string tk = read_string(p);
                    expect(p, ':');
                    skip_ws(p);
                    std::string tv = (*p == '"') ? read_string(p) : "";
                    if      (tk == "name")        rec.name        = tv;
                    else if (tk == "type")        rec.type        = tv;
                    else if (tk == "street")      rec.street      = tv;
                    else if (tk == "housenumber") rec.housenumber = tv;
                    else if (tk == "postcode")    rec.postcode    = tv;
                }
            } else {
                // skip unknown value
                if (*p == '"') read_string(p);
                else if (*p == '{' || *p == '[') {
                    // skip nested structure
                    int depth = 0;
                    while (*p) {
                        if (*p=='{' || *p=='[') ++depth;
                        else if (*p=='}' || *p==']') { --depth; if(depth==0){++p;break;} }
                        ++p;
                    }
                } else read_number(p);
            }
        }
        points_.push_back(std::move(rec));
    }
    std::cout << "  Loaded " << points_.size() << " points\n";
}

// ── Load lines.json ───────────────────────────────────────────
// Format: [ {"id":N,"name":"...","type":"...","bbox":[...],
//            "coords":[[lat,lon],...] }, ... ]
void ApiHandler::load_lines(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "Cannot open " << path << "\n"; return; }
    std::string src((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    const char* p = src.c_str();

    expect(p, '[');
    while (true) {
        skip_ws(p);
        if (*p == ']' || *p == '\0') break;
        if (*p == ',') { ++p; continue; }
        if (*p != '{') { ++p; continue; }
        ++p;

        LineRecord rec{};
        while (true) {
            skip_ws(p);
            if (*p == '}' || *p == '\0') { if (*p=='}') ++p; break; }
            if (*p == ',') { ++p; continue; }
            if (*p != '"') { ++p; continue; }

            std::string key = read_string(p);
            expect(p, ':');
            skip_ws(p);

            if      (key == "id")   rec.id   = (int64_t)read_number(p);
            else if (key == "name") rec.name = read_string(p);
            else if (key == "type") rec.type = read_string(p);
            else if (key == "ref")  rec.ref  = read_string(p);
            else if (key == "bbox") {
                expect(p, '[');
                rec.min_lat = read_number(p); expect(p,',');
                rec.max_lat = read_number(p); expect(p,',');
                rec.min_lon = read_number(p); expect(p,',');
                rec.max_lon = read_number(p);
                expect(p, ']');
            }
            else if (key == "coords") {
                expect(p, '[');
                while (true) {
                    skip_ws(p);
                    if (*p == ']' || *p == '\0') { if(*p==']') ++p; break; }
                    if (*p == ',') { ++p; continue; }
                    if (*p == '[') {
                        ++p;
                        double lat = read_number(p); expect(p,',');
                        double lon = read_number(p);
                        expect(p, ']');
                        rec.coords.push_back(lat);
                        rec.coords.push_back(lon);
                    } else ++p;
                }
            }
            else {
                if (*p == '"') read_string(p);
                else { int d=0; while(*p){if(*p=='{'||*p=='[')++d;else if(*p=='}'||*p==']'){--d;if(d<=0&&(*p=='}'||*p==']')){if(d==0)++p;break;}}++p;} }
            }
        }
        lines_.push_back(std::move(rec));
    }
    std::cout << "  Loaded " << lines_.size() << " lines\n";
}

// ── Load admin_areas.json ─────────────────────────────────────
void ApiHandler::load_admin(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "Cannot open " << path << "\n"; return; }
    std::string src((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    const char* p = src.c_str();

    expect(p, '[');
    while (true) {
        skip_ws(p);
        if (*p == ']' || *p == '\0') break;
        if (*p == ',') { ++p; continue; }
        if (*p != '{') { ++p; continue; }
        ++p;

        AdminRecord rec{};
        while (true) {
            skip_ws(p);
            if (*p == '}' || *p == '\0') { if (*p=='}') ++p; break; }
            if (*p == ',') { ++p; continue; }
            if (*p != '"') { ++p; continue; }

            std::string key = read_string(p);
            expect(p, ':');
            skip_ws(p);

            if      (key == "id")          rec.id          = (int64_t)read_number(p);
            else if (key == "admin_level") rec.admin_level = (int)read_number(p);
            else if (key == "name")        rec.name        = read_string(p);
            else if (key == "bbox") {
                expect(p,'[');
                rec.min_lat = read_number(p); expect(p,',');
                rec.max_lat = read_number(p); expect(p,',');
                rec.min_lon = read_number(p); expect(p,',');
                rec.max_lon = read_number(p);
                expect(p,']');
            }
            else if (key == "outer_ring") {
                expect(p,'[');
                while (true) {
                    skip_ws(p);
                    if (*p==']'||*p=='\0'){if(*p==']')++p;break;}
                    if (*p==','){++p;continue;}
                    if (*p=='[') {
                        ++p;
                        double lat = read_number(p); expect(p,',');
                        double lon = read_number(p);
                        expect(p,']');
                        rec.ring.push_back(lat);
                        rec.ring.push_back(lon);
                    } else ++p;
                }
            }
            else {
                if (*p=='"') read_string(p);
                else read_number(p);
            }
        }
        admins_.push_back(std::move(rec));
    }
    std::cout << "  Loaded " << admins_.size() << " admin areas\n";
}

// ── Constructor ───────────────────────────────────────────────
ApiHandler::ApiHandler(const std::string& data_dir) {
    std::string sep = data_dir;
    if (!sep.empty() && sep.back() != '/' && sep.back() != '\\') sep += '/';
    load_points(sep + "points.json");
    load_lines (sep + "lines.json");
    load_admin (sep + "admin_areas.json");
}

// ============================================================
//  GeoJSON builders
// ============================================================

static std::string esc(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else                out += c;
    }
    return out;
}

static char coord_buf[64];
static const char* fc(double v) {
    snprintf(coord_buf, sizeof(coord_buf), "%.7f", v);
    return coord_buf;
}

std::string ApiHandler::points_geojson(const BBox& bbox, int limit) const {
    std::string out;
    out.reserve(1 << 20);
    out += "{\"type\":\"FeatureCollection\",\"features\":[";
    bool first = true;
    int  count = 0;
    for (const auto& pt : points_) {
        if (!bbox.contains(pt.lat, pt.lon)) continue;
        if (count >= limit) break;
        if (!first) out += ',';
        first = false;
        out += "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\","
               "\"coordinates\":[";
        char buf[64];
        snprintf(buf,64,"%.7f,%.7f",pt.lon,pt.lat);
        out += buf;
        out += "]},\"properties\":{\"id\":";
        out += std::to_string(pt.id);
        if (!pt.name.empty())        { out += ",\"name\":\""        + esc(pt.name)        + "\""; }
        if (!pt.type.empty())        { out += ",\"type\":\""        + esc(pt.type)        + "\""; }
        if (!pt.street.empty())      { out += ",\"street\":\""      + esc(pt.street)      + "\""; }
        if (!pt.housenumber.empty()) { out += ",\"housenumber\":\"" + esc(pt.housenumber) + "\""; }
        if (!pt.postcode.empty())    { out += ",\"postcode\":\""    + esc(pt.postcode)    + "\""; }
        out += "}}";
        ++count;
    }
    out += "]}";
    return out;
}

std::string ApiHandler::lines_geojson(const BBox& bbox, int limit) const {
    std::string out;
    out.reserve(1 << 20);
    out += "{\"type\":\"FeatureCollection\",\"features\":[";
    bool first = true;
    int  count = 0;
    for (const auto& ln : lines_) {
        if (!bbox.intersects(ln.min_lat, ln.max_lat, ln.min_lon, ln.max_lon)) continue;
        if (count >= limit) break;
        if (!first) out += ',';
        first = false;
        out += "{\"type\":\"Feature\",\"geometry\":{\"type\":\"LineString\","
               "\"coordinates\":[";
        bool fc_first = true;
        for (size_t i = 0; i + 1 < ln.coords.size(); i += 2) {
            if (!fc_first) out += ',';
            fc_first = false;
            char buf[64];
            snprintf(buf,64,"[%.7f,%.7f]", ln.coords[i+1], ln.coords[i]);
            out += buf;
        }
        out += "]},\"properties\":{\"id\":";
        out += std::to_string(ln.id);
        if (!ln.name.empty()) { out += ",\"name\":\"" + esc(ln.name) + "\""; }
        if (!ln.type.empty()) { out += ",\"type\":\"" + esc(ln.type) + "\""; }
        if (!ln.ref.empty())  { out += ",\"ref\":\""  + esc(ln.ref)  + "\""; }
        out += "}}";
        ++count;
    }
    out += "]}";
    return out;
}

std::string ApiHandler::admin_geojson(const BBox& bbox) const {
    std::string out;
    out.reserve(1 << 18);
    out += "{\"type\":\"FeatureCollection\",\"features\":[";
    bool first = true;
    for (const auto& ad : admins_) {
        if (!bbox.intersects(ad.min_lat, ad.max_lat, ad.min_lon, ad.max_lon)) continue;
        if (!first) out += ',';
        first = false;
        out += "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Polygon\","
               "\"coordinates\":[[";
        bool rc_first = true;
        for (size_t i = 0; i + 1 < ad.ring.size(); i += 2) {
            if (!rc_first) out += ',';
            rc_first = false;
            char buf[64];
            snprintf(buf,64,"[%.7f,%.7f]", ad.ring[i+1], ad.ring[i]);
            out += buf;
        }
        out += "]]},\"properties\":{\"id\":";
        out += std::to_string(ad.id);
        out += ",\"admin_level\":";
        out += std::to_string(ad.admin_level);
        if (!ad.name.empty()) { out += ",\"name\":\"" + esc(ad.name) + "\""; }
        out += "}}";
    }
    out += "]}";
    return out;
}

