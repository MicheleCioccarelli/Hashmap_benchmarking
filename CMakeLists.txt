cmake_minimum_required(VERSION 4.2)
project(HashMaps C)

set(CMAKE_C_STANDARD 11)

option(HASHMAP_COUNT_PROBES "Count insertion and lookup probes in the API hashmap" OFF)

add_library(api_hashmap_normal STATIC src/api_hashmap.c)
target_include_directories(api_hashmap_normal PUBLIC include)

add_library(api_hashmap_probe STATIC src/api_hashmap.c)
target_include_directories(api_hashmap_probe PUBLIC include)
target_compile_definitions(api_hashmap_probe PRIVATE HASHMAP_COUNT_PROBES=1)

add_library(elastic_hashing STATIC src/elastic_hashing.c)
target_include_directories(elastic_hashing PUBLIC include)

# 1. List ALL your.c files here. This tells CMake to compile them together.
add_executable(HashMaps main.c)

# 2. Add the include path. This acts exactly like the `-I./include` compiler flag.
# Now you can just use #include "api_hashmap.h" in your.c files.
target_include_directories(HashMaps PRIVATE include)

# 3. Link the math library. This acts exactly like the `-lm` linker flag.
# The linker knows that "m" stands for "libm", which contains the floor() function.
target_link_libraries(HashMaps m)

if(HASHMAP_COUNT_PROBES)
    target_compile_definitions(HashMaps PRIVATE HASHMAP_COUNT_PROBES=1)
    target_link_libraries(HashMaps api_hashmap_probe)
else()
    target_link_libraries(HashMaps api_hashmap_normal)
endif()

enable_testing()

add_executable(test_api_hashmap tests/test_api_hashmap.c)
target_link_libraries(test_api_hashmap api_hashmap_normal)
add_test(NAME api_hashmap COMMAND test_api_hashmap)

add_executable(test_api_hashmap_probe tests/test_api_hashmap.c)
target_compile_definitions(test_api_hashmap_probe PRIVATE HASHMAP_COUNT_PROBES=1)
target_link_libraries(test_api_hashmap_probe api_hashmap_probe)
add_test(NAME api_hashmap_probe COMMAND test_api_hashmap_probe)

add_executable(test_elastic_partition tests/test_elastic_partition.c)
target_link_libraries(test_elastic_partition elastic_hashing)
add_test(NAME elastic_partition COMMAND test_elastic_partition)
