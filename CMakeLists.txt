cmake_minimum_required(VERSION 3.20)
project(HashMaps C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(HASHMAP_SOURCES
    main.c
    src/benchmark_cli.c
    src/api_hashmap.c
    src/elastic_hashing.c
    src/funnel_hashing.c
    src/hash_functions.c
    src/wordlist_runner.c
    third_party/siphash.c
)

add_executable(HashMaps ${HASHMAP_SOURCES})
target_include_directories(HashMaps PRIVATE include third_party)
target_link_libraries(HashMaps PRIVATE m)

add_executable(HashMapsProbes ${HASHMAP_SOURCES})
target_include_directories(HashMapsProbes PRIVATE include third_party)
target_compile_definitions(HashMapsProbes PRIVATE HASHMAP_COUNT_PROBES=1)
target_link_libraries(HashMapsProbes PRIVATE m)
