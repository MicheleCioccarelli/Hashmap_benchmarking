#include "elastic_hashing.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    enum { CAPACITY = 64, KEY_COUNT = 48 };
    char keys[KEY_COUNT][32];
    Element stored[KEY_COUNT];
    Element* elements[KEY_COUNT];
    const uint8_t seed[SIPHASH_2_4_KEY_SIZE] = {0};
    Element* table[CAPACITY] = {NULL};
    ElasticHashmap hashmap = {
        .capacity = CAPACITY,
        .size = 0,
        .table = table,
    };

    for (int i = 0; i < KEY_COUNT; i++) {
        snprintf(keys[i], sizeof(keys[i]), "lookup-key-%d", i);
        stored[i].key = keys[i];
        stored[i].value = i;
        elements[i] = &stored[i];
    }

    batch_insert(&hashmap, 0.25f, elements, seed);
    assert(hashmap.size == KEY_COUNT);

    for (int i = 0; i < KEY_COUNT; i++) {
        assert(elements[i] == NULL);
        const Option_Element_p result = retrieve_element_elastic_hashmap(&hashmap, keys[i], seed);
        assert(result.option == Some);
        assert(result.element_p == &stored[i]);
    }

    const Option_Element_p missing = retrieve_element_elastic_hashmap(&hashmap, "not-present", seed);
    assert(missing.option == None);
    assert(missing.element_p == NULL);
    return 0;
}
