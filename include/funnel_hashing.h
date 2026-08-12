//
// Created by miki on 12/08/2026.
//

#ifndef HASHMAPS_FUNNEL_HASHING_H
#define HASHMAPS_FUNNEL_HASHING_H
#include "commons.h"

typedef struct FunnelHashMap {
    int capacity;
    int size;

    Element** table;
} FunnelHashMap;

/// Index-based representation of a funnel hashing bucket (the lowest in the A' hierarchy, which actually contains keys)
/// note: each bucket contains \beta keys
typedef struct Funnel_bucket {
    int subarray_number;
    int starting_index;
    // capacity
    int length;
    // How many of the key spots are occupied
    int size;
} Funnel_bucket;

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


int init_funnel_A1(double delta, int table_size)

#endif //HASHMAPS_FUNNEL_HASHING_H
