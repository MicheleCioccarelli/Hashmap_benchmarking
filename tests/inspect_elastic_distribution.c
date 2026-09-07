#include "elastic_hashing.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const uint8_t seed[SIPHASH_2_4_KEY_SIZE] = {0};

    for (int capacity = 2; capacity <= 32; capacity++) {
        const int n_insertions = capacity - (int)floor(0.25 * capacity);
        Element stored[32];
        Element* elements[32];
        char keys[32][16];
        Element** table = calloc((size_t)capacity, sizeof(Element*));
        ElasticHashmap hashmap = {.capacity = capacity, .size = 0, .table = table};
        ElasticSubArray* subarrays = NULL;

        if (table == NULL) {
            return 1;
        }

        for (int i = 0; i < n_insertions; i++) {
            snprintf(keys[i], sizeof(keys[i]), "key-%d", i);
            stored[i].key = keys[i];
            stored[i].value = i;
            elements[i] = &stored[i];
        }

        batch_insert(&hashmap, 0.25f, elements, seed);
        subarrays = partition_elastic_hashmap(capacity);
        printf("n=%d inserted=%d subarrays", capacity, hashmap.size);
        for (int i = 0; i < n_elastic_subarrays(capacity); i++) {
            int occupied = 0;
            for (int j = 0; j < subarrays[i].length; j++) {
                occupied += table[subarrays[i].starting_index + j] != NULL;
            }
            printf(" %d/%d", occupied, subarrays[i].length);
        }
        printf("\n");
        free(subarrays);
        free(table);
    }

    return 0;
}
