#ifndef HASHMAPS_HASH_FUNCTIONS_H
#define HASHMAPS_HASH_FUNCTIONS_H

#include <stddef.h>
#include <stdint.h>

#include "commons.h"

/// DO NOT USE THIS DIRECTLY
/// Wrapper for the SipHash implementation (in the third_party dir)
/// The standard implementation SipHash writes its result through an output parameter, this function returns the 64 bit result directly
///
/// The same data and key always return the same result, which lets lookups rebuild the insertion probe sequence
/// Different data can still produce the same result, this is normal for a finite 64 bit hash output
///
/// Use one randomly chosen key as the seed for a complete benchmark run and record it for reproducibility
/// input points to length bytes, they are what is actually hashed => for probing there should be key + probe number
/// key must point to SIPHASH_2_4_KEY_SIZE bytes
/// The caller should mod the returned value to a valid subarray index
uint64_t _siphash_2_4_64(const void* input, size_t length, const uint8_t key[SIPHASH_2_4_KEY_SIZE]);

/// Designed as the hash function for Elastic Hashing probes
/// !! The caller needs to run % subarray_size on the output of the hash function
///
/// Builds the 64 bit value for the one dimensional probe h_probe_number(element_key)
/// probe_number is one-based and must use the same value during insertion and lookup, the subscript in h_i(x)
///
/// The hashed input is the fixed size probe_number followed by the key bytes
/// The function does not choose a subarray, the caller reduces the returned value using the appropriate subarray lenght
/// Use the same seed for every probe in the same benchmark run
/// element_key must be a null terminated string and must remain valid during the call
/// Returns the raw 64 bit hash value before it is reduced to a table index
uint64_t siphash_probe64(const char* element_key, uint64_t probe_number, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]);

#endif
