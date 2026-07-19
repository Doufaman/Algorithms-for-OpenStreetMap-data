# Fachpraktikum Algorithms for OpenStreetMap data
Made by: Shu,Ruiyi (Matrikelnummer: 3829877)<br>
email: <a href="mailto:st199686@stud.uni-stuttgart.de">st199686@stud.uni-stuttgart.de</a> <br>
This file introduces how to run the project. Details.md introduces the details of the project, including the structure of the code, the design of the algorithm ... <br>

## Introduction
This project runs in Unbuntu 22.04, made in C++ and CMAKE. <br>

### 1. Dependency Installation
    '''
    sudo apt update
    sudo apt install cmake g++ make
    sudo apt install -y build-essential cmake ninja-build
    sudo apt install -y \
        build-essential \
        cmake \
        libosmium2-dev \
        libprotozero-dev \
        zlib1g-dev \
        libbz2-dev \
        libexpat1-dev
    sudo apt install -y gdb
    '''

### 2. Data Import
PBF files are too big, so please download data from: https://download.geofabrik.de/index.html <br>
Create a folder named "data"<br>
-- data<br>
-- src<br>
Put the downloaded PBF file in the "data" folder. Then modify file paths in "config.h"<br>
-- src<br>
|-- config.h<br>

### 3. Run

All commands below are plain Linux commands (Ubuntu 22.04). If you work
from Windows PowerShell with WSL, wrap each command as
`wsl bash -c "cd '<your_path>' && <command>"`.

#### 3.1 Build

    cd "<your_path>"
    mkdir -p build && cd build
    cmake ..
    make -j

The executable `OSM_Geocoder` is created in the `build` folder.
New source files are picked up automatically on the next `make`
(no need to re-run cmake).

#### 3.2 Parse (data processing)

Parse the default PBF configured in `src/config.h`:

    ./OSM_Geocoder

Or parse any specific PBF file — a bare filename is resolved relative
to the `data/` folder:

    ./OSM_Geocoder bayern-260717.osm.pbf
    ./OSM_Geocoder /absolute/path/to/some-region.osm.pbf

Each parse writes its output to its own subfolder `data/<dataset>/`
(binary files `points.bin`, `lines.bin`, `admin_areas.bin`), so
different regions never overwrite each other. A timestamped benchmark
report is saved to `data/benchmark/<dataset>_<time>.md`.

To parse several regions in a row:

    for f in bremen-260717.osm.pbf hamburg-260717.osm.pbf; do
        ./OSM_Geocoder "$f"
    done

#### 3.3 Frontend (server)

Serve the default dataset (see `DEFAULT_SERVE_DATASET` in `src/config.h`):

    ./OSM_Geocoder serve

Serve one specific dataset:

    ./OSM_Geocoder serve bayern-260717

Serve several datasets merged, or all parsed datasets at once:

    ./OSM_Geocoder serve bremen-260717,hamburg-260717
    ./OSM_Geocoder serve all

Then open **http://localhost:8080** in a browser. The header shows the
loaded dataset; the dropdown lists all datasets available on disk.
Stop the server with `Ctrl+C`.
