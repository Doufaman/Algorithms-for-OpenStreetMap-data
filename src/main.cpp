#include <iostream>
#include <string>
#include "config.h"
#include "data_extraction_and_storage/data_extraction_storage.h"
#include "server/http_server.h"

int main(int argc, char* argv[]) {
    // ── Mode selection ────────────────────────────────────────
    // Usage:
    //   ./OSM_Geocoder           → parse PBF and write JSON files
    //   ./OSM_Geocoder serve     → load JSON files and start HTTP server
    //
    bool serve_mode = (argc > 1 && std::string(argv[1]) == "serve");

    if (serve_mode) {
        // ── Serve mode ────────────────────────────────────────
        std::string web_dir = std::string(PROJECT_ROOT) + "/src/web";
        Server::run(Config::DATA_DIR, web_dir, 8080);

    }
    else {
        // ── Parse mode ────────────────────────────────────────
        std::ios_base::sync_with_stdio(false);
        std::cout.setf(std::ios::unitbuf);

        DES::OsmData db;
        DES::Stats   stats;

        DES::extraction(Config::PATH_STUTTGART_PBF, db, stats);
        DES::processing(db, stats);
        DES::storage(db, stats, Config::DATA_DIR);
    }

    return 0;
}