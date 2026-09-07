#include "elastic_hashing.h"

#include <assert.h>

int main(void) {
    static const char* keys[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven"};
    Element stored[12];
    Element* elements[12];
    const uint8_t seed[SIPHASH_2_4_KEY_SIZE] = {0};

    for (int i = 0; i < 12; i++) {
        stored[i].key = keys[i];
        stored[i].value = i;
        elements[i] = &stored[i];
    }

    Element* table[16] = {NULL};
    ElasticHashmap hashmap = {
        .capacity = 16,
        .size = 0,
        .table = table,
    };

    batch_insert(&hashmap, 0.25f, elements, seed);

    assert(hashmap.size == 12);
    for (int i = 0; i < 12; i++) {
        assert(elements[i] == NULL);
    }

    return 0;
}
