#include "benchmark_cli.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wordlist_runner.h"

static const uint8_t DEFAULT_BENCHMARK_SEED[SIPHASH_2_4_KEY_SIZE] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

static const int DEFAULT_LOAD_SWEEP_CAPACITY = 65536;
static const int DEFAULT_C_SWEEP_CAPACITY = 16384;
static const int DEFAULT_LOOKUP_COMPARISON_CAPACITY = 4096;

/// Converts the mode name used on the command line into the corresponding mode bits
static bool parse_mode(const char* text, WordlistHashmapMode* mode) {
    if (text == NULL || mode == NULL) {
        return false;
    }

    if (strcmp(text, "standard") == 0) {
        *mode = WordlistStandard;
    } else if (strcmp(text, "elastic") == 0) {
        *mode = WordlistElastic;
    } else if (strcmp(text, "funnel") == 0) {
        *mode = WordlistFunnel;
    } else if (strcmp(text, "both") == 0) {
        *mode = WordlistBoth;
    } else if (strcmp(text, "funnel-control") == 0) {
        *mode = WordlistFunnelControl;
    } else if (strcmp(text, "all") == 0) {
        *mode = WordlistAll;
    } else {
        return false;
    }
    return true;
}

/// Parses delta and rejects values outside the interval accepted by the benchmark runner
static bool parse_delta(const char* text, float* delta) {
    if (text == NULL || delta == NULL) {
        return false;
    }

    char* end = NULL;
    errno = 0;
    const float parsed_delta = strtof(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !isfinite(parsed_delta) || parsed_delta <= 0.0f || parsed_delta >= 1.0f) {
        return false;
    }

    *delta = parsed_delta;
    return true;
}

/// Parses a positive capacity which fits the int-based hashmap APIs
static bool parse_capacity(const char* text, int* capacity) {
    if (text == NULL || capacity == NULL) {
        return false;
    }

    char* end = NULL;
    errno = 0;
    const long parsed_capacity = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed_capacity <= 0 || parsed_capacity > INT_MAX) {
        return false;
    }

    *capacity = (int)parsed_capacity;
    return true;
}

/// Converts one hexadecimal character into the value of one four bit nibble
static int hex_nibble(const char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

/// Decodes 32 hexadecimal characters into the 16 raw bytes required by SipHash
/// Two characters encode one byte so seeds can be written in an email or command line
static bool parse_seed_hex(const char* text, uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    if (text == NULL || seed == NULL || strlen(text) != (size_t)SIPHASH_2_4_KEY_SIZE * 2) {
        return false;
    }

    for (size_t i = 0; i < (size_t)SIPHASH_2_4_KEY_SIZE; i++) {
        const int high = hex_nibble(text[i * 2]);
        const int low = hex_nibble(text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        seed[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

/// Prints the available ways to call the C benchmark runner
static void print_usage(const char* program_name) {
    const char* name = program_name == NULL ? "HashMaps" : program_name;
    fprintf(stderr, "usage: %s <wordlist> <standard|elastic|funnel|both|funnel-control|all> [delta] [32-hex-digit-seed]\n", name);
    fprintf(stderr, "       %s <wordlist>\n", name);
    fprintf(stderr, "       %s --wordlist-sweep <wordlist> [maximum-capacity] [32-hex-digit-seed]\n", name);
    fprintf(stderr, "       %s --csv-wordlist-sweep <wordlist> [maximum-capacity] [32-hex-digit-seed]\n", name);
    fprintf(stderr, "       %s --csv-wordlist <wordlist> <standard|elastic|funnel|both|funnel-control|all> [delta] [32-hex-digit-seed]\n", name);
    fprintf(stderr, "       %s --demo <standard|elastic|funnel|both|funnel-control|all> [delta] [32-hex-digit-seed]\n", name);
    fprintf(stderr, "       %s --c-sweep [delta] [32-hex-digit-seed]\n", name);
    fprintf(stderr, "       %s --csv-load-sweep <standard|elastic|funnel|both|funnel-control|all> [capacity] [32-hex-digit-seed]\n", name);
    fprintf(stderr, "       %s --csv-c-sweep [delta] [32-hex-digit-seed]\n", name);
    fprintf(stderr, "       %s --elastic-lookup-comparison <wordlist> [maximum-capacity] [delta] [32-hex-digit-seed]\n", name);
    fprintf(stderr, "       %s --csv-elastic-lookup-comparison <wordlist> [maximum-capacity] [delta] [32-hex-digit-seed]\n", name);
    fprintf(stderr, "words are separated by whitespace and duplicate words are ignored\n");
    fprintf(stderr, "one wordlist argument runs all maps at the four standard load factors\n");
    fprintf(stderr, "--demo without delta runs the default high-load sweep\n");
    fprintf(stderr, "csv commands print only comma-separated data to stdout\n");
    fprintf(stderr, "delta defaults to 0.125 and a power-of-two inverse should be used for theorem comparisons\n");
    fprintf(stderr, "the seed defaults to 000102030405060708090a0b0c0d0e0f\n");
}

int run_benchmark_cli(const int argc, char** argv) {
    if (argc == 2 && argv[1][0] != '-') {
        uint8_t seed[SIPHASH_2_4_KEY_SIZE];
        memcpy(seed, DEFAULT_BENCHMARK_SEED, sizeof(seed));
        return run_wordlist_load_sweep(argv[1], DEFAULT_LOAD_SWEEP_CAPACITY, WordlistAll, seed);
    }

    if (argc >= 2 && (strcmp(argv[1], "--elastic-lookup-comparison") == 0 || strcmp(argv[1], "--csv-elastic-lookup-comparison") == 0)) {
        if (argc < 3 || argc > 6) {
            print_usage(argc > 0 ? argv[0] : NULL);
            return 1;
        }
        int maximum_capacity = DEFAULT_LOOKUP_COMPARISON_CAPACITY;
        if (argc >= 4 && !parse_capacity(argv[3], &maximum_capacity)) {
            print_usage(argv[0]);
            return 1;
        }
        float delta = 0.125f;
        if (argc >= 5 && !parse_delta(argv[4], &delta)) {
            print_usage(argv[0]);
            return 1;
        }
        uint8_t seed[SIPHASH_2_4_KEY_SIZE];
        memcpy(seed, DEFAULT_BENCHMARK_SEED, sizeof(seed));
        if (argc == 6 && !parse_seed_hex(argv[5], seed)) {
            print_usage(argv[0]);
            return 1;
        }
        return strcmp(argv[1], "--csv-elastic-lookup-comparison") == 0 ? run_wordlist_elastic_lookup_comparison_csv(argv[2], maximum_capacity, delta, seed) : run_wordlist_elastic_lookup_comparison(argv[2], maximum_capacity, delta, seed);
    }

    if (argc >= 2 && (strcmp(argv[1], "--wordlist-sweep") == 0 || strcmp(argv[1], "--csv-wordlist-sweep") == 0)) {
        if (argc < 3 || argc > 5) {
            print_usage(argc > 0 ? argv[0] : NULL);
            return 1;
        }
        int maximum_capacity = DEFAULT_LOAD_SWEEP_CAPACITY;
        if (argc >= 4 && !parse_capacity(argv[3], &maximum_capacity)) {
            print_usage(argv[0]);
            return 1;
        }
        uint8_t seed[SIPHASH_2_4_KEY_SIZE];
        memcpy(seed, DEFAULT_BENCHMARK_SEED, sizeof(seed));
        if (argc == 5 && !parse_seed_hex(argv[4], seed)) {
            print_usage(argv[0]);
            return 1;
        }
        return strcmp(argv[1], "--csv-wordlist-sweep") == 0 ? run_wordlist_load_sweep_csv(argv[2], maximum_capacity, WordlistAll, seed) : run_wordlist_load_sweep(argv[2], maximum_capacity, WordlistAll, seed);
    }

    if (argc >= 2 && strcmp(argv[1], "--csv-wordlist") == 0) {
        if (argc < 4 || argc > 6) {
            print_usage(argc > 0 ? argv[0] : NULL);
            return 1;
        }
        WordlistHashmapMode mode;
        if (!parse_mode(argv[3], &mode)) {
            print_usage(argv[0]);
            return 1;
        }
        float delta = 0.125f;
        if (argc >= 5 && !parse_delta(argv[4], &delta)) {
            print_usage(argv[0]);
            return 1;
        }
        uint8_t seed[SIPHASH_2_4_KEY_SIZE];
        memcpy(seed, DEFAULT_BENCHMARK_SEED, sizeof(seed));
        if (argc == 6 && !parse_seed_hex(argv[5], seed)) {
            print_usage(argv[0]);
            return 1;
        }
        return run_wordlist_test_csv(argv[2], mode, delta, seed);
    }

    if (argc >= 2 && (strcmp(argv[1], "--c-sweep") == 0 || strcmp(argv[1], "--csv-c-sweep") == 0)) {
        if (argc > 4) {
            print_usage(argc > 0 ? argv[0] : NULL);
            return 1;
        }
        float delta = 0.125f;
        if (argc >= 3 && !parse_delta(argv[2], &delta)) {
            print_usage(argv[0]);
            return 1;
        }
        uint8_t seed[SIPHASH_2_4_KEY_SIZE];
        memcpy(seed, DEFAULT_BENCHMARK_SEED, sizeof(seed));
        if (argc == 4 && !parse_seed_hex(argv[3], seed)) {
            print_usage(argv[0]);
            return 1;
        }
        return strcmp(argv[1], "--csv-c-sweep") == 0 ? run_elastic_c_sweep_csv(DEFAULT_C_SWEEP_CAPACITY, delta, seed) : run_elastic_c_sweep(DEFAULT_C_SWEEP_CAPACITY, delta, seed);
    }

    if (argc >= 2 && strcmp(argv[1], "--csv-load-sweep") == 0) {
        // --csv-load-sweep <mode> [capacity] [seed]
        // capacity is optional so that the scaling study can sweep n without a wordlist
        if (argc < 3 || argc > 5) {
            print_usage(argc > 0 ? argv[0] : NULL);
            return 1;
        }
        WordlistHashmapMode mode;
        if (!parse_mode(argv[2], &mode)) {
            print_usage(argv[0]);
            return 1;
        }
        int capacity = DEFAULT_LOAD_SWEEP_CAPACITY;
        int next_argument = 3;
        if (argc > 3 && argv[3][0] != '\0' && strspn(argv[3], "0123456789") == strlen(argv[3])) {
            const long parsed = strtol(argv[3], NULL, 10);
            if (parsed <= 0 || parsed > INT_MAX) {
                print_usage(argv[0]);
                return 1;
            }
            capacity = (int)parsed;
            next_argument = 4;
        }
        uint8_t seed[SIPHASH_2_4_KEY_SIZE];
        memcpy(seed, DEFAULT_BENCHMARK_SEED, sizeof(seed));
        if (argc > next_argument && !parse_seed_hex(argv[next_argument], seed)) {
            print_usage(argv[0]);
            return 1;
        }
        return run_generated_load_sweep_csv(capacity, mode, seed);
    }

    if (argc < 3 || argc > 5) {
        print_usage(argc > 0 ? argv[0] : NULL);
        return 1;
    }

    WordlistHashmapMode mode;
    if (!parse_mode(argv[2], &mode)) {
        print_usage(argv[0]);
        return 1;
    }

    float delta = 0.125f;
    if (argc >= 4 && !parse_delta(argv[3], &delta)) {
        print_usage(argv[0]);
        return 1;
    }

    uint8_t seed[SIPHASH_2_4_KEY_SIZE];
    memcpy(seed, DEFAULT_BENCHMARK_SEED, sizeof(seed));
    if (argc == 5 && !parse_seed_hex(argv[4], seed)) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--demo") == 0) {
        if (argc == 3) {
            return run_generated_load_sweep(DEFAULT_LOAD_SWEEP_CAPACITY, mode, seed);
        }
        return run_generated_test(1024, mode, delta, seed);
    }
    return run_wordlist_test(argv[1], mode, delta, seed);
}
