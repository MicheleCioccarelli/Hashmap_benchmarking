#include "funnel_hashing.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>

/// Checks that every index range and geometric bucket count follows the paper
static void check_partition(const int capacity, const double delta) {
    FunnelPartition* partition = partition_funnel_hashmap(capacity, delta);
    assert(partition != NULL);
    assert(partition->alpha == funnel_alpha(delta));
    assert(partition->beta == funnel_beta(delta));
    assert(partition->a_prime_length + partition->a_alpha_plus_one_length == capacity);
    assert(partition->a_prime_length % partition->beta == 0);
    assert(partition->a_alpha_plus_one_length >= (int)ceil(delta * capacity / 2.0));
    assert(partition->a_alpha_plus_one_length <= (int)floor(3.0 * delta * capacity / 4.0));

    int starting_index = 0;
    int bucket_sum = 0;
    for (int i = 0; i < partition->alpha; i++) {
        const Funnel_A_i* current = &partition->subarrays[i];
        assert(current->subarray_number == i + 1);
        assert(current->starting_index == starting_index);
        assert(current->n_buckets > 0);
        starting_index += current->n_buckets * partition->beta;
        bucket_sum += current->n_buckets;

        if (i + 1 < partition->alpha) {
            assert(fabs((double)partition->subarrays[i + 1].n_buckets - 0.75 * current->n_buckets) <= 1.0);
        }
    }

    assert(starting_index == partition->a_prime_length);
    assert(bucket_sum == partition->a_prime_length / partition->beta);
    assert(partition->b.starting_index == partition->a_prime_length);
    assert(partition->c.starting_index == partition->b.starting_index + partition->b.length);
    assert(partition->b.length + partition->c.length == partition->a_alpha_plus_one_length);
    assert(abs(partition->b.length - partition->c.length) <= 1);
    assert(partition->b_probe_limit == (int)ceil(log2(log2((double)capacity))));
    assert(partition->c_bucket_length == (int)ceil(2.0 * log2(log2((double)capacity))));
    // |C| need not divide by the bucket length: the buckets share the remainder out instead,
    // so they still tile C, still hold at least 2 log log n slots each, and differ by one at most
    assert(partition->c_bucket_count == partition->c.length / partition->c_bucket_length);
    int covered = 0, shortest = INT_MAX, longest = 0;
    for (int i = 0; i < partition->c_bucket_count; i++) {
        assert(partition->c_buckets[i].subarray_number == i + 1);
        assert(partition->c_buckets[i].starting_index == partition->c.starting_index + covered);
        assert(partition->c_buckets[i].length >= partition->c_bucket_length);
        assert(partition->c_buckets[i].size == 0);
        covered += partition->c_buckets[i].length;
        if (partition->c_buckets[i].length < shortest) shortest = partition->c_buckets[i].length;
        if (partition->c_buckets[i].length > longest) longest = partition->c_buckets[i].length;
    }
    assert(covered == partition->c.length);
    assert(longest - shortest <= 1);
    delete_funnel_partition(partition);
}

int main(void) {
    assert(funnel_alpha(0.125) == 22);
    assert(funnel_beta(0.125) == 6);
    assert(funnel_alpha(0.25) == 0);
    assert(funnel_beta(0.0) == 0);
    assert(partition_funnel_hashmap(128, 0.125) == NULL);

    check_partition(1024, 0.125);
    check_partition(4096, 0.0625);
    check_partition(16384, 0.0078125);
    check_partition(131072, 0.125);
    return 0;
}
