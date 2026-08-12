#include "elastic_hashing.h"

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <tgmath.h>

#include "siphash.h"

static const double ELASTIC_C_CONSTANT = 100.0;

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
    // Encode the probe number in a platform independent byte order
    output[0] = (uint8_t)(value >> 56);
    output[1] = (uint8_t)(value >> 48);
    output[2] = (uint8_t)(value >> 40);
    output[3] = (uint8_t)(value >> 32);
    output[4] = (uint8_t)(value >> 24);
    output[5] = (uint8_t)(value >> 16);
    output[6] = (uint8_t)(value >> 8);
    output[7] = (uint8_t)value;
}

uint64_t siphash_probe64(const char* element_key, const uint64_t probe_number, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    const size_t key_length = strlen(element_key);
    const size_t probe_length = sizeof(probe_number);
    const size_t input_length = probe_length + key_length;
    uint8_t input[input_length];

    // The fixed size prefix separates the probe number from the key bytes
    write_u64_big_endian(input, probe_number);
    memcpy(input + probe_length, element_key, key_length);

    return _siphash_2_4_64(input, input_length, seed);
}

/// Returns the number of bits needed to represent a positive number
static int bit_length(const uint64_t number) {
    int length = 0;
    uint64_t remaining = number;

    while (remaining != 0) {
        length++;
        remaining >>= 1;
    }

    return length;
}

uint64_t elastic_phi(const uint64_t subarray_number, const uint64_t local_probe_number) {
    if (subarray_number == 0 || local_probe_number == 0) {
        return 0;
    }

    const int subarray_bits = bit_length(subarray_number);
    const int probe_bits = bit_length(local_probe_number);
    if (2 * probe_bits + 1 + subarray_bits > 64) {
        return 0;
    }

    uint64_t result = 0;
    for (int bit = probe_bits - 1; bit >= 0; bit--) {
        result = (result << 1) | 1;
        result = (result << 1) | ((local_probe_number >> bit) & 1);
    }

    // zero separates the binary representation of j from the representation of i
    result <<= 1;
    return (result << subarray_bits) | subarray_number;
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

/// Inserts using at most max_probes probes in the selected subarray
static bool insert_first_available_limited(Element** table, ElasticSubArray* subarray, Element* element, const uint8_t seed[SIPHASH_2_4_KEY_SIZE], uint64_t max_probes) {
    if (table == NULL || subarray == NULL || element == NULL || element->key == NULL || seed == NULL || subarray->subarray_number <= 0 || subarray->length <= 0 || subarray->size >= subarray->length || max_probes == 0) {
        return false;
    }

    uint64_t probe_number = 1;
    while (subarray->size < subarray->length && probe_number <= max_probes) {
        // The hash produces a relative position, so the subarray start is added afterwards
        // this is because the subarrays are just offsets, not actually different arrays
        const uint64_t global_probe_number = elastic_phi((uint64_t)subarray->subarray_number, probe_number);
        if (global_probe_number == 0) {
            return false;
        }
        const int relative_index = (int)( siphash_probe64(element->key, global_probe_number, seed) % (uint64_t)subarray->length );
        const int table_index = subarray->starting_index + relative_index;

        if (table[table_index] == NULL) {
            table[table_index] = element;
            subarray->size++;
            return true;
        }

        probe_number++;
    }

    return false;
}

bool insert_first_available(Element** table, ElasticSubArray* subarray, Element* element, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    if (table == NULL || subarray == NULL || element == NULL || element->key == NULL || seed == NULL || subarray->subarray_number <= 0 || subarray->length <= 0 || subarray->size >= subarray->length) {
        return false;
    }

    uint64_t probe_number = 1;
    while (subarray->size < subarray->length) {
        // The hash produces a relative position, so the subarray start is added afterwards
        // this is because the subarrays are just offsets, not actually different arrays
        const uint64_t global_probe_number = elastic_phi((uint64_t)subarray->subarray_number, probe_number);
        if (global_probe_number == 0) {
            return false;
        }
        const int relative_index = (int)( siphash_probe64(element->key, global_probe_number, seed) % (uint64_t)subarray->length );
        const int table_index = subarray->starting_index + relative_index;

        if (table[table_index] == NULL) {
            table[table_index] = element;
            subarray->size++;
            return true;
        }

        probe_number++;
    }

    return false;
}

/// Returns the fraction of slots that are still empty in a subarray
/// The caller must provide a non-empty subarray
static double subarray_vacancy(const ElasticSubArray* subarray) {
    return (double)(subarray->length - subarray->size) / (double)subarray->length;
}

/// Returns the number of probes used by f(epsilon)
/// The result is rounded up because the insertion loop uses an integer number of probes
static uint64_t elastic_probe_limit(const double vacancy, const double delta) {
    // The paper leaves c as a sufficiently large constant and the slides use 100 in the proof
    const double limit = ELASTIC_C_CONSTANT * fmin(log2(1.0 / vacancy), log2(1.0 / delta));
    return (uint64_t)ceil(limit);
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

        subarrays[i].subarray_number = i + 1;
        subarrays[i].starting_index = prev_len;
        subarrays[i].length = current_len;
        subarrays[i].size = 0;

        prev_len += current_len;
        remaining_len -= current_len;
    }

    return subarrays;
}

void batch_insert(ElasticHashmap* hashmap, float delta, Element** elements, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    if (hashmap == NULL || hashmap->table == NULL || elements == NULL || seed == NULL || delta <= 0.0f || delta >= 1.0f || hashmap->capacity <= 0) {
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
    int n_inserted = 0;
    const int n_insertions = hashmap->capacity - (int)floor(delta * hashmap->capacity);

    // Start actually inserting elements in batch 0 => fill A1 to 75% capacity greedily
    // Batch B0 uses only A1 and fills it to 75% of its capacity
    const int n_batch_0_insertions = (int)ceil(current_subarray->length * 0.75);
    for (int i = 0; i < n_batch_0_insertions && n_inserted < n_insertions; i++) {
        if (elements[n_inserted] == NULL || !insert_first_available(hashmap->table, current_subarray, elements[n_inserted], seed)) {
            // Shouldn't happen but safeguards are good
            break;
        }

        // The hashmap now owns the inserted element pointer so it should be deleted from the insertion list
        elements[n_inserted] = NULL;
        hashmap->size++;
        n_inserted++;
    }

    // Each following batch works on Ai and Ai+1 as described by the paper
    for (int current_index = 0; n_inserted < n_insertions && current_index + 1 < n_subarrays; current_index++) {
        current_subarray = &subarrays[current_index];
        next_subarray = &subarrays[current_index + 1];

        const int n_batch_insertions = current_subarray->length - (int)floor(delta * current_subarray->length / 2.0) - (int)ceil(current_subarray->length * 0.75) + (int)ceil(next_subarray->length * 0.75);

        for (int i = 0; i < n_batch_insertions && n_inserted < n_insertions; i++) {
            const double current_vacancy = subarray_vacancy(current_subarray);
            const double next_vacancy = subarray_vacancy(next_subarray);
            bool inserted = false;

            if (current_vacancy > delta / 2.0 && next_vacancy > 0.25) {
                // Case 1 => probe Ai for f(epsilon_i) positions, then fall back to Ai+1
                const uint64_t max_probes = elastic_probe_limit(current_vacancy, delta);

                inserted = insert_first_available_limited(hashmap->table, current_subarray, elements[n_inserted], seed, max_probes);
                if (!inserted) {
                    // You tried f(epsilon) probes in Ai and they all failed
                    inserted = insert_first_available(hashmap->table, next_subarray, elements[n_inserted], seed);
                }
            } else if (current_vacancy <= delta / 2.0) {
                // Case 2 => place the element in Ai+1 no matter what
                inserted = insert_first_available(hashmap->table, next_subarray, elements[n_inserted], seed);
            } else {
                // Case 3 => place the element in Ai no matter what (bad case)
                inserted = insert_first_available(hashmap->table, current_subarray, elements[n_inserted], seed);
            }
            if (!inserted) {
                break;
            }

            // The hashmap now owns the inserted element pointer so it should be deleted from the insertion list
            elements[n_inserted] = NULL;
            hashmap->size++;
            n_inserted++;
        }
    }

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
