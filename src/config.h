// global configuration for the project

#pragma once
#include <string>

namespace Config {

    // input
    //const std::string DATA_DIR = std::string(PROJECT_ROOT) + "/data/"; //data file
    const std::string DATA_DIR = "/mnt/c/myfiles/stuttgart/OSM/code/Algorithms for OSM/Algorithms for OSM/data/";
    const std::string PATH_STUTTGART_PBF = DATA_DIR + "stuttgart-regbez-260414.osm.pbf";
	const std::string PATH_BADEN_WUERTTEMBERG_PBF = DATA_DIR + "baden-wuerttemberg-260502.osm.pbf";
	const std::string PATH_BAYERN_PBF = DATA_DIR + "bayern-260717.osm.pbf";
	const std::string PATH_GERMANY_PBF = DATA_DIR + "germany-latest.osm.pbf";
	const std::string PATH_EUROPE_PBF  = DATA_DIR + "europe-latest.osm.pbf";

    // output (JSON, legacy — still written for compatibility)
    //const std::string SAMPLE_DATA_DIR = "/mnt/c/myfiles/stuttgart/OSM/code/Algorithms for OSM/Algorithms for OSM/data/";

    // output (binary, preferred for fast loading)
    const std::string POINTS_BIN = DATA_DIR + "points.bin";
    const std::string LINES_BIN  = DATA_DIR + "lines.bin";
    const std::string ADMIN_BIN  = DATA_DIR + "admin_areas.bin";

    // Legacy aliases (kept so existing references still compile)
    const std::string HOUSES_BIN = POINTS_BIN;

    // ── Output toggles ────────────────────────────────────────
    // Set true to also write the legacy JSON files alongside binary.
    // JSON is ~10× larger and ~3× slower to write; only enable when you need human-readable output for debugging.
    constexpr bool STORE_JSON = false;
    constexpr bool STORE_BIN  = true;

    // ── Per-dataset directory helpers ─────────────────────────
    // Multiple PBFs can be parsed side-by-side without collisions;
    // each dataset's binaries live under DATA_DIR/<name>/.
    //   dataset_name_for_pbf("...germany-latest.osm.pbf") → "germany-latest"
    //   dataset_dir("germany-latest") → "DATA_DIR/germany-latest/"
    inline std::string dataset_name_for_pbf(const std::string& pbf_path) {
        size_t slash = pbf_path.find_last_of("/\\");
        std::string base = (slash == std::string::npos)
                         ? pbf_path
                         : pbf_path.substr(slash + 1);
        // Strip either ".osm.pbf" or ".pbf"
        auto ends = [](const std::string& s, const std::string& suf) {
            return s.size() >= suf.size() &&
                   s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
        };
        if (ends(base, ".osm.pbf")) base.resize(base.size() - 8);
        else if (ends(base, ".pbf")) base.resize(base.size() - 4);
        return base;
    }
    inline std::string dataset_dir(const std::string& dataset_name) {
        return DATA_DIR + dataset_name + "/";
    }

    // Default dataset for `./OSM_Geocoder serve` with no arg.
    // Switch to "germany-latest" once you have a Germany-scale parse.
    const std::string DEFAULT_SERVE_DATASET = "baden-wuerttemberg-260502";

}