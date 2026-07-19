/// Implementation of Elastic Hashing without resizing: this is accurate to
/// what is described in the paper, you have to know in advance how many elements will be inserted and the
/// \delta that you want to achieve

#ifndef HASHMAPS_ELASTIC_HASHING_H
#define HASHMAPS_ELASTIC_HASHING_H
#include "commons.h"

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

#endif
