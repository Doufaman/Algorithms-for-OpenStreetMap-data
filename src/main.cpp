#include <iostream>
#include <string>
#include "config.h"
#include "data_extraction_and_storage/data_extraction_storage.h"
#include "geo/pip_pipeline.h"
#include "server/http_server.h"

int main(int argc, char* argv[]) {
    // ── Mode selection ────────────────────────────────────────
    // Usage:
    //   ./OSM_Geocoder           → parse PBF, build PIP, write JSON files
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

        //DES::extraction(Config::PATH_STUTTGART_PBF, db, stats);
		DES::extraction(Config::PATH_BADEN_WUERTTEMBERG_PBF, db, stats);
        DES::processing(db, stats);

        // ── Sheet 2 Task 1 (Fast PIP) + Task 4 (Hierarchy) ────
        std::cout << "[3/4] Building PIP + polygon hierarchy ...\n";
        geo::PolygonHierarchy hierarchy;
        geo::build_pip_for_db(db, hierarchy);
        geo::BenchResult bench = geo::benchmark_pip(db, hierarchy);
        geo::print_hierarchy_report(hierarchy, bench);
        geo::print_sample_addresses(db, hierarchy, 5);

        // ── Layered per-tier PIP benchmark ────────────────────
        geo::LayeredBenchResult layered = geo::benchmark_layered_pip(db, hierarchy);
        geo::print_layered_bench(layered);

        // ── Sheet 2 Task 2: attach admin chain to every Point ─
        std::cout << "[3.5/4] Attaching admin attributes to db.points ...\n";
        geo::AttachAdminStats aa = geo::attach_admin_to_db(db, hierarchy);
        geo::print_attach_admin_report(aa);

        DES::storage(db, stats, Config::DATA_DIR);
    }

    return 0;
}