//
// Created by miki on 12/08/2026.
//

#include "../include/funnel_hashing.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "hash_functions.h"

typedef struct FunnelBucketRemainder {
    int index;
    double fraction;
} FunnelBucketRemainder;

/// Domain prefixes keep logically independent Funnel choices from reusing one SipHash input
/// The low bits contain an Ai number, a B attempt number or one of the two C choices
/// These values are hash-input labels, not table indices, capacities or additional seeds
///
/// They prevent A1, B attempt 1 and C choice 1 from accidentally hashing the identical (key,1) input
static const uint64_t FUNNEL_A_HASH_DOMAIN = UINT64_C(0x2000000000000000);
static const uint64_t FUNNEL_B_HASH_DOMAIN = UINT64_C(0x4000000000000000);
static const uint64_t FUNNEL_C_HASH_DOMAIN = UINT64_C(0x6000000000000000);

/// Probe recording is removed completely by the preprocessor in the normal build
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
#define RECORD_FUNNEL_INSERT_OP(hashmap) ((hashmap)->probe_stats.insertion_ops++)
#define RECORD_FUNNEL_INSERT_PROBE(hashmap) ((hashmap)->probe_stats.insertion_probes++)
#define RECORD_FUNNEL_LOOKUP_OP(hashmap) (((FunnelHashMap*)(hashmap))->probe_stats.lookup_ops++)
#define RECORD_FUNNEL_LOOKUP_PROBE(hashmap) (((FunnelHashMap*)(hashmap))->probe_stats.lookup_probes++)
#define RECORD_FUNNEL_EVENT(hashmap, event) ((hashmap)->probe_stats.event++)
#else
#define RECORD_FUNNEL_INSERT_OP(hashmap) ((void)0)
#define RECORD_FUNNEL_INSERT_PROBE(hashmap) ((void)0)
#define RECORD_FUNNEL_LOOKUP_OP(hashmap) ((void)0)
#define RECORD_FUNNEL_LOOKUP_PROBE(hashmap) ((void)0)
#define RECORD_FUNNEL_EVENT(hashmap, event) ((void)0)
#endif

/// Checks the delta assumption used throughout the Funnel Hashing proof
static bool valid_funnel_delta(const double delta) {
    return isfinite(delta) && delta > 0.0 && delta <= 0.125;
}

int funnel_alpha(const double delta) {
    if (!valid_funnel_delta(delta)) {
        return 0;
    }

    const double value = ceil(4.0 * -log2(delta));
    if (value > INT_MAX - 10) {
        return 0;
    }
    return (int)value + 10;
}

int funnel_beta(const double delta) {
    if (!valid_funnel_delta(delta)) {
        return 0;
    }

    const double value = ceil(2.0 * -log2(delta));
    if (value > INT_MAX) {
        return 0;
    }
    return (int)value;
}

/// Sorts fractional remainders from largest to smallest and uses the lower Ai first for ties
static int compare_bucket_remainders(const void* left, const void* right) {
    const FunnelBucketRemainder* left_remainder = left;
    const FunnelBucketRemainder* right_remainder = right;

    if (left_remainder->fraction < right_remainder->fraction) {
        return 1;
    }
    if (left_remainder->fraction > right_remainder->fraction) {
        return -1;
    }
    if (left_remainder->index < right_remainder->index) {
        return -1;
    }
    return left_remainder->index > right_remainder->index;
}

/// Splits the A' bucket count using weights 1, 3/4, (3/4)^2 and so on
static bool allocate_a_subarrays(FunnelPartition* partition, const int total_buckets) {
    if (partition == NULL || partition->subarrays == NULL || partition->alpha <= 0 || partition->beta <= 0 || total_buckets < partition->alpha) {
        return false;
    }

    FunnelBucketRemainder* remainders = malloc((size_t)partition->alpha * sizeof(FunnelBucketRemainder));
    if (remainders == NULL) {
        return false;
    }

    double weight = 1.0;
    double weight_sum = 0.0;
    for (int i = 0; i < partition->alpha; i++) {
        weight_sum += weight;
        weight *= 0.75;
    }

    const int distributable_buckets = total_buckets - partition->alpha;
    int allocated_buckets = 0;
    weight = 1.0;
    for (int i = 0; i < partition->alpha; i++) {
        const double exact_extra = (double)distributable_buckets * weight / weight_sum;
        const int extra_buckets = (int)floor(exact_extra);
        partition->subarrays[i].subarray_number = i + 1;
        partition->subarrays[i].n_buckets = 1 + extra_buckets;
        allocated_buckets += partition->subarrays[i].n_buckets;
        remainders[i] = (FunnelBucketRemainder){.index = i, .fraction = exact_extra - extra_buckets};
        weight *= 0.75;
    }

    const int buckets_left = total_buckets - allocated_buckets;
    if (buckets_left < 0 || buckets_left > partition->alpha) {
        free(remainders);
        return false;
    }

    qsort(remainders, (size_t)partition->alpha, sizeof(FunnelBucketRemainder), compare_bucket_remainders);
    for (int i = 0; i < buckets_left; i++) {
        partition->subarrays[remainders[i].index].n_buckets++;
    }
    free(remainders);

    int starting_index = 0;
    int bucket_sum = 0;
    for (int i = 0; i < partition->alpha; i++) {
        Funnel_A_i* current = &partition->subarrays[i];
        current->starting_index = starting_index;
        starting_index += current->n_buckets * partition->beta;
        bucket_sum += current->n_buckets;

        if (i + 1 < partition->alpha) {
            const double expected_next = 0.75 * (double)current->n_buckets;
            if (fabs((double)partition->subarrays[i + 1].n_buckets - expected_next) > 1.0) {
                return false;
            }
        }
    }

    return bucket_sum == total_buckets && starting_index == partition->a_prime_length;
}

/// Rounds one of the log log n parameters to a usable number of slots
static int funnel_log_log_parameter(const int capacity, const double multiplier) {
    if (capacity <= 2) {
        return 1;
    }

    const double value = ceil(multiplier * log2(log2((double)capacity)));
    if (!isfinite(value) || value > INT_MAX) {
        return 0;
    }
    return value < 1.0 ? 1 : (int)value;
}

/// Returns the greatest common divisor used to bound the A_(alpha+1) size congruence search
static int greatest_common_divisor(int left, int right) {
    while (right != 0) {
        const int remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

/// Selects a legal A_(alpha+1) size which also gives C equal-sized buckets
static int find_a_alpha_plus_one_length(const int capacity, const double delta, const int beta, const int c_bucket_length) {
    const int minimum_length = (int)ceil(delta * (double)capacity / 2.0);
    const int maximum_length = (int)floor(3.0 * delta * (double)capacity / 4.0);
    if (minimum_length > maximum_length || beta <= 0 || c_bucket_length <= 0) {
        return 0;
    }

    const int distance_to_divisibility = (capacity - minimum_length) % beta;
    int candidate = minimum_length + distance_to_divisibility;
    const int c_length_period = 2 * c_bucket_length;
    const int candidate_period = c_length_period / greatest_common_divisor(beta, c_length_period);

    // C receives floor(A_(alpha+1)/2) slots so its length must be divisible by the bucket size
    for (int attempt = 0; attempt < candidate_period && candidate <= maximum_length; attempt++) {
        if (candidate / 2 >= c_bucket_length && (candidate / 2) % c_bucket_length == 0) {
            return candidate;
        }
        candidate += beta;
    }
    return 0;
}

FunnelPartition* partition_funnel_hashmap(const int capacity, const double delta) {
    const int alpha = funnel_alpha(delta);
    const int beta = funnel_beta(delta);
    if (capacity <= 0 || alpha == 0 || beta == 0) {
        return NULL;
    }

    const int b_probe_limit = funnel_log_log_parameter(capacity, 1.0);
    const int c_bucket_length = funnel_log_log_parameter(capacity, 2.0);
    const int a_alpha_plus_one_length = find_a_alpha_plus_one_length(capacity, delta, beta, c_bucket_length);
    if (b_probe_limit == 0 || c_bucket_length == 0 || a_alpha_plus_one_length == 0) {
        return NULL;
    }

    const int a_prime_length = capacity - a_alpha_plus_one_length;
    const int total_buckets = a_prime_length / beta;
    if (total_buckets < alpha) {
        return NULL;
    }

    FunnelPartition* partition = malloc(sizeof(FunnelPartition));
    if (partition == NULL) {
        return NULL;
    }

    *partition = (FunnelPartition) {
        .alpha = alpha,
        .beta = beta,
        .a_prime_length = a_prime_length,
        .a_alpha_plus_one_length = a_alpha_plus_one_length,
        .b_probe_limit = b_probe_limit,
        .c_bucket_length = c_bucket_length,
        .c_bucket_count = (a_alpha_plus_one_length / 2) / c_bucket_length,
        .subarrays = calloc((size_t)alpha, sizeof(Funnel_A_i)),
        .c_buckets = calloc((size_t)((a_alpha_plus_one_length / 2) / c_bucket_length), sizeof(Funnel_Ci_bucket)),
    };
    if (partition->subarrays == NULL || partition->c_buckets == NULL || !allocate_a_subarrays(partition, total_buckets)) {
        delete_funnel_partition(partition);
        return NULL;
    }

    const int b_length = a_alpha_plus_one_length / 2 + a_alpha_plus_one_length % 2;
    partition->b = (Funnel_B){.starting_index = a_prime_length, .length = b_length, .size = 0};
    partition->c = (Funnel_C){.starting_index = a_prime_length + b_length, .length = a_alpha_plus_one_length - b_length, .size = 0};
    for (int i = 0; i < partition->c_bucket_count; i++) {
        partition->c_buckets[i] = (Funnel_Ci_bucket){.subarray_number = i + 1, .starting_index = partition->c.starting_index + i * c_bucket_length, .size = 0};
    }
    return partition;
}

void delete_funnel_partition(FunnelPartition* partition) {
    if (partition != NULL) {
        free(partition->subarrays);
        free(partition->c_buckets);
        free(partition);
    }
}

FunnelHashMap* create_funnel_hashmap(const int capacity) {
    if (capacity <= 0) {
        return NULL;
    }

    FunnelHashMap* hashmap = malloc(sizeof(FunnelHashMap));
    if (hashmap == NULL) {
        return NULL;
    }

    *hashmap = (FunnelHashMap){.capacity = capacity, .size = 0, .table = calloc((size_t)capacity, sizeof(Element*))};
    if (hashmap->table == NULL) {
        free(hashmap);
        return NULL;
    }
    return hashmap;
}

/// Produces one domain-separated pseudo-random draw for a Funnel probe decision
static uint64_t funnel_hash_draw(const char* key, const uint64_t domain, const uint64_t draw_number, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    return siphash_probe64(key, domain | draw_number, seed);
}

/// Returns the first physical index of the one bucket selected for a key in Ai
static int selected_a_bucket_start(const FunnelPartition* partition, const int subarray_index, const char* key, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    const Funnel_A_i* subarray = &partition->subarrays[subarray_index];
    const uint64_t draw = funnel_hash_draw(key, FUNNEL_A_HASH_DOMAIN, (uint64_t)subarray->subarray_number, seed);
    const int bucket_index = (int)(draw % (uint64_t)subarray->n_buckets);
    return subarray->starting_index + bucket_index * partition->beta;
}

/// Attempts the beta greedy probes in the one Aij bucket selected inside Ai
static bool attempt_funnel_a_insertion(FunnelHashMap* hashmap, const FunnelPartition* partition, const int subarray_index, Element* element, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    const int bucket_start = selected_a_bucket_start(partition, subarray_index, element->key, seed);

    // Funnel scans the complete selected bucket before moving to the next Ai
    for (int slot = 0; slot < partition->beta; slot++) {
        const int table_index = bucket_start + slot;
        RECORD_FUNNEL_INSERT_PROBE(hashmap);
        if (hashmap->table[table_index] == NULL) {
            hashmap->table[table_index] = element;
            return true;
        }
    }
    return false;
}

/// Attempts at most ceil(log log n) independent uniform probes in B
static bool attempt_funnel_b_insertion(FunnelHashMap* hashmap, FunnelPartition* partition, Element* element, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    for (int attempt = 1; attempt <= partition->b_probe_limit; attempt++) {
        const uint64_t draw = funnel_hash_draw(element->key, FUNNEL_B_HASH_DOMAIN, (uint64_t)attempt, seed);
        const int table_index = partition->b.starting_index + (int)(draw % (uint64_t)partition->b.length);
        RECORD_FUNNEL_INSERT_PROBE(hashmap);
        if (hashmap->table[table_index] == NULL) {
            hashmap->table[table_index] = element;
            partition->b.size++;
            return true;
        }
    }
    return false;
}

/// Selects one of the two independent C bucket choices
static int selected_c_bucket(const FunnelPartition* partition, const char* key, const int choice, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    const uint64_t draw = funnel_hash_draw(key, FUNNEL_C_HASH_DOMAIN, (uint64_t)choice, seed);
    return (int)(draw % (uint64_t)partition->c_bucket_count);
}

/// Alternates between two C buckets and inserts into the first empty position
static bool attempt_funnel_c_insertion(FunnelHashMap* hashmap, FunnelPartition* partition, Element* element, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    const int first_bucket_index = selected_c_bucket(partition, element->key, 1, seed);
    const int second_bucket_index = selected_c_bucket(partition, element->key, 2, seed);
    Funnel_Ci_bucket* first_bucket = &partition->c_buckets[first_bucket_index];
    Funnel_Ci_bucket* second_bucket = &partition->c_buckets[second_bucket_index];

    // Alternation chooses the less full bucket and uses the first bucket to break ties
    for (int slot = 0; slot < partition->c_bucket_length; slot++) {
        const int first_index = first_bucket->starting_index + slot;
        RECORD_FUNNEL_INSERT_PROBE(hashmap);
        if (hashmap->table[first_index] == NULL) {
            hashmap->table[first_index] = element;
            first_bucket->size++;
            partition->c.size++;
            return true;
        }

        const int second_index = second_bucket->starting_index + slot;
        RECORD_FUNNEL_INSERT_PROBE(hashmap);
        if (hashmap->table[second_index] == NULL) {
            hashmap->table[second_index] = element;
            second_bucket->size++;
            partition->c.size++;
            return true;
        }
    }
    return false;
}

#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
/// Records the probe count of one completed Funnel insertion
static void finish_funnel_insertion_stats(FunnelHashMap* hashmap, const uint64_t probe_start) {
    const uint64_t operation_probes = hashmap->probe_stats.insertion_probes - probe_start;
    if (operation_probes > hashmap->probe_stats.maximum_insertion_probes) {
        hashmap->probe_stats.maximum_insertion_probes = operation_probes;
    }
}

/// Records the probe count of one completed Funnel lookup
static void finish_funnel_lookup_stats(const FunnelHashMap* hashmap, const uint64_t probe_start) {
    FunnelHashMap* mutable_hashmap = (FunnelHashMap*)hashmap;
    const uint64_t operation_probes = mutable_hashmap->probe_stats.lookup_probes - probe_start;
    if (operation_probes > mutable_hashmap->probe_stats.maximum_lookup_probes) {
        mutable_hashmap->probe_stats.maximum_lookup_probes = operation_probes;
    }
}
#endif

/// Follows the complete greedy insertion sequence for one element
static bool insert_one_funnel_element(FunnelHashMap* hashmap, FunnelPartition* partition, Element* element, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    RECORD_FUNNEL_INSERT_OP(hashmap);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const uint64_t insertion_probe_start = hashmap->probe_stats.insertion_probes;
#endif
    for (int i = 0; i < partition->alpha; i++) {
        if (attempt_funnel_a_insertion(hashmap, partition, i, element, seed)) {
            RECORD_FUNNEL_EVENT(hashmap, insertions_in_a);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
            finish_funnel_insertion_stats(hashmap, insertion_probe_start);
#endif
            return true;
        }
    }
    if (attempt_funnel_b_insertion(hashmap, partition, element, seed)) {
        RECORD_FUNNEL_EVENT(hashmap, insertions_in_b);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
        finish_funnel_insertion_stats(hashmap, insertion_probe_start);
#endif
        return true;
    }
    if (attempt_funnel_c_insertion(hashmap, partition, element, seed)) {
        RECORD_FUNNEL_EVENT(hashmap, insertions_in_c);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
        finish_funnel_insertion_stats(hashmap, insertion_probe_start);
#endif
        return true;
    }

    RECORD_FUNNEL_EVENT(hashmap, insertion_failures);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    finish_funnel_insertion_stats(hashmap, insertion_probe_start);
#endif
    return false;
}

bool insert_element_funnel_hashmap(FunnelHashMap* hashmap, const double delta, Element* element, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    if (hashmap == NULL || hashmap->table == NULL || hashmap->capacity <= 0 || hashmap->size >= hashmap->capacity || element == NULL || element->key == NULL || seed == NULL) {
        return false;
    }

    FunnelPartition* partition = partition_funnel_hashmap(hashmap->capacity, delta);
    if (partition == NULL) {
        return false;
    }

    const bool inserted = insert_one_funnel_element(hashmap, partition, element, seed);
    if (inserted) {
        hashmap->size++;
    }
    delete_funnel_partition(partition);
    return inserted;
}

bool batch_insert_funnel_hashmap(FunnelHashMap* hashmap, const double delta, Element** elements, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    if (hashmap == NULL || hashmap->table == NULL || hashmap->capacity <= 0 || hashmap->size != 0 || elements == NULL || seed == NULL) {
        return false;
    }

    for (int i = 0; i < hashmap->capacity; i++) {
        if (hashmap->table[i] != NULL) {
            return false;
        }
    }

#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    hashmap->probe_stats = (FunnelHashmapProbeStats){0};
#endif

    FunnelPartition* partition = partition_funnel_hashmap(hashmap->capacity, delta);
    if (partition == NULL) {
        return false;
    }

    const int insertion_count = hashmap->capacity - (int)floor(delta * (double)hashmap->capacity);
    for (int i = 0; i < insertion_count; i++) {
        if (elements[i] == NULL || elements[i]->key == NULL || !insert_one_funnel_element(hashmap, partition, elements[i], seed)) {
            delete_funnel_partition(partition);
            return false;
        }

        // Ownership moves to the table only after one stage accepts the element
        elements[i] = NULL;
        hashmap->size++;
    }

    delete_funnel_partition(partition);
    return true;
}

/// Returns Some for a matching candidate and None for every other occupied candidate
static Option_Element_p compare_funnel_candidate(Element* candidate, const char* key) {
    if (candidate != NULL && strcmp(candidate->key, key) == 0) {
        return (Option_Element_p){.option = Some, .element_p = candidate};
    }
    return (Option_Element_p){.option = None, .element_p = NULL};
}

Option_Element_p retrieve_element_funnel_hashmap_with_partition(const FunnelHashMap* hashmap, const FunnelPartition* partition, const char* key, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    if (hashmap == NULL || hashmap->table == NULL || hashmap->capacity <= 0 || partition == NULL || partition->a_prime_length + partition->a_alpha_plus_one_length != hashmap->capacity || key == NULL || seed == NULL) {
        return (Option_Element_p){.option = None, .element_p = NULL};
    }

#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const uint64_t lookup_probe_start = hashmap->probe_stats.lookup_probes;
#endif
    RECORD_FUNNEL_LOOKUP_OP(hashmap);
    Option_Element_p result = {.option = None, .element_p = NULL};
    for (int i = 0; i < partition->alpha; i++) {
        const int bucket_start = selected_a_bucket_start(partition, i, key, seed);
        for (int slot = 0; slot < partition->beta; slot++) {
            RECORD_FUNNEL_LOOKUP_PROBE(hashmap);
            Element* candidate = hashmap->table[bucket_start + slot];
            if (candidate == NULL) {
                goto lookup_finished;
            }
            result = compare_funnel_candidate(candidate, key);
            if (result.option == Some) {
                goto lookup_finished;
            }
        }
    }

    for (int attempt = 1; attempt <= partition->b_probe_limit; attempt++) {
        const uint64_t draw = funnel_hash_draw(key, FUNNEL_B_HASH_DOMAIN, (uint64_t)attempt, seed);
        const int table_index = partition->b.starting_index + (int)(draw % (uint64_t)partition->b.length);
        RECORD_FUNNEL_LOOKUP_PROBE(hashmap);
        Element* candidate = hashmap->table[table_index];
        if (candidate == NULL) {
            goto lookup_finished;
        }
        result = compare_funnel_candidate(candidate, key);
        if (result.option == Some) {
            goto lookup_finished;
        }
    }

    const int first_bucket = selected_c_bucket(partition, key, 1, seed);
    const int second_bucket = selected_c_bucket(partition, key, 2, seed);
    for (int slot = 0; slot < partition->c_bucket_length; slot++) {
        const int first_index = partition->c_buckets[first_bucket].starting_index + slot;
        RECORD_FUNNEL_LOOKUP_PROBE(hashmap);
        Element* candidate = hashmap->table[first_index];
        if (candidate == NULL) {
            goto lookup_finished;
        }
        result = compare_funnel_candidate(candidate, key);
        if (result.option == Some) {
            goto lookup_finished;
        }

        const int second_index = partition->c_buckets[second_bucket].starting_index + slot;
        RECORD_FUNNEL_LOOKUP_PROBE(hashmap);
        candidate = hashmap->table[second_index];
        if (candidate == NULL) {
            goto lookup_finished;
        }
        result = compare_funnel_candidate(candidate, key);
        if (result.option == Some) {
            goto lookup_finished;
        }
    }

lookup_finished:
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    finish_funnel_lookup_stats(hashmap, lookup_probe_start);
#endif
    return result;
}

Option_Element_p retrieve_element_funnel_hashmap(const FunnelHashMap* hashmap, const double delta, const char* key, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    if (hashmap == NULL || hashmap->table == NULL || hashmap->capacity <= 0 || key == NULL || seed == NULL) {
        return (Option_Element_p){.option = None, .element_p = NULL};
    }

    FunnelPartition* partition = partition_funnel_hashmap(hashmap->capacity, delta);
    if (partition == NULL) {
        return (Option_Element_p){.option = None, .element_p = NULL};
    }

    const Option_Element_p result = retrieve_element_funnel_hashmap_with_partition(hashmap, partition, key, seed);
    delete_funnel_partition(partition);
    return result;
}

void funnel_hashmap_reset_probe_stats(FunnelHashMap* hashmap) {
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    if (hashmap != NULL) {
        hashmap->probe_stats = (FunnelHashmapProbeStats){0};
    }
#else
    (void)hashmap;
#endif
}

FunnelHashmapProbeStats funnel_hashmap_probe_stats(const FunnelHashMap* hashmap) {
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    if (hashmap != NULL) {
        return hashmap->probe_stats;
    }
#else
    (void)hashmap;
#endif
    return (FunnelHashmapProbeStats){0};
}

void delete_funnel_hashmap(FunnelHashMap* hashmap) {
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
