#include "funnel_hashing.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Allocates one key owned by the Funnel hashmap after successful insertion
static Element* create_element(const int value) {
    Element* element = malloc(sizeof(Element));
    char* key = malloc(48);
    assert(element != NULL);
    assert(key != NULL);

    snprintf(key, 48, "funnel-key-%d", value);
    *element = (Element){.key = key, .value = value};
    return element;
}

/// Forces insertion past A' to verify the B and C stages and their lookup order
static void check_special_array_stages(const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    enum { CAPACITY = 1024 };
    const double delta = 0.125;
    FunnelPartition* partition = partition_funnel_hashmap(CAPACITY, delta);
    assert(partition != NULL);

    Element blocker = {.key = "occupied", .value = -1};
    Element b_target = {.key = "forced-b-target", .value = 1};
    Element* b_table[CAPACITY] = {NULL};
    for (int i = 0; i < partition->a_prime_length; i++) {
        b_table[i] = &blocker;
    }
    FunnelHashMap b_hashmap = {.capacity = CAPACITY, .size = partition->a_prime_length, .table = b_table};
    assert(insert_element_funnel_hashmap(&b_hashmap, delta, &b_target, seed));
    const Option_Element_p b_result = retrieve_element_funnel_hashmap(&b_hashmap, delta, b_target.key, seed);
    assert(b_result.option == Some);
    assert(b_result.element_p == &b_target);

    Element c_target = {.key = "forced-c-target", .value = 2};
    Element* c_table[CAPACITY] = {NULL};
    for (int i = 0; i < partition->c.starting_index; i++) {
        c_table[i] = &blocker;
    }
    FunnelHashMap c_hashmap = {.capacity = CAPACITY, .size = partition->c.starting_index, .table = c_table};
    assert(insert_element_funnel_hashmap(&c_hashmap, delta, &c_target, seed));
    const Option_Element_p c_result = retrieve_element_funnel_hashmap(&c_hashmap, delta, c_target.key, seed);
    assert(c_result.option == Some);
    assert(c_result.element_p == &c_target);

    delete_funnel_partition(partition);
}

int main(void) {
    enum { CAPACITY = 1024 };
    const double delta = 0.125;
    const int key_count = CAPACITY - (int)floor(delta * (double)CAPACITY);
    const uint8_t seed[SIPHASH_2_4_KEY_SIZE] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };

    check_special_array_stages(seed);

    FunnelHashMap* hashmap = create_funnel_hashmap(CAPACITY);
    Element** elements = calloc((size_t)key_count, sizeof(Element*));
    assert(hashmap != NULL);
    assert(elements != NULL);

    for (int i = 0; i < key_count; i++) {
        elements[i] = create_element(i);
    }

    assert(batch_insert_funnel_hashmap(hashmap, delta, elements, seed));
    assert(hashmap->size == key_count);

    FunnelPartition* partition = partition_funnel_hashmap(CAPACITY, delta);
    assert(partition != NULL);
    int a_prime_elements = 0;
    int b_elements = 0;
    int c_elements = 0;
    for (int i = 0; i < CAPACITY; i++) {
        if (hashmap->table[i] == NULL) {
            continue;
        }
        if (i < partition->a_prime_length) {
            a_prime_elements++;
        } else if (i < partition->c.starting_index) {
            b_elements++;
        } else {
            c_elements++;
        }
    }
    assert(a_prime_elements + b_elements + c_elements == key_count);
    printf("funnel distribution: A'=%d B=%d C=%d\n", a_prime_elements, b_elements, c_elements);

    for (int i = 0; i < key_count; i++) {
        assert(elements[i] == NULL);

        char key[48];
        snprintf(key, sizeof(key), "funnel-key-%d", i);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
        funnel_hashmap_reset_probe_stats(hashmap);
#endif
        const Option_Element_p reusable_result = retrieve_element_funnel_hashmap_with_partition(hashmap, partition, key, seed);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
        const FunnelHashmapProbeStats reusable_stats = funnel_hashmap_probe_stats(hashmap);
        funnel_hashmap_reset_probe_stats(hashmap);
#endif
        const Option_Element_p convenience_result = retrieve_element_funnel_hashmap(hashmap, delta, key, seed);
        assert(reusable_result.option == Some);
        assert(reusable_result.element_p->value == i);
        assert(strcmp(reusable_result.element_p->key, key) == 0);
        assert(convenience_result.option == reusable_result.option);
        assert(convenience_result.element_p == reusable_result.element_p);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
        const FunnelHashmapProbeStats convenience_stats = funnel_hashmap_probe_stats(hashmap);
        assert(reusable_stats.lookup_ops == convenience_stats.lookup_ops);
        assert(reusable_stats.lookup_probes == convenience_stats.lookup_probes);
        assert(reusable_stats.maximum_lookup_probes == convenience_stats.maximum_lookup_probes);
#endif
    }

#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    funnel_hashmap_reset_probe_stats(hashmap);
#endif
    const Option_Element_p reusable_missing = retrieve_element_funnel_hashmap_with_partition(hashmap, partition, "missing-funnel-key", seed);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const FunnelHashmapProbeStats reusable_missing_stats = funnel_hashmap_probe_stats(hashmap);
    funnel_hashmap_reset_probe_stats(hashmap);
#endif
    const Option_Element_p convenience_missing = retrieve_element_funnel_hashmap(hashmap, delta, "missing-funnel-key", seed);
    assert(reusable_missing.option == None);
    assert(reusable_missing.element_p == NULL);
    assert(convenience_missing.option == reusable_missing.option);
    assert(convenience_missing.element_p == reusable_missing.element_p);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const FunnelHashmapProbeStats convenience_missing_stats = funnel_hashmap_probe_stats(hashmap);
    assert(reusable_missing_stats.lookup_ops == convenience_missing_stats.lookup_ops);
    assert(reusable_missing_stats.lookup_probes == convenience_missing_stats.lookup_probes);
    assert(reusable_missing_stats.maximum_lookup_probes == convenience_missing_stats.maximum_lookup_probes);
#endif

    delete_funnel_partition(partition);
    free(elements);
    delete_funnel_hashmap(hashmap);
    return 0;
}
