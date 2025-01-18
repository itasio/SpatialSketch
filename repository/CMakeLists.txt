cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

project(grid)

set(CMAKE_CXX_FLAGS "-std=c++2a -Wall -Werror -Wunused -Wunused-value -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-write-strings -Wno-maybe-uninitialized -Wno-narrowing -Wno-reorder -Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
#string(APPEND CMAKE_CXX_FLAGS " -O3") # optimizer
#string(APPEND CMAKE_CXX_FLAGS " -pg") # gprof profiler

# Postgresql C++
set(POSTGRESQL_LINK -lpq -lpqxx)

# external code, currently ot in use
include_directories(src/extern/include)
add_library(EXTERN  src/extern/MurmurHash3.cpp
                    src/extern/SHA1.cpp)

# Utilities
include_directories(src/utils/include)
add_library(utils   src/utils/Utils.cpp
                    src/utils/Statistics.cpp
                    src/utils/Postgres.cpp)

# sketches
include_directories(src/spatialsketch/sketches/include)
add_library(sketches    src/spatialsketch/sketches/CountMin.cpp
                        src/spatialsketch/sketches/BloomFilter.cpp
                        src/spatialsketch/sketches/FM.cpp
                        src/spatialsketch/sketches/DyadCountMin.cpp
                        src/spatialsketch/sketches/ECM.cpp)

# ElasticSketch
include_directories(src/ElasticSketch)
add_library(ElasticSketch   src/ElasticSketch/BOBHash32.cpp)
target_link_libraries(ElasticSketch)                       

# SpatialSketch
include_directories(src/spatialsketch/include)
add_library(spatialsketch   src/spatialsketch/Partitioner.cpp
                            src/spatialsketch/SpatialSketch.cpp
                            src/spatialsketch/Messenger.cpp)
target_link_libraries(spatialsketch sketches ElasticSketch utils ${POSTGRESQL_LINK})

# Find and include librdkafka
find_package(PkgConfig REQUIRED)
pkg_check_modules(RDKAFKA REQUIRED rdkafka++)

# Include librdkafka headers
include_directories(${RDKAFKA_INCLUDE_DIRS})
link_directories(${RDKAFKA_LIBRARY_DIRS})
add_definitions(${RDKAFKA_CFLAGS_OTHER})

# MARQ
include_directories(src/MARQ/include)
#add_library(MARQ         src/MARQ/marq.cpp
#                        src/MARQ/countmin.cpp
#                        src/MARQ/prng.cc
#                        src/MARQ/rand48.cc)                       
#add_definitions(-DMARQ)

# Experiments
include_directories(src/other/include) # required for reservoir sampling and multidim cm
add_executable(experiments src/main/experiments.cpp src/other/ReservoirSampling.cpp src/other/MultiDimCM.cpp)
target_link_libraries(experiments spatialsketch) # MARQ

# Example
add_executable(example src/main/example.cpp)
target_link_libraries(example spatialsketch
                                ${RDKAFKA_LIBRARIES})

