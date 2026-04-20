#pragma once
#include <string>

// DES = Data Extraction and Storage
// read, process and store data from OSM PBF files
namespace DES {
    void extractFromPBF(const std::string& pbf_path);
}

