//
// Created by miki on 12/08/2026.
//

#include "../include/funnel_hashing.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "hash_functions.h"

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

/// Smallest integer the paper allows after a_i, that is ceil(3a_i/4 - 1), never below one
static int funnel_min_next(const int a) {
    if (3 * a < 4) {
        return 1;
    }
    const int value = (3 * a - 1) / 4;
    return value < 1 ? 1 : value;
}

/// Largest integer the paper allows after a_i, that is floor(3a_i/4 + 1)
static int funnel_max_next(const int a) {
    return (3 * a + 4) / 4;
}

/// Smallest or largest total that "count" further terms can reach starting from a
static long funnel_suffix_bound(int a, const int count, const bool maximum) {
    long total = 0;
    for (int i = 0; i < count; i++) {
        total += a;
        a = maximum ? funnel_max_next(a) : funnel_min_next(a);
    }
    return total;
}

/// Fills a_(index+1) .. a_alpha given a_(index+1) = value, so that they sum to the total
///
/// The successors allowed at each step are every integer in [3a/4 - 1, 3a/4 + 1], which is
/// exactly the paper's a_(i+1) = 3a_i/4 +- 1, so the recurrence holds by construction. The
/// reachable totals of a suffix have gaps, so the bounds below only prune and the search
/// backtracks when a branch cannot be completed.
static bool funnel_fill_from(FunnelPartition* partition, const int index, const int value, const long remaining) {
    partition->subarrays[index].subarray_number = index + 1;
    partition->subarrays[index].n_buckets = value;

    const long left = remaining - value;
    const int successors = partition->alpha - index - 1;
    if (successors == 0) {
        return left == 0;
    }
    if (left <= 0) {
        return false;
    }

    const int lowest = funnel_min_next(value);
    const int highest = funnel_max_next(value);

    // The paper's ideal successor is 3a_i/4 exactly and the +-1 is the freedom needed to make
    // the terms sum to |A'|/beta. Candidates are therefore tried nearest-first, so the freedom
    // is spent only where the total demands it and the sequence stays as geometric as it can.
    int candidates[8];
    int count = 0;
    for (int next = highest; next >= lowest && count < 8; next--) {
        candidates[count++] = next;
    }
    for (int i = 1; i < count; i++) {
        const int candidate = candidates[i];
        const int distance = abs(4 * candidate - 3 * value);
        int j = i - 1;
        while (j >= 0 && abs(4 * candidates[j] - 3 * value) > distance) {
            candidates[j + 1] = candidates[j];
            j--;
        }
        candidates[j + 1] = candidate;
    }

    for (int i = 0; i < count; i++) {
        const int next = candidates[i];
        if (funnel_suffix_bound(next, successors, false) > left
            || funnel_suffix_bound(next, successors, true) < left) {
            continue;
        }
        if (funnel_fill_from(partition, index + 1, next, left)) {
            return true;
        }
    }
    return false;
}

/// Checks the paper's sum_{j>i} |A_j| > 2.5 |A_i| for every i in [alpha - 10]
///
/// The paper reads this off the ideal geometric sequence, but the +-1 freedom can break it
/// once the terms have decayed to single digits, which happens well inside [alpha - 10] at
/// the capacities used here. It is therefore imposed on the search rather than assumed.
static bool funnel_tail_dominates(const FunnelPartition* partition) {
    const int alpha = partition->alpha;
    long tail = 0;
    for (int i = alpha - 1; i >= 0; i--) {
        if (i < alpha - 10 && 2 * tail <= 5 * (long)partition->subarrays[i].n_buckets) {
            return false;
        }
        tail += partition->subarrays[i].n_buckets;
    }
    return true;
}

/// Splits the A' bucket count into a_1 .. a_alpha satisfying a_(i+1) = 3a_i/4 +- 1
static bool allocate_a_subarrays(FunnelPartition* partition, const int total_buckets) {
    if (partition == NULL || partition->subarrays == NULL || partition->alpha <= 0 || partition->beta <= 0 || total_buckets < partition->alpha) {
        return false;
    }
    const int alpha = partition->alpha;

    // The head of a true geometric sequence with ratio 3/4 summing to the total. Heads are
    // tried outward from it, so the sequence built is the one closest to the paper's ideal
    // rather than merely the first that happens to be feasible.
    double geometric_sum = 0.0, weight = 1.0;
    for (int i = 0; i < alpha; i++) {
        geometric_sum += weight;
        weight *= 0.75;
    }
    int ideal_head = (int)(total_buckets / geometric_sum + 0.5);
    if (ideal_head < 1) {
        ideal_head = 1;
    }

    bool built = false;
    for (int offset = 0; offset <= total_buckets && !built; offset++) {
        for (int direction = 0; direction < 2 && !built; direction++) {
            const int head = direction == 0 ? ideal_head + offset : ideal_head - offset;
            if (head < 1 || head > total_buckets || (offset == 0 && direction == 1)) {
                continue;
            }
            if (funnel_suffix_bound(head, alpha, false) > total_buckets
                || funnel_suffix_bound(head, alpha, true) < total_buckets) {
                continue;
            }
            built = funnel_fill_from(partition, 0, head, total_buckets) && funnel_tail_dominates(partition);
        }
    }
    if (!built) {
        return false;
    }

    int starting_index = 0;
    int bucket_sum = 0;
    for (int i = 0; i < alpha; i++) {
        Funnel_A_i* current_subarray = &partition->subarrays[i];
        current_subarray->starting_index = starting_index;
        starting_index += current_subarray->n_buckets * partition->beta;
        bucket_sum += current_subarray->n_buckets;

        if (i + 1 < alpha) {
            const double expected_next = 0.75 * (double)current_subarray->n_buckets;
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

/// Selects the smallest legal A_(alpha+1) size, subject only to the paper's two conditions
static int find_a_alpha_plus_one_length(const int capacity, const double delta, const int beta, const int c_bucket_length) {
    const int minimum_length = (int)ceil(delta * (double)capacity / 2.0);
    const int maximum_length = (int)floor(3.0 * delta * (double)capacity / 4.0);
    if (minimum_length > maximum_length || beta <= 0 || c_bucket_length <= 0) {
        return 0;
    }

    // The paper asks for floor(3dn/4) >= |A_(alpha+1)| >= ceil(dn/2) "with the exact size
    // chosen so that |A'| is divisible by beta", and asks nothing else. Any further
    // congruence would be a third condition inside an interval of width dn/4, which often
    // has no solution; C absorbs its own remainder instead, in c_bucket_lengths below.
    const int distance_to_divisibility = (capacity - minimum_length) % beta;
    const int candidate = minimum_length + distance_to_divisibility;
    if (candidate > maximum_length || candidate / 2 < c_bucket_length) {
        return 0;
    }
    return candidate;
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

    // |C| need not be a multiple of the bucket length. Rather than leave the remainder
    // unreachable, C is split into as equal buckets as the slots allow: since the count is
    // floor(|C| / c_bucket_length), every bucket still holds at least the 2 log log n slots
    // the overflow argument needs, and the lengths differ from each other by at most one.
    const int base_bucket_length = partition->c.length / partition->c_bucket_count;
    const int longer_buckets = partition->c.length % partition->c_bucket_count;
    int bucket_start = partition->c.starting_index;
    for (int i = 0; i < partition->c_bucket_count; i++) {
        const int bucket_length = base_bucket_length + (i < longer_buckets ? 1 : 0);
        partition->c_buckets[i] = (Funnel_Ci_bucket){
            .subarray_number = i + 1, .starting_index = bucket_start,
            .length = bucket_length, .size = 0};
        bucket_start += bucket_length;
    }
    if (bucket_start != partition->c.starting_index + partition->c.length) {
        delete_funnel_partition(partition);
        return NULL;
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
    const int longest = first_bucket->length > second_bucket->length ? first_bucket->length : second_bucket->length;
    for (int slot = 0; slot < longest; slot++) {
        if (slot < first_bucket->length) {
            const int first_index = first_bucket->starting_index + slot;
            RECORD_FUNNEL_INSERT_PROBE(hashmap);
            if (hashmap->table[first_index] == NULL) {
                hashmap->table[first_index] = element;
                first_bucket->size++;
                partition->c.size++;
                return true;
            }
        }

        if (slot < second_bucket->length) {
            const int second_index = second_bucket->starting_index + slot;
            RECORD_FUNNEL_INSERT_PROBE(hashmap);
            if (hashmap->table[second_index] == NULL) {
                hashmap->table[second_index] = element;
                second_bucket->size++;
                partition->c.size++;
                return true;
            }
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
    const Funnel_Ci_bucket* first_c = &partition->c_buckets[first_bucket];
    const Funnel_Ci_bucket* second_c = &partition->c_buckets[second_bucket];
    const int longest_c = first_c->length > second_c->length ? first_c->length : second_c->length;
    for (int slot = 0; slot < longest_c; slot++) {
        if (slot < first_c->length) {
            const int first_index = first_c->starting_index + slot;
            RECORD_FUNNEL_LOOKUP_PROBE(hashmap);
            Element* candidate = hashmap->table[first_index];
            if (candidate == NULL) {
                goto lookup_finished;
            }
            result = compare_funnel_candidate(candidate, key);
            if (result.option == Some) {
                goto lookup_finished;
            }
        }

        if (slot < second_c->length) {
            const int second_index = second_c->starting_index + slot;
            RECORD_FUNNEL_LOOKUP_PROBE(hashmap);
            Element* candidate = hashmap->table[second_index];
            if (candidate == NULL) {
                goto lookup_finished;
            }
            result = compare_funnel_candidate(candidate, key);
            if (result.option == Some) {
                goto lookup_finished;
            }
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
