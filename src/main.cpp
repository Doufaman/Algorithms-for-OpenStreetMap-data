#include <iostream>
#include <osmium/version.hpp>
#include "config.h"
#include "data_extraction_and_storage/data_extraction_storage.h"

int main() {
    DES::extractFromPBF(Config::PATH_STUTTGART_PBF);
    return 0;
}
