#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include "config.h"
#include "data_extraction_and_storage/data_extraction_storage.h"
#include "geo/pip_pipeline.h"
#include "geocoder/normalizer.h"
#include "io/benchmark_log.h"
#include "server/http_server.h"

int main(int argc, char* argv[]) {
    // ── Mode selection ────────────────────────────────────────
    // Usage:
    //   ./OSM_Geocoder                     → parse the default PBF (below)
    //   ./OSM_Geocoder <pbf-path>          → parse the given PBF file
    //   ./OSM_Geocoder serve               → serve the DEFAULT_SERVE_DATASET
    //   ./OSM_Geocoder serve <dataset>     → serve data/<dataset>/
    //   ./OSM_Geocoder serve all           → merge-load EVERY dataset on disk
    //   ./OSM_Geocoder serve a,b,c         → merge-load a chosen subset
    //   ./OSM_Geocoder test-normalizer     → run Sheet 3 T1 self-test only
    //
    std::string mode = (argc > 1) ? argv[1] : "";
    bool serve_mode      = (mode == "serve");
    bool normalizer_test = (mode == "test-normalizer");

    if (normalizer_test) {
        return geocoder::run_normalizer_tests();
    }

    if (serve_mode) {
        // ── Serve mode ────────────────────────────────────────
        std::string dataset = (argc > 2 && argv[2][0] != '\0')
                              ? std::string(argv[2])
                              : Config::DEFAULT_SERVE_DATASET;
        std::string data_dir = Config::dataset_dir(dataset);
        std::string web_dir  = std::string(PROJECT_ROOT) + "/src/web";

        std::cout << "▶ Serve dataset : " << dataset << "\n"
                  << "▶ Data dir      : " << data_dir << "\n";
        Server::run(Config::DATA_DIR, dataset, web_dir, 8080);

    }
    else {
        // ── Parse mode ────────────────────────────────────────
        std::ios_base::sync_with_stdio(false);
        std::cout.setf(std::ios::unitbuf);

        // Source PBF: taken from the command line when given
        // (./OSM_Geocoder <path-or-filename>), otherwise the default below.
        // A bare filename (no slash) is resolved relative to DATA_DIR.
        std::string pbf = Config::PATH_BADEN_WUERTTEMBERG_PBF;
        if (!mode.empty()) {
            pbf = mode;
            if (pbf.find('/') == std::string::npos &&
                pbf.find('\\') == std::string::npos)
                pbf = Config::DATA_DIR + pbf;
        }
        if (!std::filesystem::exists(pbf)) {
            std::cerr << "ERROR: PBF not found: " << pbf << "\n";
            return 1;
        }

        // Derive dataset name + output directory from the PBF file name.
        // Ensures parses of different PBFs never overwrite each other.
        std::string dataset = Config::dataset_name_for_pbf(pbf);
        std::string out_dir = Config::dataset_dir(dataset);
        std::error_code ec;
        std::filesystem::create_directories(out_dir, ec);

        std::cout << "▶ Source PBF    : " << pbf     << "\n"
                  << "▶ Dataset name  : " << dataset << "\n"
                  << "▶ Output dir    : " << out_dir << "\n\n";

        auto t_start = std::chrono::system_clock::now();

        DES::OsmData db;
        DES::Stats   stats;

        DES::extraction(pbf, db, stats);
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

        DES::storage(db, stats, out_dir);

        auto t_end = std::chrono::system_clock::now();

        // ── Save a human-readable benchmark report ─────────────
        bench::ParseReport report;
        report.dataset         = dataset;
        report.pbf_path        = pbf;
        report.start_time      = t_start;
        report.end_time        = t_end;
        report.des_stats       = &stats;
        report.hierarchy_stats = &hierarchy.stats();
        report.pip_bench       = &bench;
        report.layered_bench   = &layered;
        report.attach_stats    = &aa;

        std::string bench_dir = Config::DATA_DIR + "benchmark/";
        std::string written   = bench::write_markdown(report, bench_dir);
        if (!written.empty())
            std::cout << "\n▶ Benchmark report written: " << written << "\n";
    }

    return 0;
}