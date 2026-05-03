#include <iostream>
#include <osmium/version.hpp>
#include "config.h"
#include "data_extraction_and_storage/data_extraction_storage.h"

int main() {

    std::cout << "DATA_DIR = " << Config::DATA_DIR << std::endl;
    std::cout << "PBF = " << Config::PATH_STUTTGART_PBF << std::endl;

	/*Extract data from PBF file, process it and store it. Only run once, then read data from .json files in the future.*/
    
    // DES::OsmData db;
    // DES::Stats stats;

    // DES::extraction(Config::PATH_STUTTGART_PBF, db, stats);
    // DES::processing(db, stats);
    // DES::storage(db, stats, Config::DATA_DIR);
    

    return 0;
}
