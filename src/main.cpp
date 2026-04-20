#include <iostream>
#include <osmium/version.hpp>
#include "config.h"
#include "data extraction and storage/data_extraction_storage.h"

int main() {
    DataExtraction::extractFromPBF(Config::PBF_INPUT_PATH);
    return 0;
}
