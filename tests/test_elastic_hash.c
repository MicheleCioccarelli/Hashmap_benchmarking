#include <assert.h>
#include <stdint.h>

#include "elastic_hashing.h"

int main(void) {
    assert(elastic_c_constant() == 100.0);
    const uint8_t key[SIPHASH_2_4_KEY_SIZE] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    const uint8_t input[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
    };

    assert(_siphash_2_4_64(input, sizeof(input), key) == UINT64_C(0xa129ca6149be45e5));

    const uint8_t elastic_probe_input[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
        'a', 'l', 'i', 'c', 'e',
    };

    assert(siphash_probe64("alice", 8, key)
           == _siphash_2_4_64(elastic_probe_input, sizeof(elastic_probe_input), key));

    assert(elastic_phi(1, 1) == 13);
    assert(elastic_phi(1, 2) == 57);
    assert(elastic_phi(2, 1) == 26);
    assert(elastic_phi(0, 1) == 0);
    assert(elastic_phi(1, 0) == 0);
    assert(elastic_phi(UINT64_MAX, 1) == 0);

    uint64_t seen[64 * 64];
    int seen_count = 0;
    for (uint64_t i = 1; i <= 64; i++) {
        for (uint64_t j = 1; j <= 64; j++) {
            const uint64_t probe_number = elastic_phi(i, j);
            assert(probe_number != 0);
            for (int k = 0; k < seen_count; k++) {
                assert(seen[k] != probe_number);
            }
            seen[seen_count] = probe_number;
            seen_count++;
        }
    }

    return 0;
}
