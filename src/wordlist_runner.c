#include "wordlist_runner.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "api_hashmap.h"

typedef struct WordList {
    char** words;
    size_t size;
    size_t capacity;
    size_t token_count;
} WordList;

typedef enum BenchmarkOutputMode {
    BenchmarkDetailed,
    BenchmarkLoadSweep,
    BenchmarkCSweep,
    BenchmarkCsv,
} BenchmarkOutputMode;

static const float LOAD_SWEEP_DELTAS[] = {0.125f, 0.03125f, 0.0078125f, 0.00390625f};

typedef struct BenchmarkMeasurement {
    const char* implementation;
    const char* dataset;
    const uint8_t* seed;
    int capacity;
    size_t key_count;
    double delta;
    double c;
    bool correct;
    double insertion_seconds;
    double lookup_seconds;
    double negative_lookup_seconds;
    uint64_t insertion_ops;
    uint64_t insertion_probes;
    uint64_t maximum_insertion_probes;
    uint64_t lookup_ops;
    uint64_t lookup_probes;
    uint64_t maximum_lookup_probes;
    uint64_t negative_lookup_probes;
    uint64_t batch_zero_insertions;
    uint64_t batch_zero_probes;
    uint64_t case_one_insertions;
    uint64_t case_one_fallbacks;
    uint64_t case_one_probes;
    uint64_t case_two_insertions;
    uint64_t case_two_probes;
    uint64_t case_three_insertions;
    uint64_t case_three_probes;
    uint64_t maximum_case_three_probes;
    uint64_t funnel_insertions_in_a;
    uint64_t funnel_insertions_in_b;
    uint64_t funnel_insertions_in_c;
    uint64_t funnel_insertion_failures;
} BenchmarkMeasurement;

/// Frees every word and resets the wordlist to an empty state
static void delete_wordlist(WordList* wordlist) {
    if (wordlist == NULL) {
        return;
    }

    for (size_t i = 0; i < wordlist->size; i++) {
        free(wordlist->words[i]);
    }
    free(wordlist->words);
    *wordlist = (WordList){0};
}

/// Expands a byte buffer until it can hold required_size bytes
static bool grow_buffer(char** buffer, size_t* capacity, const size_t required_size) {
    if (buffer == NULL || capacity == NULL) {
        return false;
    }

    size_t new_capacity = *capacity == 0 ? 64 : *capacity;
    while (new_capacity < required_size) {
        if (new_capacity > SIZE_MAX / 2) {
            return false;
        }
        new_capacity *= 2;
    }

    char* larger_buffer = realloc(*buffer, new_capacity);
    if (larger_buffer == NULL) {
        return false;
    }

    *buffer = larger_buffer;
    *capacity = new_capacity;
    return true;
}

/// Adds one owned word pointer to the end of a wordlist
static bool append_word(WordList* wordlist, char* word) {
    if (wordlist == NULL || word == NULL) {
        return false;
    }

    if (wordlist->size == wordlist->capacity) {
        if (wordlist->capacity > SIZE_MAX / 2 || wordlist->capacity * 2 > SIZE_MAX / sizeof(char*)) {
            return false;
        }

        const size_t new_capacity = wordlist->capacity == 0 ? 64 : wordlist->capacity * 2;
        char** larger_words = realloc(wordlist->words, new_capacity * sizeof(char*));
        if (larger_words == NULL) {
            return false;
        }

        wordlist->words = larger_words;
        wordlist->capacity = new_capacity;
    }

    wordlist->words[wordlist->size++] = word;
    return true;
}

/// Loads whitespace-separated tokens and keeps the first occurrence of every word
static bool load_unique_words(const char* path, WordList* wordlist) {
    if (path == NULL || wordlist == NULL) {
        return false;
    }

    FILE* file = fopen(path, "r");
    if (file == NULL) {
        perror(path);
        return false;
    }

    ApiHashmap* seen_words = create_api_hashmap();
    char* token = NULL;
    size_t token_capacity = 0;
    bool success = seen_words != NULL;
    bool reached_end = false;

    while (success && !reached_end) {
        int character = fgetc(file);
        while (character != EOF && isspace((unsigned char)character)) {
            character = fgetc(file);
        }
        if (character == EOF) {
            break;
        }

        size_t token_length = 0;
        do {
            if (!grow_buffer(&token, &token_capacity, token_length + 2)) {
                success = false;
                break;
            }
            token[token_length++] = (char)character;
            character = fgetc(file);
        } while (character != EOF && !isspace((unsigned char)character));

        if (!success) {
            break;
        }
        reached_end = character == EOF;

        token[token_length] = '\0';
        wordlist->token_count++;
        if (is_none(retrieve_element_api_hashmap(seen_words, token).option)) {
            if (wordlist->size >= (size_t)INT_MAX || insert_element_api_hashmap(seen_words, token, (int)wordlist->size) == NULL || !append_word(wordlist, token)) {
                success = false;
                break;
            }
            token = NULL;
            token_capacity = 0;
        }
    }

    if (ferror(file)) {
        perror(path);
        success = false;
    }

    free(token);
    destroy_api_hashmap(seen_words);
    fclose(file);
    return success;
}

/// Returns the exact number of insertions performed by batch_insert for one capacity
static int elastic_target_size(const int capacity, const float delta) {
    return capacity - (int)floor((double)delta * (double)capacity);
}

/// Finds the largest capacity whose Elastic target size is exactly the number of unique words
static bool elastic_capacity_for_size(const size_t word_count, const float delta, int* capacity) {
    if (word_count == 0 || word_count > (size_t)INT_MAX || !isfinite(delta) || delta <= 0.0f || delta >= 1.0f || capacity == NULL) {
        return false;
    }

    int lower = (int)word_count;
    int upper = lower;
    while (elastic_target_size(upper, delta) <= (int)word_count) {
        if (upper == INT_MAX) {
            if (elastic_target_size(upper, delta) == (int)word_count) {
                *capacity = upper;
                return true;
            }
            return false;
        }
        upper = upper > INT_MAX / 2 ? INT_MAX : upper * 2;
    }

    // Find the first capacity for the following target size then step back once
    while (lower + 1 < upper) {
        const int middle = lower + (upper - lower) / 2;
        if (elastic_target_size(middle, delta) <= (int)word_count) {
            lower = middle;
        } else {
            upper = middle;
        }
    }

    if (elastic_target_size(lower, delta) != (int)word_count) {
        return false;
    }

    *capacity = lower;
    return true;
}

/// Allocates an independent copy of one null terminated word
static char* copy_word(const char* word) {
    const size_t length = strlen(word) + 1;
    char* copy = malloc(length);
    if (copy != NULL) {
        memcpy(copy, word, length);
    }
    return copy;
}

/// Creates a key which is guaranteed not to occur in the loaded wordlist
static char* choose_missing_word(const WordList* wordlist) {
    static const char prefix[] = "__hashmaps_missing_word_";
    char candidate[sizeof(prefix) + 31];

    for (uint64_t suffix = 0;; suffix++) {
        const int written = snprintf(candidate, sizeof(candidate), "%s%llu", prefix, (unsigned long long)suffix);
        if (written < 0 || (size_t)written >= sizeof(candidate)) {
            return NULL;
        }

        bool found = false;
        for (size_t i = 0; i < wordlist->size; i++) {
            if (strcmp(wordlist->words[i], candidate) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return copy_word(candidate);
        }
        if (suffix == UINT64_MAX) {
            return NULL;
        }
    }
}

/// Converts a clock interval into process CPU seconds
static double elapsed_seconds(const clock_t start, const clock_t end) {
    return (double)(end - start) / (double)CLOCKS_PER_SEC;
}

/// Starts one compact observed-versus-theory result table
static void print_scorecard_header(const char* name) {
    printf("\n[%s]\n", name);
    printf("%-34s | %-38s | %s\n", "metric", "observed", "theoretical reference");
    printf("-----------------------------------+----------------------------------------+------------------------------------------\n");
}

/// Prints one row in a result table
static void print_scorecard_row(const char* metric, const char* observed, const char* theoretical_reference) {
    printf("%-34s | %-38s | %s\n", metric, observed, theoretical_reference);
}

/// Checks the theorem precondition delta = 1 / 2^k using the binary representation of a float
static bool delta_inverse_is_power_of_two(const float delta) {
    int exponent = 0;
    return frexpf(delta, &exponent) == 0.5f;
}

/// Frees elements which were not transferred to an experimental hashmap
static void delete_unconsumed_elements(Element** elements, const size_t count) {
    if (elements == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        if (elements[i] != NULL) {
            free((char*)elements[i]->key);
            free(elements[i]);
        }
    }
    free(elements);
}

/// Allocates one owned Element copy for every unique word
static Element** create_elements(const WordList* wordlist) {
    if (wordlist == NULL || wordlist->size == 0) {
        return NULL;
    }

    Element** elements = calloc(wordlist->size, sizeof(Element*));
    if (elements == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < wordlist->size; i++) {
        elements[i] = malloc(sizeof(Element));
        if (elements[i] == NULL) {
            delete_unconsumed_elements(elements, wordlist->size);
            return NULL;
        }

        elements[i]->key = copy_word(wordlist->words[i]);
        elements[i]->value = (int)i;
        if (elements[i]->key == NULL) {
            delete_unconsumed_elements(elements, wordlist->size);
            return NULL;
        }
    }
    return elements;
}

/// Returns the log2(1 / delta) scale used in the theoretical bounds
static double paper_log_delta_scale(const double delta) {
    return log2(1.0 / delta);
}

/// Returns the log2(log2(n)) scale used in the high probability bounds
static double paper_log_log_scale(const int capacity) {
    return capacity <= 2 ? 0.0 : log2(log2((double)capacity));
}

#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
/// Returns the arithmetic mean of one probe counter or zero when no operation ran
static double probes_per_operation(const uint64_t probes, const uint64_t operations) {
    return operations == 0 ? 0.0 : (double)probes / (double)operations;
}

/// Converts one event counter into a percentage of its parent operation counter
static double operation_percentage(const uint64_t operations, const uint64_t total_operations) {
    return total_operations == 0 ? 0.0 : 100.0 * (double)operations / (double)total_operations;
}

/// Reconstructs the phi index assigned to the slot where one Elastic key was placed
static uint64_t elastic_placement_phi(const char* key, const int physical_index, const ElasticSubArray* subarrays, const int subarray_count, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    for (int i = 0; i < subarray_count; i++) {
        const int subarray_end = subarrays[i].starting_index + subarrays[i].length;
        if (physical_index < subarrays[i].starting_index || physical_index >= subarray_end) {
            continue;
        }

        for (uint64_t local_probe = 1;; local_probe++) {
            const uint64_t global_probe = elastic_phi((uint64_t)i + 1, local_probe);
            if (global_probe == 0) {
                return 0;
            }

            const int relative_index = (int)(siphash_probe64(key, global_probe, seed) % (uint64_t)subarrays[i].length);
            if (subarrays[i].starting_index + relative_index == physical_index) {
                return global_probe;
            }
            if (local_probe == UINT64_MAX) {
                return 0;
            }
        }
    }
    return 0;
}

/// Builds a direct value-to-slot index for the elements created by this runner
static int* elastic_positions_by_value(const ElasticHashmap* hashmap, const size_t element_count) {
    int* positions = malloc(element_count * sizeof(int));
    if (positions == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < element_count; i++) {
        positions[i] = -1;
    }
    for (int slot = 0; slot < hashmap->capacity; slot++) {
        Element* element = hashmap->table[slot];
        if (element != NULL && element->value >= 0 && (size_t)element->value < element_count) {
            positions[element->value] = slot;
        }
    }
    return positions;
}

/// Prints the final occupancy and vacancy of every physical Elastic subarray
static void print_elastic_occupancy(const ElasticHashmap* hashmap, const double delta) {
    ElasticSubArray* subarrays = partition_elastic_hashmap(hashmap->capacity);
    if (subarrays == NULL) {
        return;
    }

    const int subarray_count = n_elastic_subarrays(hashmap->capacity);
    print_scorecard_header("elastic subarrays");
    for (int i = 0; i < subarray_count; i++) {
        int occupied = 0;
        for (int slot = 0; slot < subarrays[i].length; slot++) {
            if (hashmap->table[subarrays[i].starting_index + slot] != NULL) {
                occupied++;
            }
        }
        const double vacancy = (double)(subarrays[i].length - occupied) / (double)subarrays[i].length;
        const int completed_target_empty = (int)floor(delta * (double)subarrays[i].length / 2.0);
        const int completed_target_occupied = subarrays[i].length - completed_target_empty;
        const double completed_target_vacancy = (double)completed_target_empty / (double)subarrays[i].length;
        char name[16];
        char observed[96];
        char theoretical_reference[128];
        snprintf(name, sizeof(name), "A%d", i + 1);
        snprintf(observed, sizeof(observed), "%d / %d, vacancy %.6f", occupied, subarrays[i].length, vacancy);
        if (occupied == 0) {
            snprintf(theoretical_reference, sizeof(theoretical_reference), "not reached, completed target %.6f", completed_target_vacancy);
        } else if (occupied == completed_target_occupied) {
            snprintf(theoretical_reference, sizeof(theoretical_reference), "completed target met at %.6f", completed_target_vacancy);
        } else {
            snprintf(theoretical_reference, sizeof(theoretical_reference), "frontier, completed target %.6f", completed_target_vacancy);
        }
        print_scorecard_row(name, observed, theoretical_reference);
    }
    free(subarrays);
}

/// Prints the final occupancy of every Funnel subarray
static void print_funnel_occupancy(const FunnelHashMap* hashmap, const FunnelPartition* partition) {
    if (hashmap == NULL || partition == NULL) {
        return;
    }

    print_scorecard_header("funnel subarrays");
    for (int i = 0; i < partition->alpha; i++) {
        const int length = partition->subarrays[i].n_buckets * partition->beta;
        int occupied = 0;
        for (int slot = 0; slot < length; slot++) {
            if (hashmap->table[partition->subarrays[i].starting_index + slot] != NULL) {
                occupied++;
            }
        }
        char name[16];
        char observed[64];
        snprintf(name, sizeof(name), "A%d", i + 1);
        snprintf(observed, sizeof(observed), "%d / %d", occupied, length);
        print_scorecard_row(name, observed, "-");
    }

    int b_occupied = 0;
    for (int slot = 0; slot < partition->b.length; slot++) {
        if (hashmap->table[partition->b.starting_index + slot] != NULL) {
            b_occupied++;
        }
    }
    int c_occupied = 0;
    for (int slot = 0; slot < partition->c.length; slot++) {
        if (hashmap->table[partition->c.starting_index + slot] != NULL) {
            c_occupied++;
        }
    }
    char observed[64];
    snprintf(observed, sizeof(observed), "%d / %d", b_occupied, partition->b.length);
    print_scorecard_row("B", observed, "load at most 1/2 with high probability");
    snprintf(observed, sizeof(observed), "%d / %d", c_occupied, partition->c.length);
    print_scorecard_row("C", observed, "no bucket overflow with high probability");
}
#endif

/// Prints the columns shared by every compact load-factor result
static void print_load_sweep_header(void) {
    printf("%-9s | %-7s | %-11s | %-11s | %-10s | %-11s | %-11s | %s\n", "map", "c", "insert time", "insert avg", "insert max", "lookup time", "lookup avg", "lookup max");
    printf("----------+---------+-------------+-------------+------------+-------------+-------------+-----------\n");
}

/// Prints one compact row with probe columns enabled only in the instrumented build
static void print_load_sweep_measurement(const BenchmarkMeasurement* measurement) {
    char c_value[16];
    if (isfinite(measurement->c)) {
        snprintf(c_value, sizeof(c_value), "%.3f", measurement->c);
    } else {
        snprintf(c_value, sizeof(c_value), "-");
    }
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    printf("%-9s | %7s | %9.6f s | %11.3f | %10llu | %9.6f s | %11.3f | %10llu\n", measurement->implementation, c_value, measurement->insertion_seconds, probes_per_operation(measurement->insertion_probes, measurement->insertion_ops), (unsigned long long)measurement->maximum_insertion_probes, measurement->lookup_seconds, probes_per_operation(measurement->lookup_probes, measurement->lookup_ops), (unsigned long long)measurement->maximum_lookup_probes);
#else
    printf("%-9s | %7s | %9.6f s | %11s | %10s | %9.6f s | %11s | %10s\n", measurement->implementation, c_value, measurement->insertion_seconds, "-", "-", measurement->lookup_seconds, "-", "-");
#endif
}

/// Prints the columns used to expose the finite cost of changing Elastic c
static void print_c_sweep_header(void) {
    printf("%-7s | %-11s | %-11s | %-10s | %-11s | %-11s | %-10s | %-13s | %-11s | %s\n", "c", "insert time", "insert avg", "insert max", "lookup time", "lookup avg", "lookup max", "case1 fallback", "case3 count", "case3 avg/max");
    printf("--------+-------------+-------------+------------+-------------+-------------+------------+---------------+-------------+----------------\n");
}

/// Prints one Elastic constant experiment including the expensive Case 3 cost
static void print_c_sweep_measurement(const BenchmarkMeasurement* measurement) {
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    char fallback[32];
    char case_three_cost[48];
    snprintf(fallback, sizeof(fallback), "%llu / %llu", (unsigned long long)measurement->case_one_fallbacks, (unsigned long long)measurement->case_one_insertions);
    snprintf(case_three_cost, sizeof(case_three_cost), "%.3f / %llu", probes_per_operation(measurement->case_three_probes, measurement->case_three_insertions), (unsigned long long)measurement->maximum_case_three_probes);
    printf("%7.3f | %9.6f s | %11.3f | %10llu | %9.6f s | %11.3f | %10llu | %-13s | %11llu | %s\n", measurement->c, measurement->insertion_seconds, probes_per_operation(measurement->insertion_probes, measurement->insertion_ops), (unsigned long long)measurement->maximum_insertion_probes, measurement->lookup_seconds, probes_per_operation(measurement->lookup_probes, measurement->lookup_ops), (unsigned long long)measurement->maximum_lookup_probes, fallback, (unsigned long long)measurement->case_three_insertions, case_three_cost);
#else
    printf("%7.3f | %9.6f s | %11s | %10s | %9.6f s | %11s | %10s | %-13s | %11s | %s\n", measurement->c, measurement->insertion_seconds, "-", "-", measurement->lookup_seconds, "-", "-", "-", "-", "-");
#endif
}

/// Prints the stable column order used by both CSV benchmark campaigns
static void print_csv_header(void) {
    printf("implementation,dataset,capacity,keys,delta,load_factor,c,seed,probe_counting,correct,insertion_seconds,insertion_operations,insertion_probes,insertion_probe_average,insertion_probe_maximum,positive_lookup_seconds,positive_lookup_operations,positive_lookup_probes,positive_lookup_probe_average,positive_lookup_probe_maximum,negative_lookup_seconds,negative_lookup_probes,elastic_batch_zero_insertions,elastic_batch_zero_probes,elastic_case_one_insertions,elastic_case_one_fallbacks,elastic_case_one_probes,elastic_case_two_insertions,elastic_case_two_probes,elastic_case_three_insertions,elastic_case_three_probes,elastic_case_three_probe_average,elastic_case_three_probe_maximum,funnel_insertions_in_a,funnel_insertions_in_b,funnel_insertions_in_c,funnel_insertion_failures,theory_log2_inverse_delta,theory_log2_inverse_delta_squared,theory_log2_log2_capacity,theory_funnel_maximum_scale,theory_funnel_special_insertion_bound\n");
}

/// Prints one quoted CSV string and doubles embedded quote characters
static void print_csv_string_field(const char* value) {
    putchar(',');
    putchar('"');
    if (value != NULL) {
        for (const char* character = value; *character != '\0'; character++) {
            if (*character == '"') {
                putchar('"');
            }
            putchar(*character);
        }
    }
    putchar('"');
}

/// Prints one comma followed by an integer field or an empty field
static void print_csv_uint64_field(const bool present, const uint64_t value) {
    putchar(',');
    if (present) {
        printf("%llu", (unsigned long long)value);
    }
}

/// Prints one comma followed by a floating point field or an empty field
static void print_csv_double_field(const bool present, const double value) {
    putchar(',');
    if (present) {
        printf("%.9f", value);
    }
}

/// Prints one benchmark row without headings so stdout can be redirected to a CSV file
static void print_csv_measurement(const BenchmarkMeasurement* measurement) {
    const bool is_funnel = strcmp(measurement->implementation, "funnel") == 0;
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const bool has_probe_counts = true;
    const bool has_elastic_probe_counts = strcmp(measurement->implementation, "elastic") == 0;
#else
    const bool has_probe_counts = false;
    const bool has_elastic_probe_counts = false;
#endif
    const double log_delta = paper_log_delta_scale(measurement->delta);
    const double log_delta_squared = log_delta * log_delta;
    const double log_log_capacity = paper_log_log_scale(measurement->capacity);

    printf("%s", measurement->implementation);
    print_csv_string_field(measurement->dataset);
    printf(",%d,%zu,%.9f,%.9f", measurement->capacity, measurement->key_count, measurement->delta, (double)measurement->key_count / (double)measurement->capacity);
    print_csv_double_field(isfinite(measurement->c), measurement->c);
    putchar(',');
    if (measurement->seed != NULL) {
        for (size_t i = 0; i < (size_t)SIPHASH_2_4_KEY_SIZE; i++) {
            printf("%02x", (unsigned int)measurement->seed[i]);
        }
    }
    printf(",%d,%d,%.9f,%llu", has_probe_counts ? 1 : 0, measurement->correct ? 1 : 0, measurement->insertion_seconds, (unsigned long long)measurement->insertion_ops);
    print_csv_uint64_field(has_probe_counts, measurement->insertion_probes);
    print_csv_double_field(has_probe_counts, !has_probe_counts || measurement->insertion_ops == 0 ? 0.0 : (double)measurement->insertion_probes / (double)measurement->insertion_ops);
    print_csv_uint64_field(has_probe_counts, measurement->maximum_insertion_probes);
    printf(",%.9f,%llu", measurement->lookup_seconds, (unsigned long long)measurement->lookup_ops);
    print_csv_uint64_field(has_probe_counts, measurement->lookup_probes);
    print_csv_double_field(has_probe_counts, !has_probe_counts || measurement->lookup_ops == 0 ? 0.0 : (double)measurement->lookup_probes / (double)measurement->lookup_ops);
    print_csv_uint64_field(has_probe_counts, measurement->maximum_lookup_probes);
    print_csv_double_field(true, measurement->negative_lookup_seconds);
    print_csv_uint64_field(has_probe_counts, measurement->negative_lookup_probes);
    print_csv_uint64_field(has_elastic_probe_counts, measurement->batch_zero_insertions);
    print_csv_uint64_field(has_elastic_probe_counts, measurement->batch_zero_probes);
    print_csv_uint64_field(has_elastic_probe_counts, measurement->case_one_insertions);
    print_csv_uint64_field(has_elastic_probe_counts, measurement->case_one_fallbacks);
    print_csv_uint64_field(has_elastic_probe_counts, measurement->case_one_probes);
    print_csv_uint64_field(has_elastic_probe_counts, measurement->case_two_insertions);
    print_csv_uint64_field(has_elastic_probe_counts, measurement->case_two_probes);
    print_csv_uint64_field(has_elastic_probe_counts, measurement->case_three_insertions);
    print_csv_uint64_field(has_elastic_probe_counts, measurement->case_three_probes);
    print_csv_double_field(has_elastic_probe_counts && measurement->case_three_insertions > 0, measurement->case_three_insertions == 0 ? 0.0 : (double)measurement->case_three_probes / (double)measurement->case_three_insertions);
    print_csv_uint64_field(has_elastic_probe_counts, measurement->maximum_case_three_probes);
    print_csv_uint64_field(has_probe_counts && is_funnel, measurement->funnel_insertions_in_a);
    print_csv_uint64_field(has_probe_counts && is_funnel, measurement->funnel_insertions_in_b);
    print_csv_uint64_field(has_probe_counts && is_funnel, measurement->funnel_insertions_in_c);
    print_csv_uint64_field(has_probe_counts && is_funnel, measurement->funnel_insertion_failures);
    print_csv_double_field(true, log_delta);
    print_csv_double_field(true, log_delta_squared);
    print_csv_double_field(true, log_log_capacity);
    print_csv_double_field(is_funnel, log_delta_squared + log_log_capacity);
    print_csv_double_field(is_funnel, measurement->delta * (double)measurement->capacity / 8.0);
    putchar('\n');
}

/// Runs insertion and positive and negative lookup checks on Elastic Hashing
static bool run_elastic(const WordList* wordlist, const char* dataset, const int capacity, const float delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE], const char* missing_word, const BenchmarkOutputMode output_mode, const double c) {
    ElasticHashmap* hashmap = calloc(1, sizeof(ElasticHashmap));
    Element** elements = create_elements(wordlist);
    if (hashmap == NULL || elements == NULL) {
        free(hashmap);
        delete_unconsumed_elements(elements, wordlist->size);
        return false;
    }

    *hashmap = (ElasticHashmap){.capacity = capacity, .size = 0, .table = calloc((size_t)capacity, sizeof(Element*))};
    if (hashmap->table == NULL) {
        free(hashmap);
        delete_unconsumed_elements(elements, wordlist->size);
        return false;
    }

    bool success = true;
    const clock_t insertion_start = clock();
    batch_insert_with_c(hashmap, delta, c, elements, seed);
    success = hashmap->size == (int)wordlist->size;
    for (size_t i = 0; i < wordlist->size; i++) {
        success = success && elements[i] == NULL;
    }
    const clock_t insertion_end = clock();
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const ElasticHashmapProbeStats insertion_stats = elastic_hashmap_probe_stats(hashmap);
    elastic_hashmap_reset_probe_stats(hashmap);
#endif

    size_t positive_lookups = 0;
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    uint64_t phi_reference_count = 0;
    uint64_t lookups_at_phi = 0;
    uint64_t lookups_before_phi = 0;
    uint64_t lookups_after_phi = 0;
    uint64_t maximum_phi = 0;
    long double phi_sum = 0.0L;
    const bool collect_phi_diagnostics = success && output_mode == BenchmarkDetailed;
    uint64_t* positive_probe_counts = collect_phi_diagnostics ? calloc(wordlist->size, sizeof(uint64_t)) : NULL;
    int* physical_positions = collect_phi_diagnostics ? elastic_positions_by_value(hashmap, wordlist->size) : NULL;
    ElasticSubArray* lookup_subarrays = collect_phi_diagnostics ? partition_elastic_hashmap(hashmap->capacity) : NULL;
    const int lookup_subarray_count = n_elastic_subarrays(hashmap->capacity);
#endif
    const clock_t positive_lookup_start = clock();
    if (success) {
        for (size_t i = 0; i < wordlist->size; i++) {
            const Option_Element_p result = retrieve_element_elastic_hashmap(hashmap, wordlist->words[i], seed);
            if (is_none(result.option) || result.element_p->value != (int)i) {
                success = false;
                break;
            }
            positive_lookups++;
        }
    }
    const clock_t positive_lookup_end = clock();
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const ElasticHashmapProbeStats positive_lookup_stats = elastic_hashmap_probe_stats(hashmap);
    // Per-key placement reconstruction uses a second pass so diagnostic work does not inflate lookup timing
    if (positive_probe_counts != NULL) {
        elastic_hashmap_reset_probe_stats(hashmap);
        uint64_t previous_positive_probes = 0;
        for (size_t i = 0; i < positive_lookups; i++) {
            const Option_Element_p result = retrieve_element_elastic_hashmap(hashmap, wordlist->words[i], seed);
            if (is_none(result.option) || result.element_p->value != (int)i) {
                success = false;
                break;
            }
            const uint64_t total_positive_probes = elastic_hashmap_probe_stats(hashmap).lookup_probes;
            positive_probe_counts[i] = total_positive_probes - previous_positive_probes;
            previous_positive_probes = total_positive_probes;
        }
    }
    if (physical_positions != NULL && lookup_subarrays != NULL && positive_probe_counts != NULL) {
        for (size_t i = 0; i < positive_lookups; i++) {
            if (physical_positions[i] < 0) {
                continue;
            }
            const uint64_t phi_value = elastic_placement_phi(wordlist->words[i], physical_positions[i], lookup_subarrays, lookup_subarray_count, seed);
            if (phi_value == 0) {
                continue;
            }
            phi_reference_count++;
            phi_sum += (long double)phi_value;
            maximum_phi = phi_value > maximum_phi ? phi_value : maximum_phi;
            if (positive_probe_counts[i] < phi_value) {
                lookups_before_phi++;
            } else if (positive_probe_counts[i] == phi_value) {
                lookups_at_phi++;
            } else {
                lookups_after_phi++;
            }
        }
    }
    free(positive_probe_counts);
    free(physical_positions);
    free(lookup_subarrays);
    elastic_hashmap_reset_probe_stats(hashmap);
#endif

    const clock_t negative_lookup_start = clock();
    const Option_Element_p missing_result = success ? retrieve_element_elastic_hashmap(hashmap, missing_word, seed) : (Option_Element_p){.option = Some, .element_p = NULL};
    const clock_t negative_lookup_end = clock();
    success = success && is_none(missing_result.option);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const ElasticHashmapProbeStats negative_lookup_stats = elastic_hashmap_probe_stats(hashmap);
#endif

    BenchmarkMeasurement measurement = {
        .implementation = "elastic",
        .dataset = dataset,
        .seed = seed,
        .capacity = capacity,
        .key_count = wordlist->size,
        .delta = delta,
        .c = c,
        .correct = success,
        .insertion_seconds = elapsed_seconds(insertion_start, insertion_end),
        .lookup_seconds = elapsed_seconds(positive_lookup_start, positive_lookup_end),
        .negative_lookup_seconds = elapsed_seconds(negative_lookup_start, negative_lookup_end),
        .insertion_ops = (uint64_t)hashmap->size,
        .lookup_ops = (uint64_t)positive_lookups,
    };
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    measurement.insertion_ops = insertion_stats.insertion_ops;
    measurement.insertion_probes = insertion_stats.insertion_probes;
    measurement.maximum_insertion_probes = insertion_stats.maximum_insertion_probes;
    measurement.lookup_ops = positive_lookup_stats.lookup_ops;
    measurement.lookup_probes = positive_lookup_stats.lookup_probes;
    measurement.maximum_lookup_probes = positive_lookup_stats.maximum_lookup_probes;
    measurement.negative_lookup_probes = negative_lookup_stats.lookup_probes;
    measurement.batch_zero_insertions = insertion_stats.batch_zero_insertions;
    measurement.batch_zero_probes = insertion_stats.batch_zero_probes;
    measurement.case_one_insertions = insertion_stats.case_one_insertions;
    measurement.case_one_fallbacks = insertion_stats.case_one_fallbacks;
    measurement.case_one_probes = insertion_stats.case_one_probes;
    measurement.case_two_insertions = insertion_stats.case_two_insertions;
    measurement.case_two_probes = insertion_stats.case_two_probes;
    measurement.case_three_insertions = insertion_stats.case_three_insertions;
    measurement.case_three_probes = insertion_stats.case_three_probes;
    measurement.maximum_case_three_probes = insertion_stats.maximum_case_three_probes;
#endif
    if (output_mode != BenchmarkDetailed) {
        if (output_mode == BenchmarkCsv) {
            print_csv_measurement(&measurement);
        } else if (output_mode == BenchmarkCSweep) {
            print_c_sweep_measurement(&measurement);
        } else {
            print_load_sweep_measurement(&measurement);
        }
        delete_unconsumed_elements(elements, wordlist->size);
        delete_elastic_hashmap(hashmap);
        return success;
    }

    char observed[96];
    char theoretical_reference[160];
    print_scorecard_header("elastic");
    snprintf(observed, sizeof(observed), "%d / %zu", hashmap->size, wordlist->size);
    snprintf(theoretical_reference, sizeof(theoretical_reference), "%zu required", wordlist->size);
    print_scorecard_row("insertions", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%d fixed slots", hashmap->capacity);
    snprintf(theoretical_reference, sizeof(theoretical_reference), "%d shared slots", capacity);
    print_scorecard_row("capacity", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%.6f", (double)hashmap->size / (double)hashmap->capacity);
    snprintf(theoretical_reference, sizeof(theoretical_reference), "%.6f target", (double)wordlist->size / (double)capacity);
    print_scorecard_row("load factor", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%.3f", c);
    print_scorecard_row("c in f(epsilon)", observed, "paper requires a sufficiently large constant but does not fix it");
    snprintf(observed, sizeof(observed), "%zu / %zu", positive_lookups, wordlist->size);
    snprintf(theoretical_reference, sizeof(theoretical_reference), "%zu required", wordlist->size);
    print_scorecard_row("positive lookups", observed, theoretical_reference);
    print_scorecard_row("negative lookup", is_none(missing_result.option) ? "pass" : "fail", "no complexity bound");
    snprintf(observed, sizeof(observed), "%.6f s", elapsed_seconds(insertion_start, insertion_end));
    print_scorecard_row("insertion CPU time", observed, "-");
    snprintf(observed, sizeof(observed), "%.6f s", elapsed_seconds(positive_lookup_start, positive_lookup_end));
    print_scorecard_row("positive lookup CPU time", observed, "-");
    snprintf(observed, sizeof(observed), "%.6f s", elapsed_seconds(negative_lookup_start, negative_lookup_end));
    print_scorecard_row("negative lookup CPU time", observed, "-");
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const double log_delta_scale = paper_log_delta_scale((double)delta);
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)insertion_stats.insertion_probes);
    print_scorecard_row("insertion probes total", observed, "-");
    snprintf(observed, sizeof(observed), "%.6f", probes_per_operation(insertion_stats.insertion_probes, insertion_stats.insertion_ops));
    print_scorecard_row("insertion probes average", observed, "-");
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)insertion_stats.maximum_insertion_probes);
    snprintf(theoretical_reference, sizeof(theoretical_reference), "max expected over seeds O(L), not a one-run threshold; L=%.3f", log_delta_scale);
    print_scorecard_row("insertion probes maximum", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)positive_lookup_stats.lookup_probes);
    print_scorecard_row("positive lookup probes total", observed, "-");
    snprintf(observed, sizeof(observed), "%.6f", probes_per_operation(positive_lookup_stats.lookup_probes, positive_lookup_stats.lookup_ops));
    print_scorecard_row("positive lookup probes average", observed, "amortized expected O(1)");
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)positive_lookup_stats.maximum_lookup_probes);
    snprintf(theoretical_reference, sizeof(theoretical_reference), "max expected over seeds O(L), not a one-run threshold; L=%.3f", log_delta_scale);
    print_scorecard_row("positive lookup probes maximum", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)negative_lookup_stats.lookup_probes);
    print_scorecard_row("negative lookup probes", observed, "no bound");
    snprintf(observed, sizeof(observed), "%llu (%.3f%%)", (unsigned long long)insertion_stats.batch_zero_insertions, operation_percentage(insertion_stats.batch_zero_insertions, insertion_stats.insertion_ops));
    print_scorecard_row("batch B0 insertions", observed, "ceil(0.75 * |A1|)");
    snprintf(observed, sizeof(observed), "%llu total, %.6f average", (unsigned long long)insertion_stats.batch_zero_probes, probes_per_operation(insertion_stats.batch_zero_probes, insertion_stats.batch_zero_insertions));
    print_scorecard_row("batch B0 insertion probes", observed, "-");
    snprintf(observed, sizeof(observed), "%llu (%.3f%%)", (unsigned long long)insertion_stats.case_one_insertions, operation_percentage(insertion_stats.case_one_insertions, insertion_stats.insertion_ops));
    print_scorecard_row("case 1 insertions", observed, "state dependent");
    snprintf(observed, sizeof(observed), "%llu total, %.6f average", (unsigned long long)insertion_stats.case_one_probes, probes_per_operation(insertion_stats.case_one_probes, insertion_stats.case_one_insertions));
    print_scorecard_row("case 1 insertion probes", observed, "includes Ai attempts and fallback when used");
    snprintf(observed, sizeof(observed), "%llu (%.3f%% of case 1)", (unsigned long long)insertion_stats.case_one_fallbacks, operation_percentage(insertion_stats.case_one_fallbacks, insertion_stats.case_one_insertions));
    print_scorecard_row("case 1 fallbacks", observed, "state dependent");
    snprintf(observed, sizeof(observed), "%llu (%.3f%%)", (unsigned long long)insertion_stats.case_two_insertions, operation_percentage(insertion_stats.case_two_insertions, insertion_stats.insertion_ops));
    print_scorecard_row("case 2 insertions", observed, "state dependent");
    snprintf(observed, sizeof(observed), "%llu total, %.6f average", (unsigned long long)insertion_stats.case_two_probes, probes_per_operation(insertion_stats.case_two_probes, insertion_stats.case_two_insertions));
    print_scorecard_row("case 2 insertion probes", observed, "-");
    snprintf(observed, sizeof(observed), "%llu (%.3f%%)", (unsigned long long)insertion_stats.case_three_insertions, operation_percentage(insertion_stats.case_three_insertions, insertion_stats.insertion_ops));
    print_scorecard_row("case 3 insertions", observed, "0% with probability 1-O(1/|Ai|^2) per batch");
    snprintf(observed, sizeof(observed), "%llu total, %.6f average, max %llu", (unsigned long long)insertion_stats.case_three_probes, probes_per_operation(insertion_stats.case_three_probes, insertion_stats.case_three_insertions), (unsigned long long)insertion_stats.maximum_case_three_probes);
    print_scorecard_row("case 3 insertion probes", observed, "expensive case");
    if (phi_reference_count > 0) {
        snprintf(observed, sizeof(observed), "average %.3Lf, maximum %llu", phi_sum / (long double)phi_reference_count, (unsigned long long)maximum_phi);
        print_scorecard_row("placement phi", observed, "phi(i,j) = O(i * j^2)");
        snprintf(observed, sizeof(observed), "%llu before, %llu exact, %llu after", (unsigned long long)lookups_before_phi, (unsigned long long)lookups_at_phi, (unsigned long long)lookups_after_phi);
        print_scorecard_row("lookup versus placement phi", observed, "after = 0 required");
    }
    print_elastic_occupancy(hashmap, (double)delta);
#endif

    delete_unconsumed_elements(elements, wordlist->size);
    delete_elastic_hashmap(hashmap);
    return success;
}

/// Runs insertion and positive and negative lookup checks on Funnel Hashing
static bool run_funnel(const WordList* wordlist, const char* dataset, const int capacity, const double delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE], const char* missing_word, const BenchmarkOutputMode output_mode) {
    FunnelHashMap* hashmap = create_funnel_hashmap(capacity);
    Element** elements = create_elements(wordlist);
    if (hashmap == NULL || elements == NULL) {
        delete_funnel_hashmap(hashmap);
        delete_unconsumed_elements(elements, wordlist->size);
        return false;
    }

    const clock_t insertion_start = clock();
    bool success = batch_insert_funnel_hashmap(hashmap, delta, elements, seed);
    success = success && hashmap->size == (int)wordlist->size;
    for (size_t i = 0; i < wordlist->size; i++) {
        success = success && elements[i] == NULL;
    }
    const clock_t insertion_end = clock();
    FunnelPartition* partition = success ? partition_funnel_hashmap(capacity, delta) : NULL;
    success = success && partition != NULL;
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const FunnelHashmapProbeStats insertion_stats = funnel_hashmap_probe_stats(hashmap);
    funnel_hashmap_reset_probe_stats(hashmap);
#endif

    size_t positive_lookups = 0;
    const clock_t positive_lookup_start = clock();
    if (success) {
        for (size_t i = 0; i < wordlist->size; i++) {
            const Option_Element_p result = retrieve_element_funnel_hashmap_with_partition(hashmap, partition, wordlist->words[i], seed);
            if (is_none(result.option) || result.element_p->value != (int)i) {
                success = false;
                break;
            }
            positive_lookups++;
        }
    }
    const clock_t positive_lookup_end = clock();
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const FunnelHashmapProbeStats positive_lookup_stats = funnel_hashmap_probe_stats(hashmap);
    funnel_hashmap_reset_probe_stats(hashmap);
#endif

    const clock_t negative_lookup_start = clock();
    const Option_Element_p missing_result = success ? retrieve_element_funnel_hashmap_with_partition(hashmap, partition, missing_word, seed) : (Option_Element_p){.option = Some, .element_p = NULL};
    const clock_t negative_lookup_end = clock();
    success = success && is_none(missing_result.option);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const FunnelHashmapProbeStats negative_lookup_stats = funnel_hashmap_probe_stats(hashmap);
#endif

    BenchmarkMeasurement measurement = {
        .implementation = "funnel",
        .dataset = dataset,
        .seed = seed,
        .capacity = capacity,
        .key_count = wordlist->size,
        .delta = delta,
        .c = NAN,
        .correct = success,
        .insertion_seconds = elapsed_seconds(insertion_start, insertion_end),
        .lookup_seconds = elapsed_seconds(positive_lookup_start, positive_lookup_end),
        .negative_lookup_seconds = elapsed_seconds(negative_lookup_start, negative_lookup_end),
        .insertion_ops = (uint64_t)hashmap->size,
        .lookup_ops = (uint64_t)positive_lookups,
    };
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    measurement.insertion_ops = insertion_stats.insertion_ops;
    measurement.insertion_probes = insertion_stats.insertion_probes;
    measurement.maximum_insertion_probes = insertion_stats.maximum_insertion_probes;
    measurement.lookup_ops = positive_lookup_stats.lookup_ops;
    measurement.lookup_probes = positive_lookup_stats.lookup_probes;
    measurement.maximum_lookup_probes = positive_lookup_stats.maximum_lookup_probes;
    measurement.negative_lookup_probes = negative_lookup_stats.lookup_probes;
    measurement.funnel_insertions_in_a = insertion_stats.insertions_in_a;
    measurement.funnel_insertions_in_b = insertion_stats.insertions_in_b;
    measurement.funnel_insertions_in_c = insertion_stats.insertions_in_c;
    measurement.funnel_insertion_failures = insertion_stats.insertion_failures;
#endif
    if (output_mode != BenchmarkDetailed) {
        if (output_mode == BenchmarkCsv) {
            print_csv_measurement(&measurement);
        } else {
            print_load_sweep_measurement(&measurement);
        }
        delete_unconsumed_elements(elements, wordlist->size);
        delete_funnel_partition(partition);
        delete_funnel_hashmap(hashmap);
        return success;
    }

    char observed[96];
    char theoretical_reference[160];
    print_scorecard_header("funnel");
    snprintf(observed, sizeof(observed), "%d / %zu", hashmap->size, wordlist->size);
    snprintf(theoretical_reference, sizeof(theoretical_reference), "%zu required", wordlist->size);
    print_scorecard_row("insertions", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%d fixed slots", hashmap->capacity);
    snprintf(theoretical_reference, sizeof(theoretical_reference), "%d shared slots", capacity);
    print_scorecard_row("capacity", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%.6f", (double)hashmap->size / (double)hashmap->capacity);
    snprintf(theoretical_reference, sizeof(theoretical_reference), "%.6f target", (double)wordlist->size / (double)capacity);
    print_scorecard_row("load factor", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%zu / %zu", positive_lookups, wordlist->size);
    snprintf(theoretical_reference, sizeof(theoretical_reference), "%zu required", wordlist->size);
    print_scorecard_row("positive lookups", observed, theoretical_reference);
    print_scorecard_row("negative lookup", is_none(missing_result.option) ? "pass" : "fail", "same bound as insertion");
    snprintf(observed, sizeof(observed), "%.6f s", elapsed_seconds(insertion_start, insertion_end));
    print_scorecard_row("insertion CPU time", observed, "-");
    snprintf(observed, sizeof(observed), "%.6f s", elapsed_seconds(positive_lookup_start, positive_lookup_end));
    print_scorecard_row("positive lookup CPU time", observed, "-");
    snprintf(observed, sizeof(observed), "%.6f s", elapsed_seconds(negative_lookup_start, negative_lookup_end));
    print_scorecard_row("negative lookup CPU time", observed, "-");
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const double log_delta_scale = paper_log_delta_scale(delta);
    const double worst_expected_scale = log_delta_scale * log_delta_scale;
    const double high_probability_scale = worst_expected_scale + paper_log_log_scale(capacity);
    if (partition != NULL) {
        const int minimum_last_length = (int)ceil(delta * (double)capacity / 2.0);
        const int maximum_last_length = (int)floor(3.0 * delta * (double)capacity / 4.0);
        snprintf(observed, sizeof(observed), "%d", partition->alpha);
        snprintf(theoretical_reference, sizeof(theoretical_reference), "ceil(4L) + 10 = %d", funnel_alpha(delta));
        print_scorecard_row("alpha", observed, theoretical_reference);
        snprintf(observed, sizeof(observed), "%d", partition->beta);
        snprintf(theoretical_reference, sizeof(theoretical_reference), "ceil(2L) = %d", funnel_beta(delta));
        print_scorecard_row("beta", observed, theoretical_reference);
        snprintf(observed, sizeof(observed), "%d", partition->a_alpha_plus_one_length);
        snprintf(theoretical_reference, sizeof(theoretical_reference), "required range [%d, %d]", minimum_last_length, maximum_last_length);
        print_scorecard_row("A_(alpha+1) slots", observed, theoretical_reference);
        snprintf(observed, sizeof(observed), "%d", partition->b_probe_limit);
        snprintf(theoretical_reference, sizeof(theoretical_reference), "ceil(log2(log2(n))) = %d", partition->b_probe_limit);
        print_scorecard_row("B probe limit", observed, theoretical_reference);
        snprintf(observed, sizeof(observed), "%d in %d buckets", partition->c_bucket_length, partition->c_bucket_count);
        snprintf(theoretical_reference, sizeof(theoretical_reference), "ceil(2log2(log2(n))) = %d", partition->c_bucket_length);
        print_scorecard_row("C bucket length", observed, theoretical_reference);
    }
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)insertion_stats.insertion_probes);
    print_scorecard_row("insertion probes total", observed, "-");
    snprintf(observed, sizeof(observed), "%.6f", probes_per_operation(insertion_stats.insertion_probes, insertion_stats.insertion_ops));
    snprintf(theoretical_reference, sizeof(theoretical_reference), "amortized expected O(L), L=%.3f", log_delta_scale);
    print_scorecard_row("insertion probes average", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)insertion_stats.maximum_insertion_probes);
    snprintf(theoretical_reference, sizeof(theoretical_reference), "run maximum w.h.p. O(L^2 + loglog(n)), scale %.3f", high_probability_scale);
    print_scorecard_row("insertion probes maximum", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)positive_lookup_stats.lookup_probes);
    print_scorecard_row("positive lookup probes total", observed, "-");
    snprintf(observed, sizeof(observed), "%.6f", probes_per_operation(positive_lookup_stats.lookup_probes, positive_lookup_stats.lookup_ops));
    snprintf(theoretical_reference, sizeof(theoretical_reference), "amortized expected O(L), L=%.3f", log_delta_scale);
    print_scorecard_row("positive lookup probes average", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)positive_lookup_stats.maximum_lookup_probes);
    snprintf(theoretical_reference, sizeof(theoretical_reference), "run maximum w.h.p. O(L^2 + loglog(n)), scale %.3f", high_probability_scale);
    print_scorecard_row("positive lookup probes maximum", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)negative_lookup_stats.lookup_probes);
    snprintf(theoretical_reference, sizeof(theoretical_reference), "expected O(L^2), scale %.3f", worst_expected_scale);
    print_scorecard_row("negative lookup probes", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%llu (%.3f%%)", (unsigned long long)insertion_stats.insertions_in_a, operation_percentage(insertion_stats.insertions_in_a, insertion_stats.insertion_ops));
    print_scorecard_row("insertions in A'", observed, "-");
    const uint64_t special_insertions = insertion_stats.insertions_in_b + insertion_stats.insertions_in_c + insertion_stats.insertion_failures;
    snprintf(observed, sizeof(observed), "%llu (%.3f%%)", (unsigned long long)special_insertions, operation_percentage(special_insertions, insertion_stats.insertion_ops));
    const double special_insertion_bound = delta * (double)capacity / 8.0;
    const double special_insertion_percentage_bound = insertion_stats.insertion_ops == 0 ? 0.0 : 100.0 * special_insertion_bound / (double)insertion_stats.insertion_ops;
    snprintf(theoretical_reference, sizeof(theoretical_reference), "< %.3f (< %.3f%% of insertions) w.h.p.", special_insertion_bound, special_insertion_percentage_bound);
    print_scorecard_row("insertions reaching A_(alpha+1)", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%llu (%.3f%% of special)", (unsigned long long)insertion_stats.insertions_in_c, operation_percentage(insertion_stats.insertions_in_c, special_insertions));
    snprintf(theoretical_reference, sizeof(theoretical_reference), "B failure <= 1/log2(n) = %.3f%%", 100.0 / log2((double)capacity));
    print_scorecard_row("insertions in C", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)insertion_stats.insertion_failures);
    print_scorecard_row("table failures", observed, "0 with high probability");
    print_funnel_occupancy(hashmap, partition);
#endif

    delete_unconsumed_elements(elements, wordlist->size);
    delete_funnel_partition(partition);
    delete_funnel_hashmap(hashmap);
    return success;
}

/// Runs insertion and positive and negative lookup checks on the control hashmap
static bool run_standard(const WordList* wordlist, const char* dataset, const int capacity, const float delta, const char* missing_word, const uint8_t seed[SIPHASH_2_4_KEY_SIZE], const BenchmarkOutputMode output_mode) {
    ApiHashmap* hashmap = seed == NULL ? create_fixed_api_hashmap_with_size((size_t)capacity) : create_fixed_api_hashmap_with_size_and_seed((size_t)capacity, seed);
    if (hashmap == NULL) {
        return false;
    }

    bool success = true;
    size_t inserted = 0;
    const clock_t insertion_start = clock();
    for (size_t i = 0; i < wordlist->size; i++) {
        if (insert_element_api_hashmap(hashmap, wordlist->words[i], (int)i) == NULL) {
            success = false;
            break;
        }
        inserted++;
    }
    const clock_t insertion_end = clock();
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const ApiHashmapProbeStats insertion_stats = api_hashmap_probe_stats(hashmap);
    api_hashmap_reset_probe_stats(hashmap);
#endif

    size_t positive_lookups = 0;
    const clock_t positive_lookup_start = clock();
    if (success) {
        for (size_t i = 0; i < wordlist->size; i++) {
            const Option_Element_p result = retrieve_element_api_hashmap(hashmap, wordlist->words[i]);
            if (is_none(result.option) || result.element_p->value != (int)i) {
                success = false;
                break;
            }
            positive_lookups++;
        }
    }
    const clock_t positive_lookup_end = clock();
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const ApiHashmapProbeStats positive_lookup_stats = api_hashmap_probe_stats(hashmap);
    api_hashmap_reset_probe_stats(hashmap);
#endif

    const clock_t negative_lookup_start = clock();
    const Option_Element_p missing_result = success ? retrieve_element_api_hashmap(hashmap, missing_word) : (Option_Element_p){.option = Some, .element_p = NULL};
    const clock_t negative_lookup_end = clock();
    success = success && is_none(missing_result.option);
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    const ApiHashmapProbeStats negative_lookup_stats = api_hashmap_probe_stats(hashmap);
#endif

    BenchmarkMeasurement measurement = {
        .implementation = "standard",
        .dataset = dataset,
        .seed = seed,
        .capacity = capacity,
        .key_count = wordlist->size,
        .delta = delta,
        .c = NAN,
        .correct = success,
        .insertion_seconds = elapsed_seconds(insertion_start, insertion_end),
        .lookup_seconds = elapsed_seconds(positive_lookup_start, positive_lookup_end),
        .negative_lookup_seconds = elapsed_seconds(negative_lookup_start, negative_lookup_end),
        .insertion_ops = (uint64_t)inserted,
        .lookup_ops = (uint64_t)positive_lookups,
    };
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    measurement.insertion_ops = insertion_stats.insertion_ops;
    measurement.insertion_probes = insertion_stats.insertion_probes;
    measurement.maximum_insertion_probes = insertion_stats.maximum_insertion_probes;
    measurement.lookup_ops = positive_lookup_stats.lookup_ops;
    measurement.lookup_probes = positive_lookup_stats.lookup_probes;
    measurement.maximum_lookup_probes = positive_lookup_stats.maximum_lookup_probes;
    measurement.negative_lookup_probes = negative_lookup_stats.lookup_probes;
#endif
    if (output_mode != BenchmarkDetailed) {
        if (output_mode == BenchmarkCsv) {
            print_csv_measurement(&measurement);
        } else {
            print_load_sweep_measurement(&measurement);
        }
        destroy_api_hashmap(hashmap);
        return success;
    }

    char observed[96];
    char theoretical_reference[128];
    print_scorecard_header("standard");
    snprintf(observed, sizeof(observed), "%zu / %zu", inserted, wordlist->size);
    snprintf(theoretical_reference, sizeof(theoretical_reference), "%zu required", wordlist->size);
    print_scorecard_row("insertions", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%zu fixed slots", api_hashmap_capacity(hashmap));
    snprintf(theoretical_reference, sizeof(theoretical_reference), "%d shared slots", capacity);
    print_scorecard_row("capacity", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%.6f", (double)api_hashmap_size(hashmap) / (double)api_hashmap_capacity(hashmap));
    snprintf(theoretical_reference, sizeof(theoretical_reference), "%.6f target", (double)wordlist->size / (double)capacity);
    print_scorecard_row("load factor", observed, theoretical_reference);
    snprintf(observed, sizeof(observed), "%zu / %zu", positive_lookups, wordlist->size);
    snprintf(theoretical_reference, sizeof(theoretical_reference), "%zu required", wordlist->size);
    print_scorecard_row("positive lookups", observed, theoretical_reference);
    print_scorecard_row("negative lookup", is_none(missing_result.option) ? "pass" : "fail", "absent");
    snprintf(observed, sizeof(observed), "%.6f s", elapsed_seconds(insertion_start, insertion_end));
    print_scorecard_row("insertion CPU time", observed, "-");
    snprintf(observed, sizeof(observed), "%.6f s", elapsed_seconds(positive_lookup_start, positive_lookup_end));
    print_scorecard_row("positive lookup CPU time", observed, "-");
    snprintf(observed, sizeof(observed), "%.6f s", elapsed_seconds(negative_lookup_start, negative_lookup_end));
    print_scorecard_row("negative lookup CPU time", observed, "-");
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)insertion_stats.insertion_probes);
    print_scorecard_row("insertion probes total", observed, "-");
    snprintf(observed, sizeof(observed), "%.6f", probes_per_operation(insertion_stats.insertion_probes, insertion_stats.insertion_ops));
    print_scorecard_row("insertion probes average", observed, "-");
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)insertion_stats.maximum_insertion_probes);
    print_scorecard_row("insertion probes maximum", observed, "-");
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)positive_lookup_stats.lookup_probes);
    print_scorecard_row("positive lookup probes total", observed, "-");
    snprintf(observed, sizeof(observed), "%.6f", probes_per_operation(positive_lookup_stats.lookup_probes, positive_lookup_stats.lookup_ops));
    print_scorecard_row("positive lookup probes average", observed, "-");
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)positive_lookup_stats.maximum_lookup_probes);
    print_scorecard_row("positive lookup probes maximum", observed, "-");
    snprintf(observed, sizeof(observed), "%llu", (unsigned long long)negative_lookup_stats.lookup_probes);
    print_scorecard_row("negative lookup probes", observed, "-");
#endif

    destroy_api_hashmap(hashmap);
    return success;
}

/// Checks that a mode contains one or more known implementation bits and no unknown bits
static bool valid_wordlist_mode(const WordlistHashmapMode mode) {
    const unsigned int mode_bits = (unsigned int)mode;
    return mode_bits > 0 && (mode_bits & ~(unsigned int)WordlistAll) == 0;
}

/// Checks whether one implementation was selected in a possibly combined mode
static bool mode_includes(const WordlistHashmapMode mode, const WordlistHashmapMode implementation) {
    return (mode & implementation) != 0;
}

/// Runs every selected implementation on an already loaded set of unique words
static int run_loaded_test(const WordList* wordlist, const char* source_name, const WordlistHashmapMode mode, const int capacity, const float delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE], const BenchmarkOutputMode output_mode, const double c) {
    if (mode_includes(mode, WordlistFunnel)) {
        FunnelPartition* partition = partition_funnel_hashmap(capacity, (double)delta);
        if (partition == NULL) {
            fprintf(stderr, "capacity %d and delta %.6f do not produce a valid Funnel partition\n", capacity, (double)delta);
            return 5;
        }
        delete_funnel_partition(partition);
    }

    char* missing_word = choose_missing_word(wordlist);
    if (missing_word == NULL) {
        fprintf(stderr, "could not construct a negative lookup key\n");
        return 6;
    }

    const int paper_target = elastic_target_size(capacity, delta);
    if (output_mode == BenchmarkDetailed) {
        char observed[96];
        char theoretical_reference[160];
        print_scorecard_header("benchmark");
        print_scorecard_row("input", source_name, "-");
        snprintf(observed, sizeof(observed), "%zu", wordlist->token_count);
        print_scorecard_row("tokens", observed, "-");
        snprintf(observed, sizeof(observed), "%zu", wordlist->size);
        snprintf(theoretical_reference, sizeof(theoretical_reference), "%d target insertions", paper_target);
        print_scorecard_row("unique words", observed, theoretical_reference);
        snprintf(observed, sizeof(observed), "%zu", wordlist->token_count - wordlist->size);
        print_scorecard_row("duplicates ignored", observed, "-");
        snprintf(observed, sizeof(observed), "%.6f", (double)delta);
        print_scorecard_row("delta", observed, "target free fraction");
        print_scorecard_row("delta inverse power of two", delta_inverse_is_power_of_two(delta) ? "yes" : "no", "required for Elastic theorem");
        snprintf(observed, sizeof(observed), "%d", capacity);
        print_scorecard_row("shared capacity", observed, "fixed for every selected map");
        snprintf(observed, sizeof(observed), "%d", capacity - paper_target);
        print_scorecard_row("target free slots", observed, "floor(delta * capacity)");
        if (seed != NULL) {
            char seed_hex[SIPHASH_2_4_KEY_SIZE * 2 + 1];
            for (size_t i = 0; i < (size_t)SIPHASH_2_4_KEY_SIZE; i++) {
                snprintf(&seed_hex[i * 2], 3, "%02x", (unsigned int)seed[i]);
            }
            seed_hex[sizeof(seed_hex) - 1] = '\0';
            print_scorecard_row("SipHash seed", seed_hex, "same seed for selected maps");
        } else {
            print_scorecard_row("SipHash seed", "control default", "-");
        }
    } else if (output_mode == BenchmarkLoadSweep) {
        printf("\n[load %.6f%%, delta %.8f, capacity %d, keys %zu]\n", 100.0 * (double)wordlist->size / (double)capacity, (double)delta, capacity, wordlist->size);
        print_load_sweep_header();
    }

    bool success = true;
    if (mode_includes(mode, WordlistStandard)) {
        success = run_standard(wordlist, source_name, capacity, delta, missing_word, seed, output_mode) && success;
    }
    if (mode_includes(mode, WordlistElastic)) {
        success = run_elastic(wordlist, source_name, capacity, delta, seed, missing_word, output_mode, c) && success;
    }
    if (mode_includes(mode, WordlistFunnel)) {
        success = run_funnel(wordlist, source_name, capacity, (double)delta, seed, missing_word, output_mode) && success;
    }

    free(missing_word);
    if (!success) {
        fprintf(stderr, "hashmap demo failed\n");
        return 7;
    }
    return 0;
}

/// Loads one wordlist and selects either detailed or CSV output
static int run_wordlist_test_with_output(const char* path, const WordlistHashmapMode mode, const float delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE], const BenchmarkOutputMode output_mode) {
    if (path == NULL || !isfinite(delta) || delta <= 0.0f || delta >= 1.0f || !valid_wordlist_mode(mode) || ((mode_includes(mode, WordlistElastic) || mode_includes(mode, WordlistFunnel)) && seed == NULL)) {
        fprintf(stderr, "invalid wordlist test arguments\n");
        return 1;
    }

    WordList wordlist = {0};
    if (!load_unique_words(path, &wordlist)) {
        delete_wordlist(&wordlist);
        fprintf(stderr, "could not load wordlist\n");
        return 2;
    }
    if (wordlist.size == 0) {
        delete_wordlist(&wordlist);
        fprintf(stderr, "wordlist contains no words\n");
        return 3;
    }

    int capacity = 0;
    if (!elastic_capacity_for_size(wordlist.size, delta, &capacity)) {
        delete_wordlist(&wordlist);
        fprintf(stderr, "wordlist is too large for the fixed-capacity implementations\n");
        return 4;
    }

    if (output_mode == BenchmarkCsv) {
        print_csv_header();
    }
    const int result = run_loaded_test(&wordlist, path, mode, capacity, delta, seed, output_mode, elastic_c_constant());
    delete_wordlist(&wordlist);
    return result;
}

int run_wordlist_test(const char* path, const WordlistHashmapMode mode, const float delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    return run_wordlist_test_with_output(path, mode, delta, seed, BenchmarkDetailed);
}

int run_wordlist_test_csv(const char* path, const WordlistHashmapMode mode, const float delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    return run_wordlist_test_with_output(path, mode, delta, seed, BenchmarkCsv);
}

/// Selects the largest power-of-two capacity supported by the wordlist
static int wordlist_load_sweep_capacity(const size_t word_count, const int maximum_capacity) {
    if (word_count == 0 || maximum_capacity <= 0) {
        return 0;
    }

    int candidate = 1;
    while (candidate <= maximum_capacity / 2) {
        candidate *= 2;
    }

    const float smallest_delta = LOAD_SWEEP_DELTAS[sizeof(LOAD_SWEEP_DELTAS) / sizeof(LOAD_SWEEP_DELTAS[0]) - 1];
    while (candidate > 0) {
        const bool enough_words = (size_t)elastic_target_size(candidate, smallest_delta) <= word_count;
        if (enough_words) {
            return candidate;
        }
        candidate /= 2;
    }
    return 0;
}

/// Loads one wordlist and runs the same prefix of unique words at every requested load factor
static int run_wordlist_load_sweep_with_output(const char* path, const int maximum_capacity, const WordlistHashmapMode mode, const uint8_t seed[SIPHASH_2_4_KEY_SIZE], const BenchmarkOutputMode output_mode) {
    if (path == NULL || maximum_capacity <= 0 || !valid_wordlist_mode(mode) || ((mode_includes(mode, WordlistElastic) || mode_includes(mode, WordlistFunnel)) && seed == NULL)) {
        fprintf(stderr, "invalid wordlist sweep arguments\n");
        return 1;
    }

    WordList wordlist = {0};
    if (!load_unique_words(path, &wordlist)) {
        delete_wordlist(&wordlist);
        fprintf(stderr, "could not load wordlist\n");
        return 2;
    }

    const int capacity = wordlist_load_sweep_capacity(wordlist.size, maximum_capacity);
    if (capacity == 0) {
        fprintf(stderr, "the wordlist has %zu unique words but no load sweep fits below capacity %d\n", wordlist.size, maximum_capacity);
        delete_wordlist(&wordlist);
        return 3;
    }

    if (output_mode == BenchmarkCsv) {
        print_csv_header();
    } else {
        printf("\n[wordlist sweep: %s, %zu unique words available, capacity %d]\n", path, wordlist.size, capacity);
        fflush(stdout);
    }

    int result = 0;
    for (size_t i = 0; i < sizeof(LOAD_SWEEP_DELTAS) / sizeof(LOAD_SWEEP_DELTAS[0]); i++) {
        WordlistHashmapMode point_mode = mode;
        if (mode_includes(mode, WordlistFunnel)) {
            FunnelPartition* partition = partition_funnel_hashmap(capacity, (double)LOAD_SWEEP_DELTAS[i]);
            if (partition == NULL) {
                point_mode = (WordlistHashmapMode)((unsigned int)point_mode & ~(unsigned int)WordlistFunnel);
                fprintf(stderr, "skipping Funnel at delta %.8f because capacity %d does not produce a valid finite partition\n", (double)LOAD_SWEEP_DELTAS[i], capacity);
            }
            delete_funnel_partition(partition);
        }
        if (!valid_wordlist_mode(point_mode)) {
            continue;
        }
        WordList prefix = {
            .words = wordlist.words,
            .size = (size_t)elastic_target_size(capacity, LOAD_SWEEP_DELTAS[i]),
            .capacity = wordlist.capacity,
            .token_count = wordlist.token_count,
        };
        result = run_loaded_test(&prefix, path, point_mode, capacity, LOAD_SWEEP_DELTAS[i], seed, output_mode, elastic_c_constant());
        if (result != 0) {
            break;
        }
    }

    delete_wordlist(&wordlist);
    return result;
}

int run_wordlist_load_sweep(const char* path, const int maximum_capacity, const WordlistHashmapMode mode, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    return run_wordlist_load_sweep_with_output(path, maximum_capacity, mode, seed, BenchmarkLoadSweep);
}

int run_wordlist_load_sweep_csv(const char* path, const int maximum_capacity, const WordlistHashmapMode mode, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    return run_wordlist_load_sweep_with_output(path, maximum_capacity, mode, seed, BenchmarkCsv);
}

/// Builds the deterministic generated wordlist shared by demos and sweeps
static bool create_generated_wordlist(const int capacity, const float delta, WordList* wordlist) {
    if (capacity <= 0 || !isfinite(delta) || delta <= 0.0f || delta >= 1.0f || wordlist == NULL) {
        return false;
    }
    const int insertion_count = elastic_target_size(capacity, delta);
    *wordlist = (WordList){.token_count = (size_t)insertion_count};
    for (int i = 0; i < insertion_count; i++) {
        char generated_key[64];
        const int written = snprintf(generated_key, sizeof(generated_key), "generated-key-%d", i);
        char* key = written < 0 || (size_t)written >= sizeof(generated_key) ? NULL : copy_word(generated_key);
        if (key == NULL || !append_word(wordlist, key)) {
            free(key);
            delete_wordlist(wordlist);
            return false;
        }
    }
    return true;
}

int run_generated_test(const int capacity, const WordlistHashmapMode mode, const float delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    if (capacity <= 0 || !isfinite(delta) || delta <= 0.0f || delta >= 1.0f || !valid_wordlist_mode(mode) || ((mode_includes(mode, WordlistElastic) || mode_includes(mode, WordlistFunnel)) && seed == NULL)) {
        fprintf(stderr, "invalid generated test arguments\n");
        return 1;
    }

    WordList wordlist = {0};
    if (!create_generated_wordlist(capacity, delta, &wordlist)) {
        fprintf(stderr, "could not generate demo keys\n");
        return 2;
    }

    const int result = run_loaded_test(&wordlist, "generated keys", mode, capacity, delta, seed, BenchmarkDetailed, elastic_c_constant());
    delete_wordlist(&wordlist);
    return result;
}

/// Runs the generated load sweep using either the readable table or CSV output
static int run_generated_load_sweep_with_output(const int capacity, const WordlistHashmapMode mode, const uint8_t seed[SIPHASH_2_4_KEY_SIZE], const BenchmarkOutputMode output_mode) {
    if (capacity <= 0 || !valid_wordlist_mode(mode) || ((mode_includes(mode, WordlistElastic) || mode_includes(mode, WordlistFunnel)) && seed == NULL)) {
        fprintf(stderr, "invalid generated sweep arguments\n");
        return 1;
    }
    if (output_mode == BenchmarkCsv) {
        print_csv_header();
    }

    for (size_t i = 0; i < sizeof(LOAD_SWEEP_DELTAS) / sizeof(LOAD_SWEEP_DELTAS[0]); i++) {
        WordList wordlist = {0};
        if (!create_generated_wordlist(capacity, LOAD_SWEEP_DELTAS[i], &wordlist)) {
            fprintf(stderr, "could not generate sweep keys\n");
            return 2;
        }
        const int result = run_loaded_test(&wordlist, "generated sweep keys", mode, capacity, LOAD_SWEEP_DELTAS[i], seed, output_mode, elastic_c_constant());
        delete_wordlist(&wordlist);
        if (result != 0) {
            return result;
        }
    }
    return 0;
}

int run_generated_load_sweep(const int capacity, const WordlistHashmapMode mode, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    return run_generated_load_sweep_with_output(capacity, mode, seed, BenchmarkLoadSweep);
}

int run_generated_load_sweep_csv(const int capacity, const WordlistHashmapMode mode, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    return run_generated_load_sweep_with_output(capacity, mode, seed, BenchmarkCsv);
}

/// Runs the Elastic c sweep using either the readable table or CSV output
static int run_elastic_c_sweep_with_output(const int capacity, const float delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE], const BenchmarkOutputMode output_mode) {
    static const double constants[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 100.0};
    if (capacity <= 0 || !isfinite(delta) || delta <= 0.0f || delta >= 1.0f || seed == NULL) {
        fprintf(stderr, "invalid Elastic c sweep arguments\n");
        return 1;
    }

    WordList wordlist = {0};
    if (!create_generated_wordlist(capacity, delta, &wordlist)) {
        fprintf(stderr, "could not generate Elastic c sweep keys\n");
        return 2;
    }
    char* missing_word = choose_missing_word(&wordlist);
    if (missing_word == NULL) {
        delete_wordlist(&wordlist);
        fprintf(stderr, "could not construct a negative lookup key\n");
        return 3;
    }

    if (output_mode == BenchmarkCsv) {
        print_csv_header();
    } else {
        printf("\n[Elastic c sweep, load %.6f%%, delta %.8f, capacity %d, keys %zu]\n", 100.0 * (double)wordlist.size / (double)capacity, (double)delta, capacity, wordlist.size);
        print_c_sweep_header();
    }
    bool success = true;
    for (size_t i = 0; i < sizeof(constants) / sizeof(constants[0]); i++) {
        success = run_elastic(&wordlist, "generated c sweep keys", capacity, delta, seed, missing_word, output_mode, constants[i]) && success;
    }

    free(missing_word);
    delete_wordlist(&wordlist);
    if (!success) {
        fprintf(stderr, "Elastic c sweep failed\n");
        return 4;
    }
    return 0;
}

int run_elastic_c_sweep(const int capacity, const float delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    return run_elastic_c_sweep_with_output(capacity, delta, seed, BenchmarkCSweep);
}

int run_elastic_c_sweep_csv(const int capacity, const float delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    return run_elastic_c_sweep_with_output(capacity, delta, seed, BenchmarkCsv);
}
