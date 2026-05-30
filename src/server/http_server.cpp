#include "http_server.h"
#include "api_handler.h"
#include "httplib.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>

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

void run(const std::string& data_dir, const std::string& web_dir, int port) {
    std::cout << "Loading data from " << data_dir << " ...\n";
    ApiHandler api(data_dir);
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

    svr.Get("/api/stats", [&](const httplib::Request&, httplib::Response& res) {
        std::string body =
            "{\"points\":"  + std::to_string(api.point_count()) +
            ",\"lines\":"   + std::to_string(api.line_count())  +
            ",\"admin\":"   + std::to_string(api.admin_count()) + "}";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(body, "application/json");
    });

    std::cout << "Listening on http://localhost:" << port << "\n";
    svr.listen("0.0.0.0", port);
}

} // namespace Server
