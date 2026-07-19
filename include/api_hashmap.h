#ifndef HASHMAPS_API_HASHMAP_H
#define HASHMAPS_API_HASHMAP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "commons.h"

typedef struct api_hashmap ApiHashmap;

typedef struct ApiHashmapProbeStats {
    uint64_t insertion_ops;
    uint64_t insertion_probes;
    uint64_t lookup_ops;
    uint64_t lookup_probes;
} ApiHashmapProbeStats;

/// Hashes key and returns a number in [0, tableSize)
size_t b_hash(size_t tableSize, const char* key);

/// Instantiate an api-style hashmap starting with tableSize spaces. Returns a null pointer if malloc fails
ApiHashmap* create_api_hashmap_with_size(size_t tableSize);
/// Instantiate an api-style hashmap starting with 16 spaces
ApiHashmap* create_api_hashmap(void);

/// Deletes all the elements stored, THIS IS NOT A SIMPLE RESIZE
void destroy_api_hashmap(ApiHashmap* hashmap);

/// Returns a pointer to the Element stored in the map with `key` if it exists, otherwise the optional will be None
/// This is dangerous: whenever the table changes size, all the old pointers will be invalid.
/// Only use this if you are modifying this pointer right away and then discarding it
Option_Element_p retrieve_element_api_hashmap(const ApiHashmap* hashmap, const char* key);

/// Returns by value the Element associated to `key`, safe for storage. Value is none if the element is not present
Option_Element_v retrieve_element_value_api_hashmap(const ApiHashmap* hashmap, const char* key);

/// Inserts or updates an element and returns the stored value, returns NULL if allocation fails
///
/// This function uses `linear probing`
Element* insert_element_api_hashmap(ApiHashmap* hashmap, const char* key, int value);

/// Doubles the capacity of the hashmap and inserts old elements back in
bool resize_api_hashmap(ApiHashmap* hashmap);

size_t api_hashmap_size(const ApiHashmap* hashmap);
size_t api_hashmap_capacity(const ApiHashmap* hashmap);

/// Probe counters are updated only when compiled with HASHMAP_COUNT_PROBES
void api_hashmap_reset_probe_stats(ApiHashmap* hashmap);
ApiHashmapProbeStats api_hashmap_probe_stats(const ApiHashmap* hashmap);

/// Returns true if op is Some, false otherwise
bool is_some(Option op);
/// Returns true if op is None, false otherwise
bool is_none(Option op);

#endif
