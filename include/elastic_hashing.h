/// Implementation of Elastic Hashing without resizing: this is accurate to
/// what is described in the paper, you have to know in advance how many elements will be inserted and the
/// \delta that you want to achieve

#ifndef HASHMAPS_ELASTIC_HASHING_H
#define HASHMAPS_ELASTIC_HASHING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "commons.h"
#include "hash_functions.h"

/// Implements the injection phi(i, j)
/// The returned global probe number can be passed directly to siphash_probe64
/// Returns zero when an argument is zero or when phi(i, j) does not fit in 64 bits
uint64_t elastic_phi(uint64_t subarray_number, uint64_t local_probe_number);

/// Returns the finite value of c used in f(epsilon) by this compiled build
/// The paper requires a sufficiently large universal constant but does not prescribe its value
double elastic_c_constant(void);


/// Diagnostics struct
typedef struct ElasticHashmapProbeStats {
    uint64_t insertion_ops;
    uint64_t insertion_probes;
    uint64_t maximum_insertion_probes;
    uint64_t lookup_ops;
    uint64_t lookup_probes;
    uint64_t maximum_lookup_probes;
    uint64_t batch_zero_insertions;
    uint64_t case_one_insertions;
    uint64_t case_one_fallbacks;
    uint64_t case_two_insertions;
    uint64_t case_three_insertions;
    uint64_t batch_zero_probes;
    uint64_t case_one_probes;
    uint64_t case_two_probes;
    uint64_t case_three_probes;
    uint64_t maximum_case_three_probes;
} ElasticHashmapProbeStats;

/// Warning: this is not designed to be expanding, probably none of them should but oh well
typedef struct ElasticHashmap {
    int capacity;
    int size;

    Element** table;
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    ElasticHashmapProbeStats probe_stats;
#endif
} ElasticHashmap;

/// Representation of one of the subarrays which elastic hashing works on.
/// It records the starting index (from the big main array), and its lenght (so the end is start+lenght)
/// as well as the current size, for vacancy calculations
typedef struct ElasticSubArray {
    // One-based i used by phi(i, j)
    int subarray_number;
    int starting_index;
    // capacity
    int length;
    // How many of the key spots are occupied
    int size;
} ElasticSubArray;

/// Returns max(1, ⌈log2(n)⌉), which is the number of subarrays used by elastic hashing
int n_elastic_subarrays(int capacity);

/// Partitions the main array into ⌈log2(n)⌉ subarrays following the paper's instructions
/// Returns NULL if capacity is invalid or allocation fails
ElasticSubArray* partition_elastic_hashmap(int capacity);

/// This is the insertion algorithm as described in the main paper
/// It will fill hashmap until the free fraction is \delta, meaning n − ⌊δn⌋ insertions
/// Each insertion is divided in batches, ...
///
/// delta^-1 is supposed to be a power of 2 for optimal results
/// hashmap must be empty and have size equal to zero
/// elements must contain at least the number of elements required by the batches
/// Every element key must be unique because the paper models insertions rather than update operations
/// The function consumes the element pointers it successfully inserts
/// elements must be an array of pointers owned by the caller
/// Successfully inserted pointers are set to NULL in the input array
/// seed must remain valid for the complete insertion run and must be reused during lookup
///
/// The implementation performs batch B0 and the following batches from the paper
/// Every h_i,j is one SipHash-derived draw in Ai as required by the paper
///
/// Repeated positions are valid probes and count toward the insertion probe limit
void batch_insert(ElasticHashmap* hashmap, float delta, Element** elements, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Runs the same Elastic insertion algorithm with an explicit finite value for c in f(epsilon)
/// Use this only to compare constants because the paper does not identify the smallest valid c
/// c must be finite and positive and every other argument follows the batch_insert ownership rules
void batch_insert_with_c(ElasticHashmap* hashmap, float delta, double c, Element** elements, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Elastic hashing lookup algorithm, searches the one dimensional sequence h_1, h_2 and so on
/// If the probe sequence number that is currently being looked at is part of a certain subarray (is part of the image of phi), then the search
/// will be tighter (the next probe is confined to that subarray)
/// Probe sequence values (k) outside the image of phi are ordinary full-table SipHash probes because the paper leaves them unspecified
///
/// The function does not stop at empty cells because Elastic insertion is non-greedy
/// A temporary bitset records which physical cells were seen so a negative lookup ends after all cells have been inspected, otherwise it is impossible to stop
/// for negative lookups
///
/// Repeated positions still count as probes and can make a negative lookup require more than capacity probes (the hash function can repeat slots)
/// Returns Some with a pointer owned by hashmap when key is present and None otherwise
/// None is also returned when temporary memory allocation fails
/// seed must be the same seed used during insertion
Option_Element_p retrieve_element_elastic_hashmap(const ElasticHashmap* hashmap, const char* key, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Resets all Elastic insertion, lookup and case counters when probe counting is compiled in
/// Counters belong to batch_insert and retrieve_element_elastic_hashmap calls on this map
/// The low-level insert_first_available helper is not associated with a map and is not counted
/// This function does nothing in the normal build
void elastic_hashmap_reset_probe_stats(ElasticHashmap* hashmap);

/// Returns the counters collected since construction or the last reset
/// One probe means one physical table slot inspection and repeated positions count repeatedly
/// maximum_insertion_probes is the largest probe count used by one insertion
/// maximum_lookup_probes is the largest probe count used by one lookup
/// The per-case probe totals partition insertion_probes and maximum_case_three_probes covers only Case 3
/// Every field is zero in the normal build
ElasticHashmapProbeStats elastic_hashmap_probe_stats(const ElasticHashmap* hashmap);

/// Frees every element, key, table and hashmap object owned by hashmap
/// Only call this for a hashmap and elements which were dynamically allocated
void delete_elastic_hashmap(ElasticHashmap* hashmap);

/// Inserts element into the first available slot
/// The caller transfers ownership of element when this function returns true
/// Returns true when element was inserted succesfully and false when the arguments are invalid or the subarray is full
bool insert_first_available(Element** table, ElasticSubArray* subarray, Element* element, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

#endif
