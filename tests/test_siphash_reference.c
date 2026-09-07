#include "siphash.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define EXPECT(condition)                                                        \
    do {                                                                         \
        if (!(condition)) {                                                      \
            fprintf(stderr, "Expectation failed at line %d: %s\n", __LINE__, #condition); \
            return 1;                                                            \
        }                                                                        \
    } while (0)

static int test_vector(const uint8_t* input, const size_t input_length,
                       const uint8_t* expected) {
    const uint8_t key[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    uint8_t result[8];

    EXPECT(siphash(input, input_length, key, result, sizeof(result)) == 0);
    EXPECT(memcmp(result, expected, sizeof(result)) == 0);
    return 0;
}

int main(void) {
    const uint8_t empty_input[1] = {0};
    const uint8_t empty_expected[8] = {
        0x31, 0x0e, 0x0e, 0xdd, 0x47, 0xdb, 0x6f, 0x72,
    };
    const uint8_t input_15[15] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
    };
    const uint8_t expected_15[8] = {
        0xe5, 0x45, 0xbe, 0x49, 0x61, 0xca, 0x29, 0xa1,
    };

    EXPECT(test_vector(empty_input, 0, empty_expected) == 0);
    EXPECT(test_vector(input_15, sizeof(input_15), expected_15) == 0);
    return 0;
}
