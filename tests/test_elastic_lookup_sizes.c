#include "elastic_hashing.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const uint8_t seed[SIPHASH_2_4_KEY_SIZE] = {0};

    for (int capacity = 1; capacity <= 64; capacity++) {
        const int n_insertions = capacity - (int)floor(0.25 * capacity);
        Element stored[64];
        Element* elements[64];
        char keys[64][16];
        Element** table = calloc((size_t)capacity, sizeof(Element*));
        assert(table != NULL);

        for (int i = 0; i < n_insertions; i++) {
            snprintf(keys[i], sizeof(keys[i]), "key-%d", i);
            stored[i].key = keys[i];
            stored[i].value = i;
            elements[i] = &stored[i];
        }

        ElasticHashmap hashmap = {
            .capacity = capacity,
            .size = 0,
            .table = table,
        };

        batch_insert(&hashmap, 0.25f, elements, seed);
        assert(hashmap.size == n_insertions);

        for (int i = 0; i < n_insertions; i++) {
            const Option_Element_p result = retrieve_element_elastic_hashmap(&hashmap, keys[i], seed);
            assert(result.option == Some);
            assert(result.element_p == &stored[i]);
        }

        const Option_Element_p missing = retrieve_element_elastic_hashmap(&hashmap, "not-present", seed);
        assert(missing.option == None);
        free(table);
    }

    return 0;
}
