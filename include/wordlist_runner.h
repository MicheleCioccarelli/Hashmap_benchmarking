#ifndef HASHMAPS_WORDLIST_RUNNER_H
#define HASHMAPS_WORDLIST_RUNNER_H

#include <stdint.h>

#include "elastic_hashing.h"
#include "funnel_hashing.h"

typedef enum WordlistHashmapMode {
    WordlistStandard = 1,
    WordlistElastic = 2,
    WordlistFunnel = 4,
    WordlistBoth = WordlistStandard | WordlistElastic,
    WordlistFunnelControl = WordlistStandard | WordlistFunnel,
    WordlistAll = WordlistStandard | WordlistElastic | WordlistFunnel,
} WordlistHashmapMode;

/// Loads whitespace-separated words and runs the selected hashmap implementation (currently just control/elastic/funnel)
///
/// Duplicate words are removed while preserving the order of their first occurrence
///
/// delta determines the target free fraction and must be in the open interval (0, 1)
/// The Elastic theorem applies when delta^-1 is a power of two
/// seed must point to SIPHASH_2_4_KEY_SIZE bytes when Elastic Hashing is selected
/// A valid seed is also required when Funnel Hashing is selected
///
/// The same seed is used by every selected implementation for a fair initial-hash comparison
/// seed may be NULL when only the standard hashmap is selected, which uses its documented default
///
/// Returns zero on success and a nonzero value for invalid input, file errors or allocation failures
int run_wordlist_test(const char* path, WordlistHashmapMode mode, float delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Runs the same wordlist benchmark and writes one CSV row for every selected implementation
/// The first row contains stable column names and every row records the wordlist path
/// Probe fields are empty unless the complete program is built with HASHMAP_COUNT_PROBES
int run_wordlist_test_csv(const char* path, WordlistHashmapMode mode, float delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Runs the four standard load factors using prefixes of one deduplicated wordlist
/// The capacity is the largest power of two which does not exceed maximum_capacity or the available words
/// Every load factor uses the same capacity, seed and prefix order for a comparable campaign
/// Funnel points whose finite integer partition is invalid are skipped with an explicit diagnostic
int run_wordlist_load_sweep(const char* path, int maximum_capacity, WordlistHashmapMode mode, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Runs the same complete wordlist campaign with one CSV header and one row per map and load factor
/// The selected capacity and exact number of keys are recorded in every output row
int run_wordlist_load_sweep_csv(const char* path, int maximum_capacity, WordlistHashmapMode mode, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Generates capacity - floor(delta capacity) unique keys and runs the selected implementations
/// This avoids needing an external wordlist for a quick correctness and probe-counting demo
/// Funnel mode requires a capacity for which the paper's integer partition can be built
int run_generated_test(int capacity, WordlistHashmapMode mode, float delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Runs the selected implementations at 7/8, 31/32, 127/128 and 255/256 load
/// Every point uses the same capacity and seed and prints insertion and successful lookup measurements
/// Probe columns contain values only when the complete program is built with HASHMAP_COUNT_PROBES
int run_generated_load_sweep(int capacity, WordlistHashmapMode mode, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Runs the same generated load sweep and writes one CSV row for every implementation and load
/// The first row contains stable column names suitable for a spreadsheet or data analysis tool
/// Probe fields are empty unless the complete program is built with HASHMAP_COUNT_PROBES
int run_generated_load_sweep_csv(int capacity, WordlistHashmapMode mode, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Runs Elastic Hashing with c values from 0.25 through 100 on the same generated keys
/// The compact result includes Case 1 fallbacks and Case 3 cost in the probe-enabled build
/// Small c values are experiments outside the constant guaranteed by the paper
int run_elastic_c_sweep(int capacity, float delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

/// Runs the same Elastic c sweep and writes one CSV row for every tested value of c
/// The first row contains stable column names suitable for a spreadsheet or data analysis tool
/// Probe fields are empty unless the complete program is built with HASHMAP_COUNT_PROBES
int run_elastic_c_sweep_csv(int capacity, float delta, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

#endif
