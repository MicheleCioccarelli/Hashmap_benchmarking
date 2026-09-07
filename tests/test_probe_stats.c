#include "api_hashmap.h"
#include "elastic_hashing.h"
#include "funnel_hashing.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(HASHMAP_COUNT_PROBES) || !HASHMAP_COUNT_PROBES
#error test_probe_stats must be compiled with HASHMAP_COUNT_PROBES
#endif

static char* copy_key(const char* key) {
    const size_t length = strlen(key) + 1;
    char* result = malloc(length);
    assert(result != NULL);
    memcpy(result, key, length);
    return result;
}

static Element** make_elements(const int count, const char* prefix) {
    Element** elements = calloc((size_t)count, sizeof(Element*));
    assert(elements != NULL);

    for (int i = 0; i < count; i++) {
        char key[64];
        const int written = snprintf(key, sizeof(key), "%s-%d", prefix, i);
        assert(written > 0 && (size_t)written < sizeof(key));
        elements[i] = malloc(sizeof(Element));
        assert(elements[i] != NULL);
        elements[i]->key = copy_key(key);
        elements[i]->value = i;
    }
    return elements;
}

static void check_standard_stats(void) {
    ApiHashmap* hashmap = create_api_hashmap_with_size(64);
    assert(hashmap != NULL);
    assert(insert_element_api_hashmap(hashmap, "alpha", 1) != NULL);
    assert(insert_element_api_hashmap(hashmap, "beta", 2) != NULL);

    ApiHashmapProbeStats stats = api_hashmap_probe_stats(hashmap);
    assert(stats.insertion_ops == 2);
    assert(stats.insertion_probes >= 2);
    assert(stats.maximum_insertion_probes >= 1);

    api_hashmap_reset_probe_stats(hashmap);
    assert(is_some(retrieve_element_api_hashmap(hashmap, "alpha").option));
    assert(is_none(retrieve_element_api_hashmap(hashmap, "missing").option));
    stats = api_hashmap_probe_stats(hashmap);
    assert(stats.lookup_ops == 2);
    assert(stats.lookup_probes >= 2);
    assert(stats.maximum_lookup_probes >= 1);
    destroy_api_hashmap(hashmap);
}

static void check_elastic_stats(const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    const int capacity = 64;
    const int insertion_count = 48;
    ElasticHashmap* hashmap = calloc(1, sizeof(ElasticHashmap));
    assert(hashmap != NULL);
    *hashmap = (ElasticHashmap){.capacity = capacity, .table = calloc((size_t)capacity, sizeof(Element*))};
    assert(hashmap->table != NULL);
    Element** elements = make_elements(insertion_count, "elastic");

    batch_insert(hashmap, 0.25f, elements, seed);
    ElasticHashmapProbeStats stats = elastic_hashmap_probe_stats(hashmap);
    assert(hashmap->size == insertion_count);
    assert(stats.insertion_ops == (uint64_t)insertion_count);
    assert(stats.insertion_probes >= stats.insertion_ops);
    assert(stats.maximum_insertion_probes >= 1);
    assert(stats.batch_zero_insertions + stats.case_one_insertions + stats.case_two_insertions + stats.case_three_insertions == stats.insertion_ops);
    assert(stats.batch_zero_probes + stats.case_one_probes + stats.case_two_probes + stats.case_three_probes == stats.insertion_probes);
    assert(stats.maximum_case_three_probes <= stats.maximum_insertion_probes);

    elastic_hashmap_reset_probe_stats(hashmap);
    assert(is_some(retrieve_element_elastic_hashmap(hashmap, "elastic-0", seed).option));
    assert(is_none(retrieve_element_elastic_hashmap(hashmap, "missing", seed).option));
    stats = elastic_hashmap_probe_stats(hashmap);
    assert(stats.lookup_ops == 2);
    assert(stats.lookup_probes >= 2);
    assert(stats.maximum_lookup_probes >= 1);
    free(elements);
    delete_elastic_hashmap(hashmap);
}

static void check_funnel_stats(const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    const int capacity = 1024;
    const int insertion_count = 896;
    FunnelHashMap* hashmap = create_funnel_hashmap(capacity);
    assert(hashmap != NULL);
    Element** elements = make_elements(insertion_count, "funnel");

    assert(batch_insert_funnel_hashmap(hashmap, 0.125, elements, seed));
    FunnelHashmapProbeStats stats = funnel_hashmap_probe_stats(hashmap);
    assert(hashmap->size == insertion_count);
    assert(stats.insertion_ops == (uint64_t)insertion_count);
    assert(stats.insertion_probes >= stats.insertion_ops);
    assert(stats.maximum_insertion_probes >= 1);
    assert(stats.insertions_in_a + stats.insertions_in_b + stats.insertions_in_c == stats.insertion_ops);
    assert(stats.insertion_failures == 0);

    funnel_hashmap_reset_probe_stats(hashmap);
    assert(is_some(retrieve_element_funnel_hashmap(hashmap, 0.125, "funnel-0", seed).option));
    assert(is_none(retrieve_element_funnel_hashmap(hashmap, 0.125, "missing", seed).option));
    stats = funnel_hashmap_probe_stats(hashmap);
    assert(stats.lookup_ops == 2);
    assert(stats.lookup_probes >= 2);
    assert(stats.maximum_lookup_probes >= 1);
    free(elements);
    delete_funnel_hashmap(hashmap);
}

int main(void) {
    const uint8_t seed[SIPHASH_2_4_KEY_SIZE] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    check_standard_stats();
    check_elastic_stats(seed);
    check_funnel_stats(seed);
    return 0;
}
