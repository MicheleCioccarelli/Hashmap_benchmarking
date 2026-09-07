#include "api_hashmap.h"
#include "elastic_hashing.h"
#include "funnel_hashing.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(HASHMAP_COUNT_PROBES) || !HASHMAP_COUNT_PROBES
#error compare_probe_scaling must be compiled with HASHMAP_COUNT_PROBES
#endif

typedef struct ProbeResult {
    double insertion_average;
    uint64_t insertion_maximum;
    double lookup_average;
    uint64_t lookup_maximum;
} ProbeResult;

/// Builds one recorded SipHash seed for a scaling run
static void make_seed(const int seed_number, uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    for (int i = 0; i < SIPHASH_2_4_KEY_SIZE; i++) {
        seed[i] = (uint8_t)(seed_number * SIPHASH_2_4_KEY_SIZE + i);
    }
}

/// Allocates one deterministic key for every insertion in a scaling run
static char** create_keys(const int count, const int seed_number) {
    char** keys = calloc((size_t)count, sizeof(char*));
    assert(keys != NULL);
    for (int i = 0; i < count; i++) {
        char key[64];
        const int written = snprintf(key, sizeof(key), "scaling-%d-%d", seed_number, i);
        assert(written > 0 && (size_t)written < sizeof(key));
        const size_t length = (size_t)written + 1;
        keys[i] = malloc(length);
        assert(keys[i] != NULL);
        memcpy(keys[i], key, length);
    }
    return keys;
}

/// Allocates Elements which transfer the generated keys to an experimental map
static Element** create_elements(char** keys, const int count) {
    Element** elements = calloc((size_t)count, sizeof(Element*));
    assert(elements != NULL);
    for (int i = 0; i < count; i++) {
        elements[i] = malloc(sizeof(Element));
        assert(elements[i] != NULL);
        *elements[i] = (Element){.key = keys[i], .value = i};
    }
    return elements;
}

/// Measures insertion and successful lookup probes for the control map
static ProbeResult measure_standard(char** keys, const int count, const int capacity, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    ApiHashmap* hashmap = create_fixed_api_hashmap_with_size_and_seed((size_t)capacity, seed);
    assert(hashmap != NULL);
    for (int i = 0; i < count; i++) {
        assert(insert_element_api_hashmap(hashmap, keys[i], i) != NULL);
    }
    const ApiHashmapProbeStats insertion = api_hashmap_probe_stats(hashmap);
    api_hashmap_reset_probe_stats(hashmap);

    for (int i = 0; i < count; i++) {
        const Option_Element_p result = retrieve_element_api_hashmap(hashmap, keys[i]);
        assert(is_some(result.option));
    }
    const ApiHashmapProbeStats lookup = api_hashmap_probe_stats(hashmap);
    const ProbeResult result = {
        .insertion_average = (double)insertion.insertion_probes / (double)insertion.insertion_ops,
        .insertion_maximum = insertion.maximum_insertion_probes,
        .lookup_average = (double)lookup.lookup_probes / (double)lookup.lookup_ops,
        .lookup_maximum = lookup.maximum_lookup_probes,
    };
    destroy_api_hashmap(hashmap);
    for (int i = 0; i < count; i++) {
        free(keys[i]);
    }
    free(keys);
    return result;
}

/// Measures insertion and successful lookup probes for Elastic Hashing
static ProbeResult measure_elastic(char** keys, const int count, const int capacity, const float delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    ElasticHashmap* hashmap = calloc(1, sizeof(ElasticHashmap));
    Element** elements = create_elements(keys, count);
    assert(hashmap != NULL);
    *hashmap = (ElasticHashmap){.capacity = capacity, .table = calloc((size_t)capacity, sizeof(Element*))};
    assert(hashmap->table != NULL);
    batch_insert(hashmap, delta, elements, seed);
    assert(hashmap->size == count);
    free(elements);
    const ElasticHashmapProbeStats insertion = elastic_hashmap_probe_stats(hashmap);
    elastic_hashmap_reset_probe_stats(hashmap);

    for (int i = 0; i < count; i++) {
        const Option_Element_p result = retrieve_element_elastic_hashmap(hashmap, keys[i], seed);
        assert(is_some(result.option));
    }
    const ElasticHashmapProbeStats lookup = elastic_hashmap_probe_stats(hashmap);
    const ProbeResult result = {
        .insertion_average = (double)insertion.insertion_probes / (double)insertion.insertion_ops,
        .insertion_maximum = insertion.maximum_insertion_probes,
        .lookup_average = (double)lookup.lookup_probes / (double)lookup.lookup_ops,
        .lookup_maximum = lookup.maximum_lookup_probes,
    };
    delete_elastic_hashmap(hashmap);
    free(keys);
    return result;
}

/// Measures insertion and successful lookup probes for Funnel Hashing
static ProbeResult measure_funnel(char** keys, const int count, const int capacity, const double delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    FunnelHashMap* hashmap = create_funnel_hashmap(capacity);
    FunnelPartition* partition = partition_funnel_hashmap(capacity, delta);
    Element** elements = create_elements(keys, count);
    assert(hashmap != NULL && partition != NULL);
    assert(batch_insert_funnel_hashmap(hashmap, delta, elements, seed));
    free(elements);
    const FunnelHashmapProbeStats insertion = funnel_hashmap_probe_stats(hashmap);
    funnel_hashmap_reset_probe_stats(hashmap);

    for (int i = 0; i < count; i++) {
        const Option_Element_p result = retrieve_element_funnel_hashmap_with_partition(hashmap, partition, keys[i], seed);
        assert(is_some(result.option));
    }
    const FunnelHashmapProbeStats lookup = funnel_hashmap_probe_stats(hashmap);
    const ProbeResult result = {
        .insertion_average = (double)insertion.insertion_probes / (double)insertion.insertion_ops,
        .insertion_maximum = insertion.maximum_insertion_probes,
        .lookup_average = (double)lookup.lookup_probes / (double)lookup.lookup_ops,
        .lookup_maximum = lookup.maximum_lookup_probes,
    };
    delete_funnel_partition(partition);
    delete_funnel_hashmap(hashmap);
    free(keys);
    return result;
}

/// Prints one machine-readable scaling result
static void print_result(const char* mode, const int capacity, const int seed_number, const ProbeResult result) {
    printf("%s,%d,%d,%.6f,%llu,%.6f,%llu\n", mode, capacity, seed_number, result.insertion_average, (unsigned long long)result.insertion_maximum, result.lookup_average, (unsigned long long)result.lookup_maximum);
}

int main(const int argc, char** argv) {
    const double delta = argc == 2 ? strtod(argv[1], NULL) : 0.125;
    const int capacities[] = {1024, 4096, 16384, 65536};
    const int seed_count = 5;
    assert(isfinite(delta) && delta > 0.0 && delta <= 0.125);
    printf("mode,capacity,seed,insertion_average,insertion_maximum,lookup_average,lookup_maximum\n");

    for (size_t capacity_index = 0; capacity_index < sizeof(capacities) / sizeof(capacities[0]); capacity_index++) {
        const int capacity = capacities[capacity_index];
        const int count = capacity - (int)floor(delta * capacity);
        FunnelPartition* validation_partition = partition_funnel_hashmap(capacity, delta);
        if (validation_partition == NULL) {
            continue;
        }
        delete_funnel_partition(validation_partition);
        for (int seed_number = 0; seed_number < seed_count; seed_number++) {
            uint8_t seed[SIPHASH_2_4_KEY_SIZE];
            make_seed(seed_number, seed);
            print_result("standard", capacity, seed_number, measure_standard(create_keys(count, seed_number), count, capacity, seed));
            print_result("elastic", capacity, seed_number, measure_elastic(create_keys(count, seed_number), count, capacity, (float)delta, seed));
            print_result("funnel", capacity, seed_number, measure_funnel(create_keys(count, seed_number), count, capacity, delta, seed));
        }
    }
    return 0;
}
