#include "api_hashmap.h"
#include "elastic_hashing.h"
#include "funnel_hashing.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const uint8_t PROFILE_SEED[SIPHASH_2_4_KEY_SIZE] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

/// Allocates the generated keys shared by construction and repeated lookup
static char** create_keys(const int count) {
    char** keys = calloc((size_t)count, sizeof(char*));
    if (keys == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        char key[64];
        const int written = snprintf(key, sizeof(key), "profile-key-%d", i);
        const size_t length = written < 0 ? 0 : (size_t)written + 1;
        keys[i] = length == 0 || length > sizeof(key) ? NULL : malloc(length);
        if (keys[i] == NULL) {
            for (int j = 0; j < i; j++) {
                free(keys[j]);
            }
            free(keys);
            return NULL;
        }
        memcpy(keys[i], key, length);
    }
    return keys;
}

/// Allocates one experimental Element for every generated key
static Element** create_elements(char** keys, const int count) {
    Element** elements = calloc((size_t)count, sizeof(Element*));
    if (elements == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        elements[i] = malloc(sizeof(Element));
        if (elements[i] == NULL) {
            for (int j = 0; j < i; j++) {
                free(elements[j]);
            }
            free(elements);
            return NULL;
        }
        *elements[i] = (Element){.key = keys[i], .value = i};
    }
    return elements;
}

/// Runs repeated positive lookups on the fixed-capacity control map
static int profile_standard(char** keys, const int count, const int capacity, const int repetitions) {
    ApiHashmap* hashmap = create_fixed_api_hashmap_with_size_and_seed((size_t)capacity, PROFILE_SEED);
    if (hashmap == NULL) {
        return 1;
    }
    for (int i = 0; i < count; i++) {
        if (insert_element_api_hashmap(hashmap, keys[i], i) == NULL) {
            destroy_api_hashmap(hashmap);
            return 1;
        }
    }

    unsigned long long checksum = 0;
    const clock_t start = clock();
    for (int repetition = 0; repetition < repetitions; repetition++) {
        for (int i = 0; i < count; i++) {
            const Option_Element_p result = retrieve_element_api_hashmap(hashmap, keys[i]);
            if (is_none(result.option)) {
                destroy_api_hashmap(hashmap);
                return 1;
            }
            checksum += (unsigned long long)result.element_p->value;
        }
    }
    const clock_t end = clock();
    printf("checksum=%llu cpu_seconds=%.6f\n", checksum, (double)(end - start) / CLOCKS_PER_SEC);
    destroy_api_hashmap(hashmap);
    return 0;
}

/// Runs repeated positive lookups on one already populated Elastic map
static int profile_elastic(char** keys, const int count, const int capacity, const float delta, const int repetitions) {
    ElasticHashmap* hashmap = calloc(1, sizeof(ElasticHashmap));
    Element** elements = create_elements(keys, count);
    if (hashmap == NULL || elements == NULL) {
        free(hashmap);
        free(elements);
        return 1;
    }
    *hashmap = (ElasticHashmap){.capacity = capacity, .table = calloc((size_t)capacity, sizeof(Element*))};
    if (hashmap->table == NULL) {
        free(elements);
        free(hashmap);
        return 1;
    }
    batch_insert(hashmap, delta, elements, PROFILE_SEED);
    free(elements);
    if (hashmap->size != count) {
        delete_elastic_hashmap(hashmap);
        return 1;
    }

    unsigned long long checksum = 0;
    const clock_t start = clock();
    for (int repetition = 0; repetition < repetitions; repetition++) {
        for (int i = 0; i < count; i++) {
            const Option_Element_p result = retrieve_element_elastic_hashmap(hashmap, keys[i], PROFILE_SEED);
            if (is_none(result.option)) {
                delete_elastic_hashmap(hashmap);
                return 1;
            }
            checksum += (unsigned long long)result.element_p->value;
        }
    }
    const clock_t end = clock();
    printf("checksum=%llu cpu_seconds=%.6f\n", checksum, (double)(end - start) / CLOCKS_PER_SEC);
    delete_elastic_hashmap(hashmap);
    return 0;
}

/// Runs repeated positive lookups on one already populated Funnel map
static int profile_funnel(char** keys, const int count, const int capacity, const double delta, const int repetitions) {
    FunnelHashMap* hashmap = create_funnel_hashmap(capacity);
    Element** elements = create_elements(keys, count);
    if (hashmap == NULL || elements == NULL) {
        delete_funnel_hashmap(hashmap);
        free(elements);
        return 1;
    }
    if (!batch_insert_funnel_hashmap(hashmap, delta, elements, PROFILE_SEED)) {
        free(elements);
        delete_funnel_hashmap(hashmap);
        return 1;
    }
    free(elements);
    FunnelPartition* partition = partition_funnel_hashmap(capacity, delta);
    if (partition == NULL) {
        delete_funnel_hashmap(hashmap);
        return 1;
    }

    unsigned long long checksum = 0;
    const clock_t start = clock();
    for (int repetition = 0; repetition < repetitions; repetition++) {
        for (int i = 0; i < count; i++) {
            const Option_Element_p result = retrieve_element_funnel_hashmap_with_partition(hashmap, partition, keys[i], PROFILE_SEED);
            if (is_none(result.option)) {
                delete_funnel_partition(partition);
                delete_funnel_hashmap(hashmap);
                return 1;
            }
            checksum += (unsigned long long)result.element_p->value;
        }
    }
    const clock_t end = clock();
    printf("checksum=%llu cpu_seconds=%.6f\n", checksum, (double)(end - start) / CLOCKS_PER_SEC);
    delete_funnel_partition(partition);
    delete_funnel_hashmap(hashmap);
    return 0;
}

int main(const int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <standard|elastic|funnel> <repetitions>\n", argv[0]);
        return 1;
    }

    const int capacity = 4096;
    const float delta = 0.125f;
    const int count = capacity - (int)floor((double)delta * capacity);
    const int repetitions = atoi(argv[2]);
    char** keys = repetitions > 0 ? create_keys(count) : NULL;
    if (keys == NULL) {
        return 1;
    }

    printf("pid=%d mode=%s lookups=%d\n", getpid(), argv[1], count * repetitions);
    fflush(stdout);
    sleep(1);

    int result = 1;
    if (strcmp(argv[1], "standard") == 0) {
        result = profile_standard(keys, count, capacity, repetitions);
        for (int i = 0; i < count; i++) {
            free(keys[i]);
        }
    } else if (strcmp(argv[1], "elastic") == 0) {
        result = profile_elastic(keys, count, capacity, delta, repetitions);
    } else if (strcmp(argv[1], "funnel") == 0) {
        result = profile_funnel(keys, count, capacity, delta, repetitions);
    }
    free(keys);
    return result;
}
