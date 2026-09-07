#include "wordlist_runner.h"

#include <assert.h>

int main(void) {
    const uint8_t seed[SIPHASH_2_4_KEY_SIZE] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };

    assert(run_wordlist_test("tests/wordlist_sample.txt", WordlistBoth, 0.25f, seed) == 0);
    assert(run_wordlist_test("tests/wordlist_sample.txt", WordlistStandard, 0.25f, NULL) == 0);
    assert(run_generated_test(4096, WordlistAll, 0.125f, seed) == 0);
    return 0;
}
