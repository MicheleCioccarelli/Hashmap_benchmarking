#ifndef HASHMAPS_BENCHMARK_CLI_H
#define HASHMAPS_BENCHMARK_CLI_H

/// Parses the benchmark command line and runs the selected C benchmark function
/// argv[1] is a wordlist path or one of the documented benchmark commands
/// A wordlist path without other arguments runs all maps at the four standard load factors
/// --wordlist-sweep and --csv-wordlist-sweep accept an optional maximum capacity and seed
/// --demo without delta runs the default high-load comparison for the selected implementation
/// --c-sweep compares finite Elastic c values and does not take an implementation mode
/// --elastic-lookup-comparison compares phi-aware, full-table and modular double-hash lookup on one Elastic table
/// The lookup comparison accepts a wordlist followed by optional maximum capacity, delta and seed
/// The csv commands run the corresponding benchmark with stable comma-separated output
/// For wordlists and --demo, argv[2] selects the hashmap implementation
/// argv[3] may contain delta and argv[4] may contain the SipHash seed
/// --csv-wordlist uses argv[2] for the path and argv[3] for the implementation mode
/// Its optional delta and seed are argv[4] and argv[5]
/// The optional 32 character hexadecimal value represents the 16 byte SipHash seed
/// A recorded seed makes a benchmark reproducible and different seeds sample different hash choices
/// Returns zero when the benchmark succeeds and a nonzero value for invalid input or a failed run
int run_benchmark_cli(int argc, char** argv);

#endif
