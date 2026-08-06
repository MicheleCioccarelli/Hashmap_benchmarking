cmake_minimum_required(VERSION 4.2)
project(HashMaps C)

set(CMAKE_C_STANDARD 11)

option(HASHMAP_COUNT_PROBES "Count insertion and lookup probes in the API hashmap" OFF)

add_library(api_hashmap_normal STATIC src/api_hashmap.c)
target_include_directories(api_hashmap_normal PUBLIC include)

add_library(api_hashmap_probe STATIC src/api_hashmap.c)
target_include_directories(api_hashmap_probe PUBLIC include)
target_compile_definitions(api_hashmap_probe PRIVATE HASHMAP_COUNT_PROBES=1)

add_library(siphash STATIC third_party/siphash.c)
target_include_directories(siphash PUBLIC third_party)

add_library(elastic_hashing STATIC src/elastic_hashing.c)
target_include_directories(elastic_hashing PUBLIC include)
target_link_libraries(elastic_hashing PRIVATE siphash)

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
