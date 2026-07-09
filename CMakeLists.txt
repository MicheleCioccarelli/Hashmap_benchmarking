cmake_minimum_required(VERSION 4.2)
project(HashMaps C)

set(CMAKE_C_STANDARD 11)

option(HASHMAP_COUNT_PROBES "Count insertion and lookup probes in the API hashmap" OFF)

# 1. List ALL your.c files here. This tells CMake to compile them together.
add_executable(HashMaps main.c src/api_hashmap.c)

# 2. Add the include path. This acts exactly like the `-I./include` compiler flag.
# Now you can just use #include "api_hashmap.h" in your.c files.
target_include_directories(HashMaps PRIVATE include)

# 3. Link the math library. This acts exactly like the `-lm` linker flag.
# The linker knows that "m" stands for "libm", which contains the floor() function.
target_link_libraries(HashMaps m)

if(HASHMAP_COUNT_PROBES)
    target_compile_definitions(HashMaps PRIVATE HASHMAP_COUNT_PROBES=1)
endif()
