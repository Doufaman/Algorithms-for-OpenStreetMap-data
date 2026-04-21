// global configuration for the project

#pragma once
#include <string>

namespace Config {

    // CMAKE_SOURCE_DIR 对应项目根目录，在编译时由 CMake 传入
    // 运行时用相对路径或绝对路径

    // input
    const std::string DATA_DIR = std::string(PROJECT_ROOT) + "/data/"; //data file
    const std::string PATH_STUTTGART_PBF = DATA_DIR + "stuttgart-regbez-260414.osm.pbf";

    // output
    const std::string SAMPLE_DATA_DIR = "/mnt/c/myfiles/stuttgart/OSM/code/Algorithms for OSM/Algorithms for OSM/data/";
    const std::string HOUSES_BIN = DATA_DIR + "houses.bin";
    const std::string ADMIN_BIN = DATA_DIR + "admin.bin";

}