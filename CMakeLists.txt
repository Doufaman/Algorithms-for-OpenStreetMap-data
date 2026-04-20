cmake_minimum_required(VERSION 3.10)
project("Algorithms for OSM")

set(CMAKE_CXX_STANDARD 17)

if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# find dependencies
#find_package(Osmium REQUIRED COMPONENTS pbf)
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)
find_package(Threads REQUIRED)

include_directories(
    /usr/include
    src/
)

# 收集所有源文件
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(OSM_Geocoder ${SOURCES})
add_compile_definitions(PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

target_link_libraries(OSM_Geocoder
    ${OSMIUM_LIBRARIES}
    ZLIB::ZLIB
    BZip2::BZip2
    Threads::Threads
    expat
)