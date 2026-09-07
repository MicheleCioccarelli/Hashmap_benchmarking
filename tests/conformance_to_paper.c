// Checks the implementation against the paper's SPECIFICATION, clause by clause.
// Each check names the clause and where it comes from, and prints PASS or FAIL.
//
//   usage: conformance_to_paper
//
// This answers one question and only one: does the code build the structure the
// paper describes? It says nothing about the asymptotic bounds, which are
// statements about expectations and cannot be settled by any finite run.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include "elastic_hashing.h"
#include "funnel_hashing.h"
#include "hash_functions.h"
#include "api_hashmap.h"

static int failures = 0, checks = 0;
static void report(const char* clause, const char* source, int ok, const char* detail) {
    checks++; if (!ok) failures++;
    printf("  [%s] %-46s  %s\n", ok ? "PASS" : "FAIL", clause, source);
    if (detail && *detail) printf("         %s\n", detail);
}

// ------------------------------------------------------------------ Elastic
static void elastic_partition_checks(void) {
    printf("\nELASTIC HASHING, Section 2\n");
    int halving_ok = 1, sum_ok = 1, positive_ok = 1, count_ok = 1;
    char detail[256] = "";
    for (int n = 2; n <= 200000; n++) {
        ElasticSubArray* s = partition_elastic_hashmap(n);
        const int ns = n_elastic_subarrays(n);
        long sum = 0;
        for (int i = 0; i < ns; i++) {
            sum += s[i].length;
            if (s[i].length <= 0) { positive_ok = 0; }
            if (i + 1 < ns) {
                // |A_{i+1}| = |A_i|/2 +- 1
                const int expected = s[i].length / 2;
                const int got = s[i + 1].length;
                if (got < expected - 1 || got > expected + 1) {
                    if (halving_ok) snprintf(detail, sizeof(detail),
                        "first breach at n=%d: |A_%d|=%d, |A_%d|=%d, expected %d +- 1",
                        n, i + 1, s[i].length, i + 2, got, expected);
                    halving_ok = 0;
                }
            }
        }
        if (sum != n) sum_ok = 0;
        if (ns != (int)fmax(1, ceil(log2((double)n)))) count_ok = 0;
        free(s);
    }
    report("|A_{i+1}| = |A_i|/2 +- 1", "Section 2, 'The algorithm'", halving_ok, detail);
    report("subarray lengths sum to n", "Section 2, footnote 1", sum_ok, "");
    report("every subarray is non-empty", "implied by the construction", positive_ok, "");
    report("number of subarrays is ceil(log2 n)", "Section 2, 'The algorithm'", count_ok, "");
}

static void phi_checks(void) {
    // Lemma 1: phi is an injection with phi(i,j) <= O(i j^2)
    const int IMAX = 24, JMAX = 3000;
    double worst_ratio = 0; int wi = 0, wj = 0;
    int injective = 1, nonzero = 1;
    const size_t cap = (size_t)IMAX * JMAX;
    uint64_t* v = malloc(cap * sizeof(uint64_t));
    size_t k = 0;
    for (int i = 1; i <= IMAX; i++)
        for (int j = 1; j <= JMAX; j++) {
            const uint64_t p = elastic_phi((uint64_t)i, (uint64_t)j);
            if (p == 0) nonzero = 0;
            const double ratio = (double)p / ((double)i * (double)j * (double)j);
            if (ratio > worst_ratio) { worst_ratio = ratio; wi = i; wj = j; }
            v[k++] = p;
        }
    // injectivity by sorting
    for (size_t a = 1; a < cap; a++)
        for (size_t b = a; b > 0 && v[b] < v[b - 1]; b--) { uint64_t t = v[b]; v[b] = v[b-1]; v[b-1] = t; }
    for (size_t a = 1; a < cap; a++) if (v[a] == v[a - 1]) injective = 0;
    free(v);
    char d[192];
    snprintf(d, sizeof(d), "worst phi(i,j)/(i j^2) = %.2f at (i=%d, j=%d); the proof gives < 16",
             worst_ratio, wi, wj);
    report("phi is injective", "Lemma 1", injective, "");
    report("phi(i,j) is never zero", "Lemma 1", nonzero, "");
    report("phi(i,j) <= 16 i j^2", "Lemma 1, log2 phi <= log2 i + 2log2 j + O(1)",
           worst_ratio <= 16.0, d);
}

static void elastic_insertion_checks(int capacity, float delta) {
    const int target = capacity - (int)(delta * capacity);
    ElasticHashmap* m = calloc(1, sizeof(ElasticHashmap));
    m->capacity = capacity; m->table = calloc((size_t)capacity, sizeof(Element*));
    Element** els = calloc((size_t)target + 1, sizeof(Element*));
    for (int i = 0; i < target; i++) {
        els[i] = malloc(sizeof(Element));
        char* s = malloc(40); snprintf(s, 40, "key-%d", i);
        els[i]->key = s; els[i]->value = i;
    }
    batch_insert(m, delta, els, (const uint8_t[16]){0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15});

    char d[256];
    snprintf(d, sizeof(d), "n=%d delta=1/%.0f: inserted %d of %d",
             capacity, 1 / (double)delta, m->size, target);
    report("all n - floor(delta n) keys are inserted", "Section 2", m->size == target, d);

    // "the guarantee at the end of the batch Bi is that each Aj satisfying j in
    // {1,...,i} contains exactly |Aj| - floor(delta|Aj|/2) elements". Only the
    // subarray the run stopped inside is exempt: it holds whatever was left.
    {
        ElasticSubArray* sub = partition_elastic_hashmap(capacity);
        const int n_sub = n_elastic_subarrays(capacity);
        int occupancy[64] = {0}, frontier = -1, placed = 0;
        for (int i = 0; i < n_sub && i < 64; i++) {
            for (int q = sub[i].starting_index; q < sub[i].starting_index + sub[i].length; q++)
                if (m->table[q] != NULL) occupancy[i]++;
            placed += occupancy[i];
            if (occupancy[i] > 0) frontier = i;
        }
        int settled_ok = 1, beyond_ok = 1; char od[256] = "";
        for (int i = 0; i < frontier; i++) {
            const int expected = sub[i].length - (int)floor(delta * sub[i].length / 2.0);
            if (occupancy[i] != expected) {
                if (settled_ok) snprintf(od, sizeof(od),
                    "A_%d holds %d, the invariant demands |A_i| - floor(d|A_i|/2) = %d",
                    i + 1, occupancy[i], expected);
                settled_ok = 0;
            }
        }
        for (int i = frontier + 1; i < n_sub && i < 64; i++)
            if (occupancy[i] != 0) beyond_ok = 0;
        if (settled_ok) snprintf(od, sizeof(od),
            "A_1..A_%d at |A_i| - floor(d|A_i|/2); A_%d holds the remaining %d; nothing past it",
            frontier, frontier + 1, occupancy[frontier]);
        report("each settled A_j holds |A_j| - floor(d|A_j|/2)", "Section 2",
               settled_ok && beyond_ok && placed == target, od);
        free(sub);
    }


#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    ElasticSubArray* s = partition_elastic_hashmap(capacity);
    const ElasticHashmapProbeStats st = elastic_hashmap_probe_stats(m);
    const uint64_t expected_b0 = (uint64_t)ceil(0.75 * s[0].length);
    snprintf(d, sizeof(d), "batch zero inserted %llu, expected ceil(0.75*|A_1|) = %llu",
             (unsigned long long)st.batch_zero_insertions, (unsigned long long)expected_b0);
    report("batch B_0 fills A_1 to ceil(0.75|A_1|)", "Section 2", 
           st.batch_zero_insertions == expected_b0, d);

    const uint64_t accounted = st.batch_zero_insertions + st.case_one_insertions +
                               st.case_two_insertions + st.case_three_insertions;
    snprintf(d, sizeof(d), "B0 %llu + case1 %llu + case2 %llu + case3 %llu = %llu, size = %d",
             (unsigned long long)st.batch_zero_insertions, (unsigned long long)st.case_one_insertions,
             (unsigned long long)st.case_two_insertions, (unsigned long long)st.case_three_insertions,
             (unsigned long long)accounted, m->size);
    report("every insertion is accounted to B_0 or a case", "Section 2, three cases",
           accounted == (uint64_t)m->size, d);
    free(s);
#endif
    for (int i = 0; i < capacity; i++) if (m->table[i]) { free((void*)m->table[i]->key); free(m->table[i]); }
    free(m->table); free(m); free(els);
}

// ------------------------------------------------------------------- Funnel
static void funnel_checks(int capacity, double delta) {
    printf("\nFUNNEL HASHING, Section 3   (n=%d, delta=1/%.0f)\n", capacity, 1 / delta);
    const int alpha = funnel_alpha(delta), beta = funnel_beta(delta);
    char d[256];
    snprintf(d, sizeof(d), "alpha = %d, expected ceil(4 log2(1/delta)) + 10 = %d",
             alpha, (int)ceil(4 * -log2(delta)) + 10);
    report("alpha = ceil(4 log2 delta^-1) + 10", "Section 3",
           alpha == (int)ceil(4 * -log2(delta)) + 10, d);
    snprintf(d, sizeof(d), "beta = %d, expected ceil(2 log2(1/delta)) = %d",
             beta, (int)ceil(2 * -log2(delta)));
    report("beta = ceil(2 log2 delta^-1)", "Section 3",
           beta == (int)ceil(2 * -log2(delta)), d);

    FunnelPartition* p = partition_funnel_hashmap(capacity, delta);
    if (p == NULL) {
        report("a valid integer partition exists", "Section 3", 0,
               "partition_funnel_hashmap returned NULL for this (n, delta)");
        return;
    }
    const long lo = (long)ceil(delta * capacity / 2.0);
    const long hi = (long)floor(3.0 * delta * capacity / 4.0);
    snprintf(d, sizeof(d), "|A_(alpha+1)| = %d, allowed range [%ld, %ld]",
             p->a_alpha_plus_one_length, lo, hi);
    report("ceil(dn/2) <= |A_(alpha+1)| <= floor(3dn/4)", "Section 3",
           p->a_alpha_plus_one_length >= lo && p->a_alpha_plus_one_length <= hi, d);

    snprintf(d, sizeof(d), "|A'| = %d, beta = %d, remainder %d",
             p->a_prime_length, beta, p->a_prime_length % beta);
    report("|A'| divisible by beta", "Section 3", p->a_prime_length % beta == 0, d);

    int geo_ok = 1; char gd[256] = "";
    long total_buckets = 0;
    for (int i = 0; i < alpha; i++) {
        total_buckets += p->subarrays[i].n_buckets;
        if (i + 1 < alpha) {
            const int expected = 3 * p->subarrays[i].n_buckets / 4;
            const int got = p->subarrays[i + 1].n_buckets;
            if (got < expected - 1 || got > expected + 1) {
                if (geo_ok) snprintf(gd, sizeof(gd),
                    "first breach at i=%d: a_i=%d, a_(i+1)=%d, expected %d +- 1",
                    i + 1, p->subarrays[i].n_buckets, got, expected);
                geo_ok = 0;
            }
        }
    }
    report("a_(i+1) = 3 a_i / 4 +- 1", "Section 3", geo_ok, gd);
    snprintf(d, sizeof(d), "sum of buckets = %ld, |A'|/beta = %d",
             total_buckets, p->a_prime_length / beta);
    report("the A_i cover A' exactly", "Section 3",
           total_buckets == p->a_prime_length / beta, d);

    const int expected_b = (int)ceil(log2(log2((double)capacity)));
    snprintf(d, sizeof(d), "B probe limit = %d, expected ceil(log2 log2 n) = %d",
             p->b_probe_limit, expected_b);
    report("B makes log log n attempts", "Section 3", p->b_probe_limit == expected_b, d);

    const int expected_c = (int)ceil(2 * log2(log2((double)capacity)));
    snprintf(d, sizeof(d), "C bucket length = %d, expected ceil(2 log2 log2 n) = %d",
             p->c_bucket_length, expected_c);
    report("C buckets hold 2 log log n slots", "Section 3", p->c_bucket_length == expected_c, d);

    // "Note that, for i in [alpha - 10], sum_{j>i} |A_j| > 2.5 |A_i|." This is the
    // inequality the impossibility of case lambda <= alpha-10 rests on.
    int tail_ok = 1; char td[256] = "";
    for (int i = 0; i < alpha - 10; i++) {
        long rest = 0;
        for (int j = i + 1; j < alpha; j++) rest += (long)p->subarrays[j].n_buckets * beta;
        const long here = (long)p->subarrays[i].n_buckets * beta;
        if (!(rest > 5 * here / 2)) {
            if (tail_ok) snprintf(td, sizeof(td),
                "first breach at i=%d: sum_{j>i}|A_j| = %ld, 2.5|A_i| = %.1f",
                i + 1, rest, 2.5 * here);
            tail_ok = 0;
        }
    }
    if (tail_ok) snprintf(td, sizeof(td), "holds for all %d values of i in [alpha-10]", alpha - 10);
    report("sum_{j>i} |A_j| > 2.5 |A_i| for i in [alpha-10]", "Section 3", tail_ok, td);

    // "Since B has size |A_(alpha+1)|/2 >= delta n / 4, its load factor never exceeds 1/2."
    snprintf(d, sizeof(d), "|B| = %d, delta n / 4 = %.1f", p->b.length, delta * capacity / 4.0);
    report("|B| >= delta n / 4", "Section 3", p->b.length >= delta * capacity / 4.0, d);

    // C is a bucket table: the buckets must tile C exactly, or a slot is unreachable.
    // |C| need not divide by the bucket length, so a few buckets carry one extra slot;
    // none may fall below the 2 log log n the overflow argument assumes.
    long c_slots = 0; int min_len = INT_MAX, max_len = 0, contiguous = 1, at = p->c.starting_index;
    for (int i = 0; i < p->c_bucket_count; i++) {
        const int len = p->c_buckets[i].length;
        c_slots += len;
        if (len < min_len) min_len = len;
        if (len > max_len) max_len = len;
        if (p->c_buckets[i].starting_index != at) contiguous = 0;
        at += len;
    }
    snprintf(d, sizeof(d), "%d buckets covering %ld slots, |C| = %d, lengths in [%d, %d], 2 log log n = %d",
             p->c_bucket_count, c_slots, p->c.length, min_len, max_len, p->c_bucket_length);
    report("C's buckets tile C exactly", "Section 3",
           c_slots == p->c.length && contiguous && min_len >= p->c_bucket_length
           && max_len - min_len <= 1, d);

    // A', B and C must partition the table with no gap and no overlap.
    const int seam_ok = p->subarrays[0].starting_index == 0
        && p->b.starting_index == p->a_prime_length
        && p->c.starting_index == p->a_prime_length + p->b.length
        && p->c.starting_index + p->c.length == capacity;
    snprintf(d, sizeof(d), "A' [0,%d), B [%d,%d), C [%d,%d), n = %d",
             p->a_prime_length, p->b.starting_index, p->b.starting_index + p->b.length,
             p->c.starting_index, p->c.starting_index + p->c.length, capacity);
    report("A', B and C partition the table", "Section 3", seam_ok, d);

    snprintf(d, sizeof(d), "|B| = %d, |C| = %d, difference %d",
             p->b.length, p->c.length, abs(p->b.length - p->c.length));
    report("B and C have equal (+-1) length", "Section 3",
           abs(p->b.length - p->c.length) <= 1, d);
    delete_funnel_partition(p);
}

/// Builds a full Funnel table and checks the clauses that only a populated table can settle
static void funnel_run_checks(int capacity, double delta) {
    printf("\nFUNNEL HASHING, a populated table   (n=%d, delta=1/%.0f)\n", capacity, 1 / delta);
    const uint8_t seed[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    const int target = capacity - (int)floor(delta * capacity);
    FunnelHashMap* m = create_funnel_hashmap(capacity);
    FunnelPartition* p = partition_funnel_hashmap(capacity, delta);
    Element** els = calloc((size_t)target + 1, sizeof(Element*));
    for (int i = 0; i < target; i++) {
        els[i] = malloc(sizeof(Element));
        char* k = malloc(40); snprintf(k, 40, "key-%d", i);
        els[i]->key = k; els[i]->value = i;
    }
    char d[256];
    const bool ok = batch_insert_funnel_hashmap(m, delta, els, seed);
    snprintf(d, sizeof(d), "n=%d delta=1/%.0f: inserted %d of %d", capacity, 1 / delta, m->size, target);
    report("every key is placed, no table failure", "Section 3", ok && m->size == target, d);

    // "we perform attempted insertions on each of A_1 ... A_alpha, one after another,
    // stopping upon a successful attempt", each probing up to beta slots, then B's
    // log log n attempts, then at most 2 log log n slot pairs in C. That is a hard
    // deterministic ceiling, not an expectation, so a single breach is a defect.
    int longest_c_bucket = 0;
    for (int i = 0; i < p->c_bucket_count; i++)
        if (p->c_buckets[i].length > longest_c_bucket) longest_c_bucket = p->c_buckets[i].length;
    const uint64_t ceiling = (uint64_t)p->alpha * p->beta + p->b_probe_limit + 2 * longest_c_bucket;
    const FunnelHashmapProbeStats st = funnel_hashmap_probe_stats(m);
    snprintf(d, sizeof(d), "worst insertion used %llu probes, ceiling alpha*beta + loglogn + 2*2loglogn = %llu",
             (unsigned long long)st.maximum_insertion_probes, (unsigned long long)ceiling);
    report("insertion probes <= alpha*beta + f(A_(alpha+1))", "Section 3",
           st.maximum_insertion_probes <= ceiling, d);

    // The two-choice argument needs each bucket filled from its first slot: only then
    // does the a[0],b[0],a[1],b[1] order land the key in the emptier of the two.
    int prefix_ok = 1; char pd[256] = "";
    for (int i = 0; i < p->c_bucket_count; i++) {
        int seen_empty = 0;
        for (int slot = 0; slot < p->c_buckets[i].length; slot++) {
            Element* e = m->table[p->c_buckets[i].starting_index + slot];
            if (e == NULL) seen_empty = 1;
            else if (seen_empty) {
                if (prefix_ok) snprintf(pd, sizeof(pd),
                    "C bucket %d has an occupied slot %d above an empty one", i, slot);
                prefix_ok = 0;
            }
        }
    }
    if (prefix_ok) snprintf(pd, sizeof(pd), "all %d C buckets are filled from slot 0 upward", p->c_bucket_count);
    report("C buckets are prefix filled", "Section 3", prefix_ok, pd);

    // A greedy table must find every key it holds by replaying the insertion order.
    int found = 0;
    for (int i = 0; i < target; i++) {
        char k[40]; snprintf(k, 40, "key-%d", i);
        if (retrieve_element_funnel_hashmap_with_partition(m, p, k, seed).option == Some) found++;
    }
    snprintf(d, sizeof(d), "%d of %d keys retrieved by replaying the probe sequence", found, target);
    report("every inserted key is retrievable", "Section 3", found == target, d);

    // A greedy lookup stops at the first empty slot; absent keys must not be reported present.
    int false_hits = 0;
    for (int i = 0; i < 20000; i++) {
        char k[40]; snprintf(k, 40, "absent-%d", i);
        if (retrieve_element_funnel_hashmap_with_partition(m, p, k, seed).option == Some) false_hits++;
    }
    snprintf(d, sizeof(d), "20000 absent keys, %d reported present", false_hits);
    report("absent keys are reported absent", "Section 3", false_hits == 0, d);

    free(els);
    delete_funnel_partition(p);
    delete_funnel_hashmap(m);
}

int main(void) {
    printf("Conformance of the implementation to the paper's specification\n");
    printf("Farach-Colton, Krapivin, Kuszmaul, arXiv:2501.02305\n");
    printf("=============================================================\n");

    elastic_partition_checks();
    printf("\nELASTIC HASHING, Lemma 1 (the injection phi)\n");
    phi_checks();
    printf("\nELASTIC HASHING, insertion\n");
    elastic_insertion_checks(4096, 0.125f);
    elastic_insertion_checks(16384, 0.03125f);
    elastic_insertion_checks(65536, 0.00390625f);

    funnel_checks(65536, 0.125);
    funnel_checks(65536, 0.03125);
    funnel_checks(262144, 0.00390625);
    // Both of these were unconstructible before C was allowed to absorb its own remainder
    funnel_checks(16384, 0.0078125);
    funnel_checks(131072, 0.125);

    funnel_run_checks(65536, 0.125);
    funnel_run_checks(65536, 0.03125);
    funnel_run_checks(262144, 0.00390625);
    funnel_run_checks(131072, 0.125);

    printf("\n=============================================================\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
