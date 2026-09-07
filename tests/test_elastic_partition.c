#include "elastic_hashing.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define EXPECT(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "test failed: %s at %s:%d\n", #condition, __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

static int check_partition(const int capacity) {
    const int n_subarrays = n_elastic_subarrays(capacity);
    ElasticSubArray* subarrays = partition_elastic_hashmap(capacity);
    EXPECT(subarrays != NULL);
    EXPECT(n_subarrays > 0);

    int expected_start = 0;
    for (int i = 0; i < n_subarrays; i++) {
        EXPECT(subarrays[i].starting_index == expected_start);
        EXPECT(subarrays[i].length > 0);
        EXPECT(subarrays[i].size == 0);

        if (i + 1 < n_subarrays) {
            EXPECT(abs(2 * subarrays[i + 1].length - subarrays[i].length) <= 2);
        }

        expected_start += subarrays[i].length;
    }

    EXPECT(expected_start == capacity);

    free(subarrays);
    return 0;
}

static int check_slide_example(void) {
    const int n_subarrays = n_elastic_subarrays(15);
    ElasticSubArray* subarrays = partition_elastic_hashmap(15);
    EXPECT(subarrays != NULL);

    EXPECT(n_subarrays == 4);
    EXPECT(subarrays[0].starting_index == 0);
    EXPECT(subarrays[0].length == 8);
    EXPECT(subarrays[1].starting_index == 8);
    EXPECT(subarrays[1].length == 4);
    EXPECT(subarrays[2].starting_index == 12);
    EXPECT(subarrays[2].length == 2);
    EXPECT(subarrays[3].starting_index == 14);
    EXPECT(subarrays[3].length == 1);

    free(subarrays);
    return 0;
}

static int check_maximum_capacity(void) {
    const int n_subarrays = n_elastic_subarrays(INT_MAX);
    ElasticSubArray* subarrays = partition_elastic_hashmap(INT_MAX);
    EXPECT(n_subarrays == 31);
    EXPECT(subarrays != NULL);

    int64_t covered = 0;
    for (int i = 0; i < n_subarrays; i++) {
        EXPECT(subarrays[i].starting_index == covered);
        EXPECT(subarrays[i].length > 0);
        covered += subarrays[i].length;
    }
    EXPECT(covered == INT_MAX);

    free(subarrays);
    return 0;
}

int main(void) {
    EXPECT(partition_elastic_hashmap(0) == NULL);

    EXPECT(check_slide_example() == 0);
    EXPECT(check_partition(1) == 0);
    EXPECT(check_partition(2) == 0);
    EXPECT(check_partition(5) == 0);
    EXPECT(check_partition(16) == 0);
    EXPECT(check_partition(17) == 0);
    EXPECT(check_maximum_capacity() == 0);

    return 0;
}
