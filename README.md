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
Run main.cpp and follow the instruction.<br>
Run 

    "/home/dfm/.vs/<your file>/out/build/linux-debug/OSM_Geocoder" serve

    or

    cd "/home/dfm/.vs/<your file>/out/build/linux-debug"
    ./OSM_Geocoder serve
