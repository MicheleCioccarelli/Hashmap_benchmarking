#include "elastic_hashing.h"

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <tgmath.h>

#include "siphash.h"

uint64_t _siphash_2_4_64(const void* input, const size_t length, const uint8_t key[SIPHASH_2_4_KEY_SIZE]) {
    uint8_t output[sizeof(uint64_t)];

    siphash(input, length, key, output, sizeof(output));

    // SipHash writes its 64 bit result as little endian bytes
    return (uint64_t)output[0]
           | ((uint64_t)output[1] << 8)
           | ((uint64_t)output[2] << 16)
           | ((uint64_t)output[3] << 24)
           | ((uint64_t)output[4] << 32)
           | ((uint64_t)output[5] << 40)
           | ((uint64_t)output[6] << 48)
           | ((uint64_t)output[7] << 56);
}

static void write_u64_big_endian(uint8_t* output, const uint64_t value) {
    output[0] = (uint8_t)(value >> 56);
    output[1] = (uint8_t)(value >> 48);
    output[2] = (uint8_t)(value >> 40);
    output[3] = (uint8_t)(value >> 32);
    output[4] = (uint8_t)(value >> 24);
    output[5] = (uint8_t)(value >> 16);
    output[6] = (uint8_t)(value >> 8);
    output[7] = (uint8_t)value;
}

uint64_t siphash_probe64(const char* element_key, const uint64_t probe_number,
                         const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    const size_t key_length = strlen(element_key);
    const size_t probe_length = sizeof(probe_number);
    const size_t input_length = probe_length + key_length;
    uint8_t input[input_length];

    // The fixed size prefix separates the probe number from the key bytes
    write_u64_big_endian(input, probe_number);
    memcpy(input + probe_length, element_key, key_length);

    return _siphash_2_4_64(input, input_length, seed);
}

int n_elastic_subarrays(const int capacity) {
    if (capacity <= 1) {
        return 1;
    }

    int log = 0;
    int power = 1;
    while (power < capacity) {
        power *= 2;
        log++;
    }

    return log;
}

bool subarray_greedy_insert(Element** table, ElasticSubArray* subarray, Element* element) {
    (void)table;
    (void)subarray;
    (void)element;
    return false;
}

ElasticSubArray* partition_elastic_hashmap(const int capacity) {
    if (capacity <= 0) {
        return NULL;
    }

    const int subarray_count = n_elastic_subarrays(capacity);
    ElasticSubArray* subarrays = malloc(sizeof(ElasticSubArray) * subarray_count);
    if (subarrays == NULL) {
        return NULL;
    }

    int prev_len = 0;
    int remaining_len = capacity;

    for (int i = 0; i < subarray_count; i++) {
        const bool is_last_subarray = (i == subarray_count - 1);
        int current_len = (remaining_len + 1) / 2;

        if (is_last_subarray) {
            // The paper allows +-1 so the partition covers all of A exactly
            current_len = remaining_len;
        }

        subarrays[i].starting_index = prev_len;
        subarrays[i].lenght = current_len;
        subarrays[i].size = 0;

        prev_len += current_len;
        remaining_len -= current_len;
    }

    return subarrays;
}

void batch_insert(ElasticHashmap* hashmap, float delta, Element* elements) {
    if (hashmap == NULL) {
        return;
    }

    // Allocate space to store all of the log(N) subarrays
    const int n_subarrays = n_elastic_subarrays(hashmap->capacity);
    ElasticSubArray* subarrays = partition_elastic_hashmap(hashmap->capacity);
    if (subarrays == NULL) {
        return;
    }

    // Ai
    ElasticSubArray* current_subarray = subarrays;
    // Ai+1
    ElasticSubArray* next_subarray = NULL;

    // Start actually inserting elements in batch 0 => fill A1 to 75% capacity greedily
    for (int i = 0; i < ceil(current_subarray->lenght * 0.75); i++) {

    }

    // Batch 0


    free(subarrays);
}

void delete_elastic_hashmap(ElasticHashmap* hashmap) {
    if (hashmap != NULL) {
        for (int i = 0; i < hashmap->capacity; i++) {
            if (hashmap->table[i] != NULL) {
                free((char*)hashmap->table[i]->key);
                free(hashmap->table[i]);
            }
        }
        free(hashmap->table);
        free(hashmap);
    }
}
