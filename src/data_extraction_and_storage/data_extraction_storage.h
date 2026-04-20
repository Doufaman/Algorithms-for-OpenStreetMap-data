#pragma once
#include <string>

// 声明函数，其他文件 include 此头文件后即可调用
namespace DataExtraction {
    void extractFromPBF(const std::string& pbf_path);
}