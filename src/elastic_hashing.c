#include "elastic_hashing.h"

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <tgmath.h>

/// The proof only requires c to be a sufficiently large universal constant
/// ELASTIC_C_VALUE can be overridden in an isolated benchmark build to study finite-size behavior
#ifndef ELASTIC_C_VALUE
#define ELASTIC_C_VALUE 100.0
#endif
static const double ELASTIC_C_CONSTANT = ELASTIC_C_VALUE;

double elastic_c_constant(void) {
    return ELASTIC_C_CONSTANT;
}

/// Adds the private stats parameter only to the probe-enabled build
/// The normal insertion helpers keep their original machine-level signature
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
#define ELASTIC_STATS_PARAMETER , ElasticHashmapProbeStats* probe_stats
#define ELASTIC_MAP_STATS_ARGUMENT(hashmap) , &(hashmap)->probe_stats
#define ELASTIC_NO_STATS_ARGUMENT , NULL
#define RECORD_ELASTIC_INSERT_OP(hashmap) ((hashmap)->probe_stats.insertion_ops++)
#define RECORD_ELASTIC_INSERT_PROBE(stats) do { if ((stats) != NULL) { (stats)->insertion_probes++; } } while (0)
#define RECORD_ELASTIC_LOOKUP_OP(hashmap) (((ElasticHashmap*)(hashmap))->probe_stats.lookup_ops++)
#define RECORD_ELASTIC_LOOKUP_PROBE(hashmap) (((ElasticHashmap*)(hashmap))->probe_stats.lookup_probes++)
#define RECORD_ELASTIC_EVENT(hashmap, event) ((hashmap)->probe_stats.event++)
#else
#define ELASTIC_STATS_PARAMETER
#define ELASTIC_MAP_STATS_ARGUMENT(hashmap)
#define ELASTIC_NO_STATS_ARGUMENT
#define RECORD_ELASTIC_INSERT_OP(hashmap) ((void)0)
#define RECORD_ELASTIC_INSERT_PROBE(stats) ((void)0)
#define RECORD_ELASTIC_LOOKUP_OP(hashmap) ((void)0)
#define RECORD_ELASTIC_LOOKUP_PROBE(hashmap) ((void)0)
#define RECORD_ELASTIC_EVENT(hashmap, event) ((void)0)
#endif

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

/// Decodes a number in the image of phi and returns false for every other positive integer
static bool elastic_inverse_phi(const uint64_t global_probe_number, uint64_t* subarray_number, uint64_t* local_probe_number) {
    if (global_probe_number == 0 || subarray_number == NULL || local_probe_number == NULL) {
        return false;
    }

    const int total_bits = bit_length(global_probe_number);
    int current_bit = total_bits - 1;
    uint64_t decoded_probe_number = 0;
    int decoded_probe_bits = 0;

    while (current_bit >= 0) {
        // Every bit of j is prefixed by one in the representation constructed by phi
        if (((global_probe_number >> current_bit) & 1) == 0) {
            return false;
        }
        current_bit--;
        if (current_bit < 0) {
            return false;
        }

        decoded_probe_number = (decoded_probe_number << 1) | ((global_probe_number >> current_bit) & 1);
        decoded_probe_bits++;
        current_bit--;
        if (current_bit < 0) {
            return false;
        }

        if (((global_probe_number >> current_bit) & 1) == 0) {
            const int subarray_bits = current_bit;
            if (subarray_bits <= 0) {
                return false;
            }

            const uint64_t decoded_subarray_number = global_probe_number & ((UINT64_C(1) << subarray_bits) - 1);
            if (decoded_probe_number == 0 || bit_length(decoded_probe_number) != decoded_probe_bits || decoded_subarray_number == 0 || bit_length(decoded_subarray_number) != subarray_bits) {
                return false;
            }

            *subarray_number = decoded_subarray_number;
            *local_probe_number = decoded_probe_number;
            return true;
        }
    }

    return false;
}

/// Returns the physical table index selected by one global probe number or -1 for invalid arguments
static int elastic_probe_table_index(const char* key, const uint64_t global_probe_number, const ElasticSubArray* subarray, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    if (key == NULL || global_probe_number == 0 || subarray == NULL || seed == NULL || subarray->starting_index < 0 || subarray->length <= 0) {
        return -1;
    }

    // The paper samples h_i,j uniformly with replacement from Ai
    // The modulo bias is at most one 64 bit hash value per relative position
    const uint64_t hash_value = siphash_probe64(key, global_probe_number, seed);
    const int relative_index = (int)(hash_value % (uint64_t)subarray->length);
    return subarray->starting_index + relative_index;
}

/// Returns h_global_probe_number for the complete one dimensional lookup sequence
static int elastic_lookup_table_index(const char* key, const uint64_t global_probe_number, const ElasticSubArray* subarrays, const int n_subarrays, const int capacity, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    if (key == NULL || global_probe_number == 0 || subarrays == NULL || n_subarrays <= 0 || capacity <= 0 || seed == NULL) {
        return -1;
    }

    uint64_t subarray_number = 0;
    uint64_t local_probe_number = 0;
    if (elastic_inverse_phi(global_probe_number, &subarray_number, &local_probe_number) && subarray_number <= (uint64_t)n_subarrays) {
        // This is one of the h_phi(i,j) entries assigned by the Elastic construction
        return elastic_probe_table_index(key, global_probe_number, &subarrays[subarray_number - 1], seed);
    }

    // The paper leaves positions outside the image of phi unspecified
    // Defining them as uniform full-table draws completes the one dimensional probe sequence
    return (int)(siphash_probe64(key, global_probe_number, seed) % (uint64_t)capacity);
}

int n_elastic_subarrays(const int capacity) {
    if (capacity <= 1) {
        return 1;
    }

    uint32_t remaining = (uint32_t)capacity - 1;
    int log = 0;
    while (remaining != 0) {
        remaining >>= 1;
        log++;
    }

    return log;
}

/// Inserts using at most max_probes probes in the selected subarray
static bool insert_first_available_limited(Element** table, ElasticSubArray* subarray, Element* element, const uint8_t seed[SIPHASH_2_4_KEY_SIZE], uint64_t max_probes ELASTIC_STATS_PARAMETER) {
    if (table == NULL || subarray == NULL || element == NULL || element->key == NULL || seed == NULL || subarray->subarray_number <= 0 || subarray->length <= 0 || subarray->size >= subarray->length || max_probes == 0) {
        return false;
    }

    uint64_t local_probe_number = 1;
    for (uint64_t probes = 0; subarray->size < subarray->length && probes < max_probes; probes++) {
        const uint64_t global_probe_number = elastic_phi((uint64_t)subarray->subarray_number, local_probe_number);
        const int table_index = elastic_probe_table_index(element->key, global_probe_number, subarray, seed);
        if (table_index < 0) {
            return false;
        }

        RECORD_ELASTIC_INSERT_PROBE(probe_stats);
        if (table[table_index] == NULL) {
            table[table_index] = element;
            subarray->size++;
            return true;
        }

        local_probe_number++;
    }

    return false;
}

static bool insert_first_available_unlimited(Element** table, ElasticSubArray* subarray, Element* element, const uint8_t seed[SIPHASH_2_4_KEY_SIZE] ELASTIC_STATS_PARAMETER) {
    if (table == NULL || subarray == NULL || element == NULL || element->key == NULL || seed == NULL || subarray->subarray_number <= 0 || subarray->length <= 0 || subarray->size >= subarray->length) {
        return false;
    }

    uint64_t local_probe_number = 1;
    while (subarray->size < subarray->length) {
        const uint64_t global_probe_number = elastic_phi((uint64_t)subarray->subarray_number, local_probe_number);
        const int table_index = elastic_probe_table_index(element->key, global_probe_number, subarray, seed);
        if (table_index < 0) {
            return false;
        }

        RECORD_ELASTIC_INSERT_PROBE(probe_stats);
        if (table[table_index] == NULL) {
            table[table_index] = element;
            subarray->size++;
            return true;
        }

        local_probe_number++;
    }

    return false;
}

bool insert_first_available(Element** table, ElasticSubArray* subarray, Element* element, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    return insert_first_available_unlimited(table, subarray, element, seed ELASTIC_NO_STATS_ARGUMENT);
}

/// Returns the fraction of slots that are still empty in a subarray
/// The caller must provide a non-empty subarray
static double subarray_vacancy(const ElasticSubArray* subarray) {
    return (double)(subarray->length - subarray->size) / (double)subarray->length;
}

/// Returns the number of probes used by f(epsilon)
/// The result is rounded up because the insertion loop uses an integer number of probes
static uint64_t elastic_probe_limit(const double vacancy, const double delta, const double c) {
    // The proof uses 100 in one probability threshold but leaves c itself unspecified
    // The first logarithm is squared, it is not merely a base-2 notation
    const double log_inverse_vacancy = log2(1.0 / vacancy);
    const double limit = c * fmin(log_inverse_vacancy * log_inverse_vacancy, log2(1.0 / delta));
    return (uint64_t)ceil(limit);
}

#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
/// Records the probe count of one completed Elastic insertion
static void finish_elastic_insertion_stats(ElasticHashmap* hashmap, const uint64_t probe_start, uint64_t* case_probes, uint64_t* case_maximum) {
    const uint64_t operation_probes = hashmap->probe_stats.insertion_probes - probe_start;
    if (operation_probes > hashmap->probe_stats.maximum_insertion_probes) {
        hashmap->probe_stats.maximum_insertion_probes = operation_probes;
    }
    if (case_probes != NULL) {
        *case_probes += operation_probes;
    }
    if (case_maximum != NULL && operation_probes > *case_maximum) {
        *case_maximum = operation_probes;
    }
}

/// Records the probe count of one completed Elastic lookup
static void finish_elastic_lookup_stats(const ElasticHashmap* hashmap, const uint64_t probe_start) {
    ElasticHashmap* mutable_hashmap = (ElasticHashmap*)hashmap;
    const uint64_t operation_probes = mutable_hashmap->probe_stats.lookup_probes - probe_start;
    if (operation_probes > mutable_hashmap->probe_stats.maximum_lookup_probes) {
        mutable_hashmap->probe_stats.maximum_lookup_probes = operation_probes;
    }
}
#endif

ElasticSubArray* partition_elastic_hashmap(const int capacity) {
    if (capacity <= 0) {
        return NULL;
    }

    const int subarray_count = n_elastic_subarrays(capacity);
    ElasticSubArray* subarrays = calloc((size_t)subarray_count, sizeof(ElasticSubArray));
    if (subarrays == NULL) {
        return NULL;
    }

    int prev_len = 0;
    int remaining_len = capacity;

    for (int i = 0; i < subarray_count; i++) {
        const bool is_last_subarray = (i == subarray_count - 1);
        int current_len = remaining_len / 2 + remaining_len % 2;

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

void batch_insert_with_c(ElasticHashmap* hashmap, float delta, double c, Element** elements, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    if (hashmap == NULL || hashmap->table == NULL || elements == NULL || seed == NULL || delta <= 0.0f || delta >= 1.0f || !isfinite(c) || c <= 0.0 || hashmap->capacity <= 0 || hashmap->size != 0) {
        return;
    }

    for (int i = 0; i < hashmap->capacity; i++) {
        if (hashmap->table[i] != NULL) {
            return;
        }
    }

#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    hashmap->probe_stats = (ElasticHashmapProbeStats){0};
#endif

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
    const double delta_value = (double)delta;
    const int n_insertions = hashmap->capacity - (int)floor(delta_value * (double)hashmap->capacity);

    // Start actually inserting elements in batch 0 => fill A1 to 75% capacity greedily
    // Batch B0 uses only A1 and fills it to 75% of its capacity
    const int n_batch_0_insertions = (int)ceil((double)current_subarray->length * 0.75);
    for (int i = 0; i < n_batch_0_insertions && n_inserted < n_insertions; i++) {
        if (elements[n_inserted] == NULL || elements[n_inserted]->key == NULL) {
            free(subarrays);
            return;
        }

        RECORD_ELASTIC_INSERT_OP(hashmap);
        RECORD_ELASTIC_EVENT(hashmap, batch_zero_insertions);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
        const uint64_t insertion_probe_start = hashmap->probe_stats.insertion_probes;
#endif
        if (!insert_first_available_unlimited(hashmap->table, current_subarray, elements[n_inserted], seed ELASTIC_MAP_STATS_ARGUMENT(hashmap))) {
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
            finish_elastic_insertion_stats(hashmap, insertion_probe_start, &hashmap->probe_stats.batch_zero_probes, NULL);
#endif
            // Shouldn't happen but safeguards are good
            free(subarrays);
            return;
        }
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
        finish_elastic_insertion_stats(hashmap, insertion_probe_start, &hashmap->probe_stats.batch_zero_probes, NULL);
#endif

        // The hashmap now owns the inserted element pointer so it should be deleted from the insertion list
        elements[n_inserted] = NULL;
        hashmap->size++;
        n_inserted++;
    }

    // Each following batch works on Ai and Ai+1 as described by the paper
    for (int current_index = 0; n_inserted < n_insertions && current_index + 1 < n_subarrays; current_index++) {
        current_subarray = &subarrays[current_index];
        next_subarray = &subarrays[current_index + 1];

        const int n_batch_insertions = current_subarray->length - (int)floor(delta_value * (double)current_subarray->length / 2.0) - (int)ceil((double)current_subarray->length * 0.75) + (int)ceil((double)next_subarray->length * 0.75);

        for (int i = 0; i < n_batch_insertions && n_inserted < n_insertions; i++) {
            if (elements[n_inserted] == NULL || elements[n_inserted]->key == NULL) {
                free(subarrays);
                return;
            }

            const double current_vacancy = subarray_vacancy(current_subarray);
            const double next_vacancy = subarray_vacancy(next_subarray);
            bool inserted = false;
            RECORD_ELASTIC_INSERT_OP(hashmap);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
            const uint64_t insertion_probe_start = hashmap->probe_stats.insertion_probes;
            uint64_t* case_probe_counter = NULL;
            uint64_t* case_probe_maximum = NULL;
#endif

            if (current_vacancy > delta / 2.0 && next_vacancy > 0.25) {
                // Case 1 => probe Ai for f(epsilon_i) positions, then fall back to Ai+1
                const uint64_t max_probes = elastic_probe_limit(current_vacancy, delta, c);
                RECORD_ELASTIC_EVENT(hashmap, case_one_insertions);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
                case_probe_counter = &hashmap->probe_stats.case_one_probes;
#endif

                inserted = insert_first_available_limited(hashmap->table, current_subarray, elements[n_inserted], seed, max_probes ELASTIC_MAP_STATS_ARGUMENT(hashmap));
                if (!inserted) {
                    // You tried f(epsilon) probes in Ai and they all failed
                    RECORD_ELASTIC_EVENT(hashmap, case_one_fallbacks);
                    inserted = insert_first_available_unlimited(hashmap->table, next_subarray, elements[n_inserted], seed ELASTIC_MAP_STATS_ARGUMENT(hashmap));
                }
            } else if (current_vacancy <= delta / 2.0) {
                // Case 2 => place the element in Ai+1 no matter what
                RECORD_ELASTIC_EVENT(hashmap, case_two_insertions);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
                case_probe_counter = &hashmap->probe_stats.case_two_probes;
#endif
                inserted = insert_first_available_unlimited(hashmap->table, next_subarray, elements[n_inserted], seed ELASTIC_MAP_STATS_ARGUMENT(hashmap));
            } else {
                // Case 3 => place the element in Ai no matter what (bad case)
                RECORD_ELASTIC_EVENT(hashmap, case_three_insertions);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
                case_probe_counter = &hashmap->probe_stats.case_three_probes;
                case_probe_maximum = &hashmap->probe_stats.maximum_case_three_probes;
#endif
                inserted = insert_first_available_unlimited(hashmap->table, current_subarray, elements[n_inserted], seed ELASTIC_MAP_STATS_ARGUMENT(hashmap));
            }
            if (!inserted) {
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
                finish_elastic_insertion_stats(hashmap, insertion_probe_start, case_probe_counter, case_probe_maximum);
#endif
                free(subarrays);
                return;
            }
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
            finish_elastic_insertion_stats(hashmap, insertion_probe_start, case_probe_counter, case_probe_maximum);
#endif

            // The hashmap now owns the inserted element pointer so it should be deleted from the insertion list
            elements[n_inserted] = NULL;
            hashmap->size++;
            n_inserted++;
        }
    }

    free(subarrays);
}

void batch_insert(ElasticHashmap* hashmap, const float delta, Element** elements, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    batch_insert_with_c(hashmap, delta, ELASTIC_C_CONSTANT, elements, seed);
}

Option_Element_p retrieve_element_elastic_hashmap(const ElasticHashmap* hashmap, const char* key, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    if (hashmap == NULL || hashmap->table == NULL || key == NULL || seed == NULL || hashmap->capacity <= 0) {
        return (Option_Element_p){.option = None, .element_p = NULL};
    }

#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const uint64_t lookup_probe_start = hashmap->probe_stats.lookup_probes;
#endif
    RECORD_ELASTIC_LOOKUP_OP(hashmap);

    const int n_subarrays = n_elastic_subarrays(hashmap->capacity);
    ElasticSubArray* subarrays = partition_elastic_hashmap(hashmap->capacity);
    if (subarrays == NULL) {
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
        finish_elastic_lookup_stats(hashmap, lookup_probe_start);
#endif
        return (Option_Element_p){.option = None, .element_p = NULL};
    }

    const size_t visited_size = ((size_t)hashmap->capacity + 7) / 8;
    uint8_t* visited_positions = calloc(visited_size, sizeof(uint8_t));
    if (visited_positions == NULL) {
        free(subarrays);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
        finish_elastic_lookup_stats(hashmap, lookup_probe_start);
#endif
        return (Option_Element_p){.option = None, .element_p = NULL};
    }

    Option_Element_p result = {.option = None, .element_p = NULL};
    int remaining_positions = hashmap->capacity;
    for (uint64_t global_probe_number = 1; remaining_positions > 0; global_probe_number++) {
        const int table_index = elastic_lookup_table_index(key, global_probe_number, subarrays, n_subarrays, hashmap->capacity, seed);
        if (table_index < 0) {
            break;
        }

        RECORD_ELASTIC_LOOKUP_PROBE(hashmap);
        Element* candidate = hashmap->table[table_index];
        if (candidate != NULL && strcmp(candidate->key, key) == 0) {
            result = (Option_Element_p){.option = Some, .element_p = candidate};
            break;
        }

        // Repeated positions remain real probes, the bitset is used only to know when a negative lookup is complete
        const size_t byte_index = (size_t)table_index / 8;
        const uint8_t bit_mask = (uint8_t)(1u << (table_index % 8));
        if ((visited_positions[byte_index] & bit_mask) == 0) {
            visited_positions[byte_index] |= bit_mask;
            remaining_positions--;
        }

        if (global_probe_number == UINT64_MAX) {
            break;
        }
    }

    free(visited_positions);
    free(subarrays);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    finish_elastic_lookup_stats(hashmap, lookup_probe_start);
#endif
    return result;
}

void elastic_hashmap_reset_probe_stats(ElasticHashmap* hashmap) {
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    if (hashmap != NULL) {
        hashmap->probe_stats = (ElasticHashmapProbeStats){0};
    }
#else
    (void)hashmap;
#endif
}

ElasticHashmapProbeStats elastic_hashmap_probe_stats(const ElasticHashmap* hashmap) {
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    if (hashmap != NULL) {
        return hashmap->probe_stats;
    }
#else
    (void)hashmap;
#endif
    return (ElasticHashmapProbeStats){0};
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
