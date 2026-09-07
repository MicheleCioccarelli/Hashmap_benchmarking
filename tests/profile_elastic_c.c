#include "elastic_hashing.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(HASHMAP_COUNT_PROBES) || !HASHMAP_COUNT_PROBES
#error profile_elastic_c must be compiled with HASHMAP_COUNT_PROBES
#endif

/// Builds one recorded SipHash seed for a constant experiment
static void make_seed(const int seed_number, uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    for (int i = 0; i < SIPHASH_2_4_KEY_SIZE; i++) {
        seed[i] = (uint8_t)(seed_number * SIPHASH_2_4_KEY_SIZE + i);
    }
}

/// Allocates the uniquely owned elements used by one map
static Element** create_elements(const int count, const int seed_number) {
    Element** elements = calloc((size_t)count, sizeof(Element*));
    assert(elements != NULL);
    for (int i = 0; i < count; i++) {
        char generated_key[64];
        const int written = snprintf(generated_key, sizeof(generated_key), "constant-%d-%d", seed_number, i);
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

/// Runs one complete insertion and successful lookup experiment
static void run_experiment(const int capacity, const float delta, const double c, const int seed_number) {
    const int count = capacity - (int)floor((double)delta * (double)capacity);
    uint8_t seed[SIPHASH_2_4_KEY_SIZE];
    make_seed(seed_number, seed);
    Element** elements = create_elements(count, seed_number);
    ElasticHashmap* hashmap = calloc(1, sizeof(ElasticHashmap));
    assert(hashmap != NULL);
    *hashmap = (ElasticHashmap){.capacity = capacity, .table = calloc((size_t)capacity, sizeof(Element*))};
    assert(hashmap->table != NULL);

    const clock_t insertion_start = clock();
    batch_insert_with_c(hashmap, delta, c, elements, seed);
    const clock_t insertion_end = clock();
    assert(hashmap->size == count);
    const ElasticHashmapProbeStats insertion = elastic_hashmap_probe_stats(hashmap);
    elastic_hashmap_reset_probe_stats(hashmap);

    const clock_t lookup_start = clock();
    for (int i = 0; i < count; i++) {
        char key[64];
        const int written = snprintf(key, sizeof(key), "constant-%d-%d", seed_number, i);
        assert(written > 0 && (size_t)written < sizeof(key));
        const Option_Element_p result = retrieve_element_elastic_hashmap(hashmap, key, seed);
        assert(result.option == Some && result.element_p->value == i);
    }
    const clock_t lookup_end = clock();
    const ElasticHashmapProbeStats lookup = elastic_hashmap_probe_stats(hashmap);

    const double case_three_average = insertion.case_three_insertions == 0 ? 0.0 : (double)insertion.case_three_probes / (double)insertion.case_three_insertions;
    printf("%.3f,%d,%.8f,%d,%.6f,%.6f,%llu,%.6f,%.6f,%llu,%llu,%llu,%llu,%.6f,%llu\n", c, capacity, (double)delta, seed_number, (double)(insertion_end - insertion_start) / CLOCKS_PER_SEC, (double)insertion.insertion_probes / (double)insertion.insertion_ops, (unsigned long long)insertion.maximum_insertion_probes, (double)(lookup_end - lookup_start) / CLOCKS_PER_SEC, (double)lookup.lookup_probes / (double)lookup.lookup_ops, (unsigned long long)lookup.maximum_lookup_probes, (unsigned long long)insertion.case_one_insertions, (unsigned long long)insertion.case_one_fallbacks, (unsigned long long)insertion.case_three_insertions, case_three_average, (unsigned long long)insertion.maximum_case_three_probes);

    free(elements);
    delete_elastic_hashmap(hashmap);
}

int main(const int argc, char** argv) {
    assert(argc == 3);
    const int capacity = atoi(argv[1]);
    const float delta = strtof(argv[2], NULL);
    assert(capacity > 0 && isfinite(delta) && delta > 0.0f && delta < 1.0f);
    static const double constants[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 100.0};
    printf("c,capacity,delta,seed,insertion_seconds,insertion_average,insertion_maximum,lookup_seconds,lookup_average,lookup_maximum,case_one,case_one_fallbacks,case_three,case_three_average,case_three_maximum\n");
    for (size_t i = 0; i < sizeof(constants) / sizeof(constants[0]); i++) {
        for (int seed_number = 0; seed_number < 5; seed_number++) {
            run_experiment(capacity, delta, constants[i], seed_number);
        }
    }
    return 0;
}
