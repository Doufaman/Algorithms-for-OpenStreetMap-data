// global configuration for the project

#pragma once
#include <string>

namespace Config {

    // input
    //const std::string DATA_DIR = std::string(PROJECT_ROOT) + "/data/"; //data file
    const std::string DATA_DIR = "/mnt/c/myfiles/stuttgart/OSM/code/Algorithms for OSM/Algorithms for OSM/data/";
    const std::string PATH_STUTTGART_PBF = DATA_DIR + "stuttgart-regbez-260414.osm.pbf";
	const std::string PATH_BADEN_WUERTTEMBERG_PBF = DATA_DIR + "baden-wuerttemberg-260502.osm.pbf";

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

}