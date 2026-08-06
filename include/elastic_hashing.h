/// Implementation of Elastic Hashing without resizing: this is accurate to
/// what is described in the paper, you have to know in advance how many elements will be inserted and the
/// \delta that you want to achieve

#ifndef HASHMAPS_ELASTIC_HASHING_H
#define HASHMAPS_ELASTIC_HASHING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "commons.h"

#define SIPHASH_2_4_KEY_SIZE 16

/// DO NOT USE THIS DIRECTLY
/// Wrapper for the SipHash implementation (in the third_party dir)
/// The standard implementation SipHash writes its result through an output parameter, this function returns the 64 bit result directly
///
/// The same data and key always return the same result, which lets lookups rebuild the insertion probe sequence
/// Different data can still produce the same result, this is normal for a finite 64 bit hash output
///
/// Use one randomly chosen key as the seed for a complete benchmark run and record it for reproducibility
/// input points to length bytes, they are what is actually hashed => for probing there should be key + probe number
/// key must point to SIPHASH_2_4_KEY_SIZE bytes
/// The caller should mod the returned value to a valid subarray index
uint64_t _siphash_2_4_64(const void* input, size_t length, const uint8_t key[SIPHASH_2_4_KEY_SIZE]);

/// Designed as the hash function for Elastic Hashing probes
/// !! The caller needs to run % subarray_size on the output of the hash function
///
/// Builds the 64 bit value for the one dimensional probe h_probe_number(element_key)
/// probe_number is one-based and must use the same value during insertion and lookup, the subscript in h_i(x)
///
/// The hashed input is the fixed size probe_number followed by the key bytes
/// The function does not choose a subarray, the caller reduces the returned value using the appropriate subarray lenght
/// Use the same seed for every probe in the same benchmark run
uint64_t siphash_probe64(const char* element_key, uint64_t probe_number, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Warning: this is not designed to be expanding, probably none of them should but oh well
typedef struct ElasticHashmap {
    int capacity;
    int size;

    Element** table;
} ElasticHashmap;

/// Representation of one of the subarrays which elastic hashing works on.
/// It records the starting index (from the big main array), and its lenght (so the end is start+lenght)
/// as well as the current size, for vacancy calculations
typedef struct ElasticSubArray {
    int starting_index;
    int lenght;
    int size;
} ElasticSubArray;

/// Returns ⌈log2(n)⌉, which is the number of subarrays used by elastic hashing
int n_elastic_subarrays(int capacity);

/// Partitions the main array into ⌈log2(n)⌉ subarrays following the paper's instructions
/// Returns NULL if capacity is invalid or allocation fails
ElasticSubArray* partition_elastic_hashmap(int capacity);

/// This is the insertion algorithm as described in the main paper
/// It will fill hashmap until the free fraction is \delta, meaning n − ⌊δn⌋ insertions
/// Each insertion is divided in batches, ...
///
/// delta^-1 is supposed to be a power of 2 for optimal results
void batch_insert(ElasticHashmap* hashmap, float delta, Element* elements);

void delete_elastic_hashmap(ElasticHashmap* hashmap);

// Inserts element in subarray using the first available slot, the caller's element pointer should then be invalidated
bool subarray_greedy_insert(Element** table, ElasticSubArray* subarray, Element* element);

#endif
