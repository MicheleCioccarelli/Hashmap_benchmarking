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
    uint64_t maximum_insertion_probes;
    uint64_t lookup_ops;
    uint64_t lookup_probes;
    uint64_t maximum_lookup_probes;
} ApiHashmapProbeStats;

/// Hashes key and returns a number in [0, tableSize)
/// This compatibility helper uses the default benchmark SipHash seed
size_t b_hash(size_t tableSize, const char* key);

/// Instantiate an api-style hashmap starting with tableSize spaces. Returns a null pointer if malloc fails
ApiHashmap* create_api_hashmap_with_size(size_t tableSize);
/// Instantiate a control hashmap using the supplied SipHash seed for every initial slot
/// Use the same seed as the experimental hashmap when comparing probe strategies
ApiHashmap* create_api_hashmap_with_size_and_seed(size_t tableSize, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);
/// Instantiate a fixed-capacity control hashmap using the default benchmark seed
ApiHashmap* create_fixed_api_hashmap_with_size(size_t tableSize);
/// Instantiate a fixed-capacity control hashmap using the supplied SipHash seed
/// Automatic and explicit resizing are disabled so benchmark load factors remain comparable
ApiHashmap* create_fixed_api_hashmap_with_size_and_seed(size_t tableSize, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);
/// Instantiate an api-style hashmap starting with 16 spaces
ApiHashmap* create_api_hashmap(void);

/// Deletes all the elements stored, THIS IS NOT A SIMPLE RESIZE
void destroy_api_hashmap(ApiHashmap* hashmap);

/// Returns a pointer to the Element stored in the map with `key` if it exists, otherwise the optional will be None
/// The pointer remains valid across resize because the Element itself is not moved
/// The pointer becomes invalid when the hashmap is destroyed
Option_Element_p retrieve_element_api_hashmap(const ApiHashmap* hashmap, const char* key);

/// Returns by value the Element associated to `key`, safe for storage. Value is none if the element is not present
Option_Element_v retrieve_element_value_api_hashmap(const ApiHashmap* hashmap, const char* key);

/// Inserts or updates an element and returns the stored value, returns NULL if allocation fails
///
/// This function uses `linear probing`
/// Existing keys are updated before checking whether a larger table is needed
Element* insert_element_api_hashmap(ApiHashmap* hashmap, const char* key, int value);

/// Doubles the capacity of the hashmap and inserts old elements back in
bool resize_api_hashmap(ApiHashmap* hashmap);

size_t api_hashmap_size(const ApiHashmap* hashmap);
size_t api_hashmap_capacity(const ApiHashmap* hashmap);

/// Probe counters are updated only when compiled with HASHMAP_COUNT_PROBES
/// Probes used internally to reinsert elements during resize are not included
/// Resets insertion and lookup counters collected by this map
void api_hashmap_reset_probe_stats(ApiHashmap* hashmap);
/// Returns the counters collected since construction or the last reset
/// One probe means one physical table slot inspection and every field is zero in the normal build
/// maximum_insertion_probes is the largest probe count used by one insertion
/// maximum_lookup_probes is the largest probe count used by one lookup
ApiHashmapProbeStats api_hashmap_probe_stats(const ApiHashmap* hashmap);

/// Returns true if op is Some, false otherwise
bool is_some(Option op);
/// Returns true if op is None, false otherwise
bool is_none(Option op);

#endif
