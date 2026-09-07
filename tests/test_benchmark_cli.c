#include "benchmark_cli.h"

#include <assert.h>

int main(void) {
    char* valid_arguments[] = {
        "HashMaps",
        "tests/wordlist_sample.txt",
        "standard",
        "0.25",
        "000102030405060708090a0b0c0d0e0f",
    };
    assert(run_benchmark_cli(5, valid_arguments) == 0);

    char* invalid_seed_arguments[] = {
        "HashMaps",
        "--demo",
        "elastic",
        "0.125",
        "not-a-16-byte-seed",
    };
    assert(run_benchmark_cli(5, invalid_seed_arguments) != 0);

    char* invalid_delta_arguments[] = {
        "HashMaps",
        "--demo",
        "elastic",
        "0",
    };
    assert(run_benchmark_cli(4, invalid_delta_arguments) != 0);

#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    char* lookup_comparison_arguments[] = {
        "HashMapsProbes",
        "--elastic-lookup-comparison",
        "tests/wordlist_sample.txt",
        "128",
        "0.25",
    };
    assert(run_benchmark_cli(5, lookup_comparison_arguments) == 0);
#endif
    return 0;
}
