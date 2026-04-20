#include "data_extraction_storage.h"
#include "config.h" //global config
#include <iostream>

// 仅在此文件实现
namespace DataExtraction {
    void extractFromPBF(const std::string& pbf_path) {
        std::cout << "Reading: " << pbf_path << "\n";
        // TODO: libosmium 读取逻辑
    }
}