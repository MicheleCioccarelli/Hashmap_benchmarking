#include "benchmark_cli.h"

/// Run with only a wordlist path to benchmark all three maps at every standard load factor
/// Additional command line forms remain available for individual maps, one delta and CSV output
int main(const int argc, char** argv) {
    return run_benchmark_cli(argc, argv);
}
