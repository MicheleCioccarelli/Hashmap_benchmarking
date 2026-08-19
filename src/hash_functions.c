#include "hash_functions.h"

#include <string.h>

#include "siphash.h"

uint64_t _siphash_2_4_64(const void* input, const size_t length, const uint8_t key[SIPHASH_2_4_KEY_SIZE]) {
    uint8_t output[sizeof(uint64_t)];

    siphash(input, length, key, output, sizeof(output));

    // SipHash writes its 64 bit result as little endian bytes
    return (uint64_t)output[0]
           | ((uint64_t)output[1] << 8)
           | ((uint64_t)output[2] << 16)
           | ((uint64_t)output[3] << 24)
           | ((uint64_t)output[4] << 32)
           | ((uint64_t)output[5] << 40)
           | ((uint64_t)output[6] << 48)
           | ((uint64_t)output[7] << 56);
}

/// Writes a fixed-width probe number so hashes are reproducible on every platform
static void write_u64_big_endian(uint8_t* output, const uint64_t value) {
    output[0] = (uint8_t)(value >> 56);
    output[1] = (uint8_t)(value >> 48);
    output[2] = (uint8_t)(value >> 40);
    output[3] = (uint8_t)(value >> 32);
    output[4] = (uint8_t)(value >> 24);
    output[5] = (uint8_t)(value >> 16);
    output[6] = (uint8_t)(value >> 8);
    output[7] = (uint8_t)value;
}

uint64_t siphash_probe64(const char* element_key, const uint64_t probe_number, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    const size_t key_length = strlen(element_key);
    const size_t probe_length = sizeof(probe_number);
    const size_t input_length = probe_length + key_length;
    uint8_t input[input_length];

    // The fixed size prefix separates the probe number from the key bytes
    write_u64_big_endian(input, probe_number);
    memcpy(input + probe_length, element_key, key_length);

    return _siphash_2_4_64(input, input_length, seed);
}
