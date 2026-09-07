#include "elastic_hashing.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint64_t observed_probe_numbers[128];
static int observed_probe_count;
static uint64_t target_probe_number;

/// Mock SipHash which records each probe number and selects relative index one only for the target probe
int siphash(const void* input, const size_t input_length, const void* key, uint8_t* output, const size_t output_length) {
    assert(input != NULL);
    assert(input_length >= sizeof(uint64_t));
    assert(key != NULL);
    assert(output != NULL);
    assert(output_length == sizeof(uint64_t));

    const uint8_t* bytes = input;
    uint64_t probe_number = 0;
    for (size_t i = 0; i < sizeof(uint64_t); i++) {
        probe_number = (probe_number << 8) | bytes[i];
    }

    assert(observed_probe_count < (int)(sizeof(observed_probe_numbers) / sizeof(observed_probe_numbers[0])));
    observed_probe_numbers[observed_probe_count++] = probe_number;
    memset(output, 0, output_length);
    if (probe_number == target_probe_number) {
        output[0] = 1;
    }
    return 0;
}

/// Places target at h_phi(i,j) and checks that lookup evaluates every global probe through phi(i,j)
static void check_lookup_order(const int target_subarray, const uint64_t local_probe_number) {
    enum { CAPACITY = 64 };
    const uint8_t seed[SIPHASH_2_4_KEY_SIZE] = {0};
    Element target = {.key = "target", .value = target_subarray};
    Element* table[CAPACITY] = {NULL};
    ElasticSubArray* subarrays = partition_elastic_hashmap(CAPACITY);
    assert(subarrays != NULL);

    target_probe_number = elastic_phi((uint64_t)target_subarray, local_probe_number);
    assert(target_probe_number < sizeof(observed_probe_numbers) / sizeof(observed_probe_numbers[0]));
    table[subarrays[target_subarray - 1].starting_index + 1] = &target;
    const ElasticHashmap hashmap = {.capacity = CAPACITY, .size = 1, .table = table};
    observed_probe_count = 0;

    const Option_Element_p result = retrieve_element_elastic_hashmap(&hashmap, target.key, seed);
    assert(result.option == Some);
    assert(result.element_p == &target);
    assert(observed_probe_count == (int)target_probe_number);
    for (int i = 0; i < observed_probe_count; i++) {
        assert(observed_probe_numbers[i] == (uint64_t)i + 1);
    }

    free(subarrays);
}

int main(void) {
    check_lookup_order(1, 1);
    check_lookup_order(2, 1);
    check_lookup_order(3, 1);
    check_lookup_order(2, 2);
    return 0;
}
