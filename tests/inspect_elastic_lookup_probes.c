#include "elastic_hashing.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static bool count_siphash_calls;
static uint64_t siphash_calls;

int real_siphash(const void* input, size_t input_length, const void* key, uint8_t* output, size_t output_length);

/// Counts calls made by lookup then forwards them to the unmodified SipHash implementation
int siphash(const void* input, const size_t input_length, const void* key, uint8_t* output, const size_t output_length) {
    if (count_siphash_calls) {
        siphash_calls++;
    }
    return real_siphash(input, input_length, key, output, output_length);
}

/// Recovers the insertion pair from the final slot without giving that pair to lookup
static uint64_t insertion_phi(const ElasticHashmap* hashmap, const ElasticSubArray* subarrays, const int n_subarrays, const char* key, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    int table_index = -1;
    for (int i = 0; i < hashmap->capacity; i++) {
        if (hashmap->table[i] != NULL && hashmap->table[i]->key == key) {
            table_index = i;
            break;
        }
    }
    assert(table_index >= 0);

    const ElasticSubArray* containing_subarray = NULL;
    for (int i = 0; i < n_subarrays; i++) {
        if (table_index >= subarrays[i].starting_index && table_index < subarrays[i].starting_index + subarrays[i].length) {
            containing_subarray = &subarrays[i];
            break;
        }
    }
    assert(containing_subarray != NULL);

    for (uint64_t local_probe = 1;; local_probe++) {
        const uint64_t global_probe = elastic_phi((uint64_t)containing_subarray->subarray_number, local_probe);
        assert(global_probe != 0);
        const int relative_index = (int)(siphash_probe64(key, global_probe, seed) % (uint64_t)containing_subarray->length);
        if (containing_subarray->starting_index + relative_index == table_index) {
            return global_probe;
        }
    }
}

int main(void) {
    enum { CAPACITY = 256, KEY_COUNT = 192 };
    const uint8_t seed[SIPHASH_2_4_KEY_SIZE] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    char keys[KEY_COUNT][32];
    Element stored[KEY_COUNT];
    Element* elements[KEY_COUNT];
    Element** table = calloc(CAPACITY, sizeof(Element*));
    assert(table != NULL);

    ElasticHashmap hashmap = {.capacity = CAPACITY, .size = 0, .table = table};
    for (int i = 0; i < KEY_COUNT; i++) {
        snprintf(keys[i], sizeof(keys[i]), "probe-metric-key-%d", i);
        stored[i] = (Element){.key = keys[i], .value = i};
        elements[i] = &stored[i];
    }
    batch_insert(&hashmap, 0.25f, elements, seed);
    assert(hashmap.size == KEY_COUNT);

    const int n_subarrays = n_elastic_subarrays(CAPACITY);
    ElasticSubArray* subarrays = partition_elastic_hashmap(CAPACITY);
    assert(subarrays != NULL);

    uint64_t total_phi = 0;
    uint64_t total_actual = 0;
    uint64_t maximum_phi = 0;
    uint64_t maximum_actual = 0;
    int exact_at_phi = 0;
    int found_earlier = 0;

    for (int i = 0; i < KEY_COUNT; i++) {
        count_siphash_calls = false;
        const uint64_t expected_phi = insertion_phi(&hashmap, subarrays, n_subarrays, keys[i], seed);

        siphash_calls = 0;
        count_siphash_calls = true;
        const Option_Element_p result = retrieve_element_elastic_hashmap(&hashmap, keys[i], seed);
        count_siphash_calls = false;
        assert(result.option == Some);
        assert(result.element_p == &stored[i]);
        assert(siphash_calls <= expected_phi);

        total_phi += expected_phi;
        total_actual += siphash_calls;
        maximum_phi = expected_phi > maximum_phi ? expected_phi : maximum_phi;
        maximum_actual = siphash_calls > maximum_actual ? siphash_calls : maximum_actual;
        exact_at_phi += siphash_calls == expected_phi;
        found_earlier += siphash_calls < expected_phi;
    }

    siphash_calls = 0;
    count_siphash_calls = true;
    const Option_Element_p missing = retrieve_element_elastic_hashmap(&hashmap, "missing-probe-metric-key", seed);
    count_siphash_calls = false;
    assert(missing.option == None);

    printf("capacity: %d\n", CAPACITY);
    printf("keys: %d\n", KEY_COUNT);
    printf("exactly at phi: %d\n", exact_at_phi);
    printf("found before phi: %d\n", found_earlier);
    printf("average phi bound: %.3f\n", (double)total_phi / (double)KEY_COUNT);
    printf("average actual positive probes: %.3f\n", (double)total_actual / (double)KEY_COUNT);
    printf("maximum phi bound: %" PRIu64 "\n", maximum_phi);
    printf("maximum actual positive probes: %" PRIu64 "\n", maximum_actual);
    printf("negative lookup probes: %" PRIu64 "\n", siphash_calls);

    free(subarrays);
    free(table);
    return 0;
}
