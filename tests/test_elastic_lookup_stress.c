#include "elastic_hashing.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    enum { MAX_CAPACITY = 512 };
    static const float deltas[] = {0.5f, 0.25f, 0.125f};
    char keys[MAX_CAPACITY][48];
    Element stored[MAX_CAPACITY];
    Element* elements[MAX_CAPACITY];

    for (int seed_number = 0; seed_number < 3; seed_number++) {
        uint8_t seed[SIPHASH_2_4_KEY_SIZE];
        for (int i = 0; i < SIPHASH_2_4_KEY_SIZE; i++) {
            seed[i] = (uint8_t)(seed_number * SIPHASH_2_4_KEY_SIZE + i);
        }

        for (size_t delta_index = 0; delta_index < sizeof(deltas) / sizeof(deltas[0]); delta_index++) {
            const float delta = deltas[delta_index];
            for (int capacity = 1; capacity <= MAX_CAPACITY; capacity++) {
                const int key_count = capacity - (int)floor((double)delta * (double)capacity);
                Element** table = calloc((size_t)capacity, sizeof(Element*));
                assert(table != NULL);

                for (int i = 0; i < key_count; i++) {
                    snprintf(keys[i], sizeof(keys[i]), "stress-%d-%zu-%d-%d", seed_number, delta_index, capacity, i);
                    stored[i] = (Element){.key = keys[i], .value = i};
                    elements[i] = &stored[i];
                }

                ElasticHashmap hashmap = {.capacity = capacity, .size = 0, .table = table};
                batch_insert(&hashmap, delta, elements, seed);
                assert(hashmap.size == key_count);

                for (int i = 0; i < key_count; i++) {
                    assert(elements[i] == NULL);
                    const Option_Element_p result = retrieve_element_elastic_hashmap(&hashmap, keys[i], seed);
                    assert(result.option == Some);
                    assert(result.element_p == &stored[i]);
                }

                const Option_Element_p missing = retrieve_element_elastic_hashmap(&hashmap, "missing-stress-key", seed);
                assert(missing.option == None);
                free(table);
            }
        }
    }

    return 0;
}
