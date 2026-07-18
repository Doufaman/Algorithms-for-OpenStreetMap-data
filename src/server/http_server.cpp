#include "http_server.h"
#include "api_handler.h"
#include "httplib.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <algorithm>

namespace Server {

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string mime_type(const std::string& path) {
    if (path.size() >= 3 && path.substr(path.size()-3) == ".js")   return "application/javascript";
    if (path.size() >= 4 && path.substr(path.size()-4) == ".css")  return "text/css";
    if (path.size() >= 5 && path.substr(path.size()-5) == ".html") return "text/html";
    return "text/plain";
}

// Parse bbox query param: minLon,minLat,maxLon,maxLat
static BBox parse_bbox(const httplib::Request& req) {
    BBox b;
    if (!req.has_param("bbox")) return b;
    std::string s = req.get_param_value("bbox");
    sscanf(s.c_str(), "%lf,%lf,%lf,%lf",
           &b.min_lon, &b.min_lat, &b.max_lon, &b.max_lat);
    return b;
}

static int parse_limit(const httplib::Request& req, int def) {
    if (!req.has_param("limit")) return def;
    return std::atoi(req.get_param_value("limit").c_str());
}

// ------------------------------------------------------------
//  Scan `data_root` for subdirectories that look like a parsed
//  dataset (must contain at least points.bin).  Returns the list
//  of dataset names, sorted alphabetically.
// ------------------------------------------------------------
static std::vector<std::string> list_datasets(const std::string& data_root) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry :
             std::filesystem::directory_iterator(data_root, ec)) {
        if (!entry.is_directory()) continue;
        std::filesystem::path pts = entry.path() / "points.bin";
        if (std::filesystem::exists(pts))
            out.push_back(entry.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Minimal JSON-string escape (only what filenames may contain).
static std::string json_esc(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else                out += c;
    }
    return out;
}

void run(const std::string& data_root, const std::string& dataset,
        const std::string& web_dir,   int port) {
    std::string root = data_root;
    if (!root.empty() && root.back() != '/' && root.back() != '\\')
        root += '/';

    // Resolve the dataset argument into one or more directories:
    //   "all"        → every dataset subdir on disk
    //   "a,b,c"      → the named subdirs
    //   "<name>"     → that single subdir
    std::vector<std::string> dirs;
    std::string display_name = dataset;
    if (dataset == "all") {
        auto names = list_datasets(data_root);
        for (const auto& n : names) dirs.push_back(root + n + "/");
        display_name = "all (" + std::to_string(names.size()) + " datasets)";
        std::cout << "Loading ALL datasets (" << names.size() << "):\n";
        for (const auto& n : names) std::cout << "  • " << n << "\n";
    } else if (dataset.find(',') != std::string::npos) {
        size_t pos = 0;
        std::string rest = dataset;
        while (!rest.empty()) {
            size_t comma = rest.find(',');
            std::string name = rest.substr(0, comma);
            if (!name.empty()) dirs.push_back(root + name + "/");
            if (comma == std::string::npos) break;
            rest.erase(0, comma + 1);
        }
        std::cout << "Loading " << dirs.size() << " selected datasets\n";
    } else {
        dirs.push_back(root + dataset + "/");
        std::cout << "Loading data from " << dirs[0] << " ...\n";
    }

    ApiHandler api(dirs);
    std::cout << "  points="    << api.point_count()
              << "  lines="     << api.line_count()
              << "  admin="     << api.admin_count() << "\n";
    std::cout << "Starting HTTP server on port " << port << " ...\n";

    httplib::Server svr;

    // Static files
    svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(read_file(web_dir + "/index.html"), "text/html");
    });

    svr.Get(R"(/([^/]+\.(js|css|html)))", [&](const httplib::Request& req,
                                               httplib::Response& res) {
        std::string filename = req.matches[1];
        std::string body = read_file(web_dir + "/" + filename);
        if (body.empty()) { res.status = 404; return; }
        res.set_content(body, mime_type(filename));
    });

    // API endpoints
    svr.Get("/api/points", [&](const httplib::Request& req, httplib::Response& res) {
        BBox bbox  = parse_bbox(req);
        int  limit = parse_limit(req, 1000);
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(api.points_geojson(bbox, limit), "application/json");
    });

    svr.Get("/api/lines", [&](const httplib::Request& req, httplib::Response& res) {
        BBox bbox  = parse_bbox(req);
        int  limit = parse_limit(req, 500);
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(api.lines_geojson(bbox, limit), "application/json");
    });

    svr.Get("/api/admin", [&](const httplib::Request& req, httplib::Response& res) {
        BBox bbox = parse_bbox(req);
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(api.admin_geojson(bbox), "application/json");
    });

    // ── Sheet 2 Task 3 + 4: reverse geocoder ──────────────
    svr.Get("/api/reverse", [&](const httplib::Request& req, httplib::Response& res) {
        double lat = 0, lon = 0;
        int    zoom = 15;
        if (req.has_param("lat"))  lat  = std::atof(req.get_param_value("lat").c_str());
        if (req.has_param("lon"))  lon  = std::atof(req.get_param_value("lon").c_str());
        if (req.has_param("zoom")) zoom = std::atoi(req.get_param_value("zoom").c_str());
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(api.reverse_geojson(lat, lon, zoom), "application/json");
    });

    // ── Sheet 3 Task 2: forward geocoder (search) ─────────
    svr.Get("/api/search", [&](const httplib::Request& req, httplib::Response& res) {
        std::string q     = req.has_param("q")     ? req.get_param_value("q")     : "";
        int         limit = req.has_param("limit") ? std::atoi(req.get_param_value("limit").c_str()) : 20;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(api.search_geojson(q, limit), "application/json");
    });

    svr.Get("/api/stats", [&](const httplib::Request&, httplib::Response& res) {
        std::string body =
            "{\"points\":"  + std::to_string(api.point_count()) +
            ",\"lines\":"   + std::to_string(api.line_count())  +
            ",\"admin\":"   + std::to_string(api.admin_count()) + "}";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(body, "application/json");
    });

    // ── Multi-dataset: which one am I serving? ────────────
    svr.Get("/api/info", [&, display_name](const httplib::Request&, httplib::Response& res) {
        std::string body =
            "{\"dataset\":\"" + json_esc(display_name) + "\""
            ",\"points\":"    + std::to_string(api.point_count()) +
            ",\"lines\":"     + std::to_string(api.line_count())  +
            ",\"admin\":"     + std::to_string(api.admin_count()) + "}";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(body, "application/json");
    });

    // ── Multi-dataset: what else is on disk? ──────────────
    svr.Get("/api/datasets", [&, data_root](const httplib::Request&, httplib::Response& res) {
        auto names = list_datasets(data_root);
        std::string body = "{\"current\":\"" + json_esc(dataset) + "\",\"available\":[";
        for (size_t i = 0; i < names.size(); ++i) {
            if (i) body += ",";
            body += "\"" + json_esc(names[i]) + "\"";
        }
        body += "]}";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(body, "application/json");
    });

    std::cout << "Listening on http://localhost:" << port << "\n";
    svr.listen("0.0.0.0", port);
}

} // namespace Server
