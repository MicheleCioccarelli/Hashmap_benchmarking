#include "elastic_hashing.h"

#include <assert.h>
#include <stdlib.h>

int main(void) {
    Element first = {.key = "first", .value = 1};
    Element second = {.key = "second", .value = 2};
    Element third = {.key = "third", .value = 3};
    Element* elements[] = {&first, &second, &third};
    const uint8_t seed[SIPHASH_2_4_KEY_SIZE] = {0};

    Element* table[4] = {NULL};
    ElasticHashmap hashmap = {
        .capacity = 4,
        .size = 0,
        .table = table,
    };

    batch_insert(&hashmap, 0.5f, elements, seed);

    assert(hashmap.size == 2);
    assert(elements[0] == NULL);
    assert(elements[1] == NULL);
    assert(elements[2] == &third);
    assert(table[0] != NULL);
    assert(table[1] != NULL);
    return 0;
}
