//
// Created by miki on 12/08/2026.
//

#ifndef HASHMAPS_FUNNEL_HASHING_H
#define HASHMAPS_FUNNEL_HASHING_H
#include <stdbool.h>
#include <stdint.h>

#include "commons.h"

typedef struct FunnelHashmapProbeStats {
    uint64_t insertion_ops;
    uint64_t insertion_probes;
    uint64_t maximum_insertion_probes;
    uint64_t lookup_ops;
    uint64_t lookup_probes;
    uint64_t maximum_lookup_probes;
    uint64_t insertions_in_a;
    uint64_t insertions_in_b;
    uint64_t insertions_in_c;
    uint64_t insertion_failures;
} FunnelHashmapProbeStats;

typedef struct FunnelHashMap {
    int capacity;
    int size;

    Element** table;
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    FunnelHashmapProbeStats probe_stats;
#endif
} FunnelHashMap;

/// Index-based representation of the bucket containers (A_1, A_2, ...)
/// Each bucket is \beta long, so when traversing each member of this array is a step of \beta
/// in the actual array which contains the table
/// ending_indexes starting_index + \beta*n_buckets (hope fence post problme doesn't bite me in the ass)
typedef struct Funnel_A_i {
    int subarray_number;
    int starting_index;
    // capacity
    int n_buckets;
} Funnel_A_i;

/// Representation of the B array, normal uniform probing is applied log logn times
typedef struct Funnel_B {
    int starting_index;
    // capacity
    int length;
    // How many of the key spots are occupied
    int size;
} Funnel_B;

/// Index-based representation of the C array, it contains buckets and is accessed as a 2-choice table (bins and bucket theorem)
/// warning: might be useless
typedef struct Funnel_C {
    int starting_index;
    // capacity
    int length;
    // How many of the key spots are occupied
    int size;
} Funnel_C;

/// Index-based representation of a bucket used by the 2-choice table C in funnel hashing
/// in practice it works as a normal bucket and contains 2loglogn keys
typedef struct Funnel_Ci_bucket {
    int subarray_number;
    int starting_index;
    // How many of the key spots are occupied
    int size;
} Funnel_Ci_bucket;

/// Owns the index descriptions needed to partition one Funnel Hashing table
/// It does not own the main Element table and should be freed with delete_funnel_partition
typedef struct FunnelPartition {
    // Number of geometrically decreasing Ai subarrays inside A'
    int alpha;
    // Number of physical slots in every Aij bucket
    int beta;
    // Number of physical slots covered by A1 through Aalpha
    int a_prime_length;
    // Number of physical slots in A_(alpha+1), which is split into B and C
    int a_alpha_plus_one_length;
    // ceil(log2(log2(capacity))) probes are attempted in B
    int b_probe_limit;
    // target size ceil(2log2(log2(capacity))) for every bucket in C
    int c_bucket_length;
    // Number of equal c_bucket_length buckets covering C
    int c_bucket_count;

    // alpha descriptions covering A' without separate bucket allocations
    Funnel_A_i* subarrays;
    Funnel_Ci_bucket* c_buckets;
    Funnel_B b;
    Funnel_C c;
} FunnelPartition;

/// Returns alpha = ceil(4 log2(delta^-1)) + 10 from the paper
/// Returns zero unless delta is in the paper's assumed interval (0, 1/8]
int funnel_alpha(double delta);

/// Returns beta = ceil(2 log2(delta^-1)) from the paper
/// Returns zero unless delta is in the paper's assumed interval (0, 1/8]
int funnel_beta(double delta);

/// Builds the index-only Funnel Hashing partition described in Section 3 of the paper
/// A' is divisible by beta and every Ai contains an integer number of beta-sized buckets
/// The number of buckets follows a_(i+1) = 3a_i/4 +- 1 and sums exactly to |A'|/beta
/// B and C split A_(alpha+1) into equal sizes +- 1
/// Returns NULL when the paper's size constraints cannot be satisfied or allocation fails
FunnelPartition* partition_funnel_hashmap(int capacity, double delta);

/// Frees a partition returned by partition_funnel_hashmap
void delete_funnel_partition(FunnelPartition* partition);

/// Allocates an empty Funnel hashmap with the requested fixed capacity
/// Returns NULL when capacity is invalid or allocation fails
FunnelHashMap* create_funnel_hashmap(int capacity);

/// Inserts one unique element by greedily following A1 through Aalpha, B and C
/// Ownership of element and its key moves to hashmap only when the function returns true
/// Returns false for invalid input, allocation failure or the high-probability failure of the C table
bool insert_element_funnel_hashmap(FunnelHashMap* hashmap, double delta, Element* element, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Inserts capacity - floor(delta capacity) unique elements using the complete Funnel sequence
/// The hashmap and its table must be empty and delta must satisfy the paper's assumptions
/// elements must contain enough dynamically allocated Element pointers with dynamically allocated keys
/// Successfully inserted pointers become owned by hashmap and are set to NULL in elements
/// Returns false for invalid input, allocation failure or the high-probability failure of the C table
bool batch_insert_funnel_hashmap(FunnelHashMap* hashmap, double delta, Element** elements, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Greedily follows the same A1 through Aalpha, B and C probe sequence used during insertion
/// In every Ai it recomputes the key's selected Aij bucket and checks its beta slots in order
/// It then recomputes the bounded B draws and finally alternates through the two selected C buckets
/// The first empty position proves absence because Funnel Hashing is greedy and does not support deletion
/// A key stored later in the sequence must have seen every earlier candidate occupied during insertion
/// Those candidates remain occupied because deletion and relocation are not implemented
/// Lookup stores no subarray, bucket or placement metadata for individual keys
/// Returns Some with a pointer owned by hashmap when key is present and None otherwise
/// delta and seed must be the same values used during insertion
Option_Element_p retrieve_element_funnel_hashmap(const FunnelHashMap* hashmap, double delta, const char* key, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Runs Funnel lookup using a partition which the caller has already constructed
/// Use this for repeated lookups so partition allocation and rounding are paid only once
/// partition must describe hashmap capacity and the same delta used during insertion
/// The caller retains ownership of partition and may reuse it for every lookup on the map
/// The probe sequence and probe counters are identical to retrieve_element_funnel_hashmap
Option_Element_p retrieve_element_funnel_hashmap_with_partition(const FunnelHashMap* hashmap, const FunnelPartition* partition, const char* key, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Resets all Funnel insertion, lookup and destination counters when probe counting is compiled in
/// Counters belong to the single, batch and retrieve calls made on this map
/// This function does nothing in the normal build
void funnel_hashmap_reset_probe_stats(FunnelHashMap* hashmap);

/// Returns the counters collected since construction or the last reset
/// One probe means one physical table slot inspection and repeated positions count repeatedly
/// maximum_insertion_probes is the largest probe count used by one insertion
/// maximum_lookup_probes is the largest probe count used by one lookup
/// Every field is zero in the normal build
FunnelHashmapProbeStats funnel_hashmap_probe_stats(const FunnelHashMap* hashmap);

/// Frees every element, key, table and hashmap object owned by hashmap
/// Only call this for a hashmap and elements which were dynamically allocated
void delete_funnel_hashmap(FunnelHashMap* hashmap);

#endif //HASHMAPS_FUNNEL_HASHING_H
