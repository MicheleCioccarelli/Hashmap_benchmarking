#include "elastic_hashing.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Allocates one independent copy of the same deterministic element sequence
static Element** create_elements(const int count) {
    Element** elements = calloc((size_t)count, sizeof(Element*));
    assert(elements != NULL);
    for (int i = 0; i < count; i++) {
        char generated_key[64];
        const int written = snprintf(generated_key, sizeof(generated_key), "explicit-c-%d", i);
        assert(written > 0 && (size_t)written < sizeof(generated_key));
        const size_t key_length = (size_t)written + 1;
        char* key = malloc(key_length);
        elements[i] = malloc(sizeof(Element));
        assert(key != NULL && elements[i] != NULL);
        memcpy(key, generated_key, key_length);
        *elements[i] = (Element){.key = key, .value = i};
    }
    return elements;
}

/// Allocates one empty map with the public fixed-capacity representation
static ElasticHashmap* create_hashmap(const int capacity) {
    ElasticHashmap* hashmap = calloc(1, sizeof(ElasticHashmap));
    assert(hashmap != NULL);
    *hashmap = (ElasticHashmap){.capacity = capacity, .table = calloc((size_t)capacity, sizeof(Element*))};
    assert(hashmap->table != NULL);
    return hashmap;
}

int main(void) {
    enum { CAPACITY = 1024 };
    const float delta = 0.125f;
    const int count = CAPACITY - (int)floor((double)delta * (double)CAPACITY);
    const uint8_t seed[SIPHASH_2_4_KEY_SIZE] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };

    ElasticHashmap* default_hashmap = create_hashmap(CAPACITY);
    ElasticHashmap* explicit_hashmap = create_hashmap(CAPACITY);
    Element** default_elements = create_elements(count);
    Element** explicit_elements = create_elements(count);
    batch_insert(default_hashmap, delta, default_elements, seed);
    batch_insert_with_c(explicit_hashmap, delta, elastic_c_constant(), explicit_elements, seed);
    assert(default_hashmap->size == count);
    assert(explicit_hashmap->size == count);

    for (int slot = 0; slot < CAPACITY; slot++) {
        Element* default_element = default_hashmap->table[slot];
        Element* explicit_element = explicit_hashmap->table[slot];
        assert((default_element == NULL) == (explicit_element == NULL));
        if (default_element != NULL) {
            assert(default_element->value == explicit_element->value);
            assert(strcmp(default_element->key, explicit_element->key) == 0);
        }
    }

#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const ElasticHashmapProbeStats default_stats = elastic_hashmap_probe_stats(default_hashmap);
    const ElasticHashmapProbeStats explicit_stats = elastic_hashmap_probe_stats(explicit_hashmap);
    assert(default_stats.insertion_ops == explicit_stats.insertion_ops);
    assert(default_stats.insertion_probes == explicit_stats.insertion_probes);
    assert(default_stats.maximum_insertion_probes == explicit_stats.maximum_insertion_probes);
    assert(default_stats.batch_zero_probes + default_stats.case_one_probes + default_stats.case_two_probes + default_stats.case_three_probes == default_stats.insertion_probes);
#endif

    free(default_elements);
    free(explicit_elements);
    delete_elastic_hashmap(default_hashmap);
    delete_elastic_hashmap(explicit_hashmap);
    return 0;
}
