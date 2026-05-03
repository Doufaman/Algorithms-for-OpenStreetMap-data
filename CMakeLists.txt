cmake_minimum_required(VERSION 3.10)
project("Algorithms for OSM")

set(CMAKE_CXX_STANDARD 20)

if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# ── Dependencies ─────────────────────────────────────────────
# libosmium is header-only, no find_package needed
find_package(ZLIB    REQUIRED)
find_package(BZip2   REQUIRED)
find_package(Threads REQUIRED)

# ── Include paths ─────────────────────────────────────────────
include_directories(
    /usr/include          # libosmium headers live here
    src/
)

# ── Sources ───────────────────────────────────────────────────
file(GLOB_RECURSE SOURCES "src/*.cpp")

# ── Executable ────────────────────────────────────────────────
add_executable(OSM_Geocoder ${SOURCES})

target_compile_definitions(OSM_Geocoder PRIVATE
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

if (NOT MSVC)
    target_compile_options(OSM_Geocoder PRIVATE -O2 -DNDEBUG)
endif()

# ── Link libraries ────────────────────────────────────────────
target_link_libraries(OSM_Geocoder
    ZLIB::ZLIB
    BZip2::BZip2
    Threads::Threads
    expat
)