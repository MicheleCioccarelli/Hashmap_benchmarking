#include "elastic_hashing.h"

#include <stdbool.h>
#include <stdlib.h>

int n_elastic_subarrays(const int capacity) {
    if (capacity <= 1) {
        return 1;
    }

    int log = 0;
    int power = 1;
    while (power < capacity) {
        power *= 2;
        log++;
    }

    return log;
}

ElasticSubArray* partition_elastic_hashmap(const int capacity) {
    if (capacity <= 0) {
        return NULL;
    }

    const int subarray_count = n_elastic_subarrays(capacity);
    ElasticSubArray* subarrays = malloc(sizeof(ElasticSubArray) * subarray_count);
    if (subarrays == NULL) {
        return NULL;
    }

    int prev_len = 0;
    int remaining_len = capacity;

    for (int i = 0; i < subarray_count; i++) {
        const bool is_last_subarray = (i == subarray_count - 1);
        int current_len = (remaining_len + 1) / 2;

        if (is_last_subarray) {
            // The paper allows +-1 so the partition covers all of A exactly
            current_len = remaining_len;
        }

        subarrays[i].starting_index = prev_len;
        subarrays[i].lenght = current_len;
        subarrays[i].size = 0;

        prev_len += current_len;
        remaining_len -= current_len;
    }

    return subarrays;
}

void batch_insert(ElasticHashmap* hashmap, float delta, Element* elements) {
    if (hashmap == NULL) {
        return;
    }

    // Allocate space to store all of the log(N) subarrays
    const int n_subarrays = n_elastic_subarrays(hashmap->capacity);
    ElasticSubArray* subarrays = partition_elastic_hashmap(hashmap->capacity);
    if (subarrays == NULL) {
        return;
    }

    free(subarrays);
}


void delete_elastic_hashmap(ElasticHashmap* hashmap) {
    if (hashmap != NULL) {
        for (int i = 0; i < hashmap->capacity; i++) {
            if (hashmap->table[i] != NULL) {
                free((char*)hashmap->table[i]->key);
                free(hashmap->table[i]);
            }
        }
        free(hashmap->table);
        free(hashmap);
    }
}
