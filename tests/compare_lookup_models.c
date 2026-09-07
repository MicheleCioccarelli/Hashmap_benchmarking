// Compares every way of searching an Elastic Hashing table, on ONE shared table.
//
//   usage:  compare_lookup_models <capacity> <delta> [--csv]
//
//   phi-aware        the lookup this project ships: walk k = 1,2,3,... and, when k is in
//                    the image of phi, rebuild the local draw the insertion used
//   image-only       same, but skip the k that encode no (i,j) pair; skipping costs no
//                    probe, and rank(phi) <= phi so every bound in the paper still holds
//   (i,j) j<=|Ai|    what published implementations do: scan levels in order, enumerate
//                    local draws inside each, leave a level at the first empty slot
//   (i,j) j<=8|Ai|   the same with a looser bound on j
//   full-table       blind SipHash(key,k) % capacity, the first thing one tries
//
// Every model counts a probe the same way: one inspection of one physical slot.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "elastic_hashing.h"
#include "hash_functions.h"
#include "api_hashmap.h"

static const uint8_t SEED[SIPHASH_2_4_KEY_SIZE] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

// Faithful copy of the decoder in src/elastic_hashing.c: is k = phi(i,j) for some pair?
static int inverse_phi(uint64_t k, uint64_t* sub, uint64_t* loc) {
    int bl = 0; { uint64_t r = k; while (r) { bl++; r >>= 1; } }
    int cur = bl - 1; uint64_t dp = 0; int dpb = 0;
    while (cur >= 0) {
        if (((k >> cur) & 1) == 0) return 0;
        cur--; if (cur < 0) return 0;
        dp = (dp << 1) | ((k >> cur) & 1); dpb++;
        cur--; if (cur < 0) return 0;
        if (((k >> cur) & 1) == 0) {
            int sb = cur; if (sb <= 0) return 0;
            uint64_t ds = k & ((UINT64_C(1) << sb) - 1);
            int bd = 0, bs = 0;
            { uint64_t r = dp; while (r) { bd++; r >>= 1; } r = ds; while (r) { bs++; r >>= 1; } }
            if (dp == 0 || bd != dpb || ds == 0 || bs != sb) return 0;
            *sub = ds; *loc = dp; return 1;
        }
    }
    return 0;
}

typedef struct { const char* name; double avg; long max; long missed; double seconds; } Result;

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <capacity> <delta> [--csv]\n", argv[0]); return 1; }
    const int capacity = atoi(argv[1]);
    const float delta  = (float)atof(argv[2]);
    const int csv = (argc > 3 && strcmp(argv[3], "--csv") == 0);
    const int nkeys = capacity - (int)(delta * capacity);

    ElasticHashmap* map = calloc(1, sizeof(ElasticHashmap));
    map->capacity = capacity; map->size = 0;
    map->table = calloc((size_t)capacity, sizeof(Element*));
    char (*keys)[40] = malloc(sizeof(char[40]) * (size_t)nkeys);
    Element** els = calloc((size_t)nkeys + 1, sizeof(Element*));
    for (int i = 0; i < nkeys; i++) {
        snprintf(keys[i], 40, "key-%d", i);
        els[i] = malloc(sizeof(Element));
        char* k = malloc(40); memcpy(k, keys[i], 40);
        els[i]->key = k; els[i]->value = i;
    }
    batch_insert(map, delta, els, SEED);
    if (map->size != nkeys) { fprintf(stderr, "insertion failed (%d/%d)\n", map->size, nkeys); return 1; }

    ElasticSubArray* subs = partition_elastic_hashmap(capacity);
    const int nsubs = n_elastic_subarrays(capacity);
    Result r[5]; int nr = 0;

    // ---- 1. phi-aware: read the map's own counters
    {
        clock_t t0 = clock();
        elastic_hashmap_reset_probe_stats(map);
        uint64_t prev = 0, mx = 0; long missed = 0;
        for (int i = 0; i < nkeys; i++) {
            Option_Element_p q = retrieve_element_elastic_hashmap(map, keys[i], SEED);
            if (is_none(q.option)) missed++;
            uint64_t now = elastic_hashmap_probe_stats(map).lookup_probes;
            if (now - prev > mx) mx = now - prev;
            prev = now;
        }
        clock_t t1 = clock();
        r[nr++] = (Result){"phi-aware (shipped)", (double)prev / nkeys, (long)mx, missed,
                           (double)(t1 - t0) / CLOCKS_PER_SEC};
    }
    // ---- 2. image-only
    {
        clock_t t0 = clock();
        double tot = 0; long mx = 0, missed = 0;
        for (int i = 0; i < nkeys; i++) {
            long probes = 0; int found = 0;
            for (uint64_t k = 1; k < (UINT64_C(1) << 40); k++) {
                uint64_t si, lj;
                if (!inverse_phi(k, &si, &lj)) continue;        // no probe
                if (si > (uint64_t)nsubs) continue;
                const int len = subs[si - 1].length;
                const int idx = subs[si - 1].starting_index +
                                (int)(siphash_probe64(keys[i], k, SEED) % (uint64_t)len);
                probes++;
                Element* e = map->table[idx];
                if (e && strcmp(e->key, keys[i]) == 0) { found = 1; break; }
                if (probes > (long)capacity * 8) break;
            }
            if (!found) { missed++; continue; }
            tot += probes; if (probes > mx) mx = probes;
        }
        clock_t t1 = clock();
        r[nr++] = (Result){"image-only (proposed)", tot / (nkeys - missed), mx, missed,
                           (double)(t1 - t0) / CLOCKS_PER_SEC};
    }
    // ---- 3 and 4. the (i,j) scan other implementations use
    const int mults[2] = {1, 8};
    const char* names[2] = {"(i,j) scan, j<=|Ai|", "(i,j) scan, j<=8|Ai|"};
    for (int v = 0; v < 2; v++) {
        clock_t t0 = clock();
        double tot = 0; long mx = 0, missed = 0;
        for (int i = 0; i < nkeys; i++) {
            long probes = 0; int found = 0;
            for (int lev = 0; lev < nsubs && !found; lev++) {
                const int len = subs[lev].length; if (len <= 0) continue;
                const long jmax = (long)len * mults[v];
                for (long j = 1; j <= jmax; j++) {
                    const int idx = subs[lev].starting_index +
                        (int)(siphash_probe64(keys[i], elastic_phi((uint64_t)(lev + 1), (uint64_t)j), SEED)
                              % (uint64_t)len);
                    probes++;
                    Element* e = map->table[idx];
                    if (e == NULL) break;                       // first empty in this level
                    if (strcmp(e->key, keys[i]) == 0) { found = 1; break; }
                }
            }
            if (!found) { missed++; continue; }
            tot += probes; if (probes > mx) mx = probes;
        }
        clock_t t1 = clock();
        r[nr++] = (Result){names[v], tot / (nkeys - missed), mx, missed,
                           (double)(t1 - t0) / CLOCKS_PER_SEC};
    }
    // ---- 5. blind full-table draws
    {
        clock_t t0 = clock();
        double tot = 0; long mx = 0, missed = 0;
        for (int i = 0; i < nkeys; i++) {
            long probes = 0; int found = 0;
            for (long k = 1; k <= 8L * capacity; k++) {
                const int idx = (int)(siphash_probe64(keys[i], (uint64_t)k, SEED) % (uint64_t)capacity);
                probes++;
                Element* e = map->table[idx];
                if (e && strcmp(e->key, keys[i]) == 0) { found = 1; break; }
            }
            if (!found) { missed++; continue; }
            tot += probes; if (probes > mx) mx = probes;
        }
        clock_t t1 = clock();
        r[nr++] = (Result){"full-table SipHash % n", tot / (nkeys - missed), mx, missed,
                           (double)(t1 - t0) / CLOCKS_PER_SEC};
    }

    if (csv) {
        printf("model,capacity,delta,keys,avg_probes,max_probes,missed,seconds\n");
        for (int i = 0; i < nr; i++)
            printf("\"%s\",%d,%.9f,%d,%.4f,%ld,%ld,%.6f\n", r[i].name, capacity,
                   (double)delta, nkeys, r[i].avg, r[i].max, r[i].missed, r[i].seconds);
    } else {
        printf("\nElastic table: n=%d  delta=1/%.0f  keys=%d  subarrays=%d  (c=%g)\n\n",
               capacity, 1 / (double)delta, nkeys, nsubs, elastic_c_constant());
        printf("  %-24s %12s %10s %8s %10s\n", "lookup model", "avg probes", "max", "missed", "time (s)");
        printf("  %-24s %12s %10s %8s %10s\n", "------------------------", "------------",
               "----------", "--------", "----------");
        for (int i = 0; i < nr; i++)
            printf("  %-24s %12.2f %10ld %8ld %10.3f\n", r[i].name, r[i].avg, r[i].max,
                   r[i].missed, r[i].seconds);
        printf("\n  ratio to phi-aware:");
        for (int i = 1; i < nr; i++) printf("  %s = %.2fx", r[i].name, r[i].avg / r[0].avg);
        printf("\n\n");
    }
    return 0;
}
