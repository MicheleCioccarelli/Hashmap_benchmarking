#include "api_hashmap.h"
#include "elastic_hashing.h"
#include "funnel_hashing.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const uint8_t PROFILE_SEED[SIPHASH_2_4_KEY_SIZE] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

/// Allocates one independently owned element for every insertion
static Element** create_elements(const int count, const int repetition) {
    Element** elements = calloc((size_t)count, sizeof(Element*));
    if (elements == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        char generated_key[64];
        const int written = snprintf(generated_key, sizeof(generated_key), "insertion-%d-%d", repetition, i);
        const size_t key_length = written < 0 ? 0 : (size_t)written + 1;
        elements[i] = malloc(sizeof(Element));
        char* key = key_length == 0 || key_length > sizeof(generated_key) ? NULL : malloc(key_length);
        if (elements[i] == NULL || key == NULL) {
            free(elements[i]);
            free(key);
            for (int j = 0; j < i; j++) {
                free((char*)elements[j]->key);
                free(elements[j]);
            }
            free(elements);
            return NULL;
        }
        memcpy(key, generated_key, key_length);
        *elements[i] = (Element){.key = key, .value = i};
    }
    return elements;
}

/// Frees elements which were not transferred to a hashmap
static void delete_elements(Element** elements, const int count) {
    if (elements == NULL) {
        return;
    }
    for (int i = 0; i < count; i++) {
        if (elements[i] != NULL) {
            free((char*)elements[i]->key);
            free(elements[i]);
        }
    }
    free(elements);
}

/// Profiles fixed-capacity control insertions without timing input allocation
static int profile_standard(const int capacity, const int count, const int repetitions, clock_t* elapsed) {
    for (int repetition = 0; repetition < repetitions; repetition++) {
        Element** elements = create_elements(count, repetition);
        ApiHashmap* hashmap = create_fixed_api_hashmap_with_size_and_seed((size_t)capacity, PROFILE_SEED);
        if (elements == NULL || hashmap == NULL) {
            delete_elements(elements, count);
            destroy_api_hashmap(hashmap);
            return 1;
        }

        const clock_t start = clock();
        for (int i = 0; i < count; i++) {
            if (insert_element_api_hashmap(hashmap, elements[i]->key, elements[i]->value) == NULL) {
                delete_elements(elements, count);
                destroy_api_hashmap(hashmap);
                return 1;
            }
        }
        const clock_t end = clock();
        *elapsed += end - start;
        delete_elements(elements, count);
        destroy_api_hashmap(hashmap);
    }
    return 0;
}

/// Profiles Elastic batch insertions without timing input allocation
static int profile_elastic(const int capacity, const int count, const float delta, const int repetitions, clock_t* elapsed) {
    for (int repetition = 0; repetition < repetitions; repetition++) {
        Element** elements = create_elements(count, repetition);
        ElasticHashmap* hashmap = calloc(1, sizeof(ElasticHashmap));
        if (elements == NULL || hashmap == NULL) {
            delete_elements(elements, count);
            free(hashmap);
            return 1;
        }
        *hashmap = (ElasticHashmap){.capacity = capacity, .table = calloc((size_t)capacity, sizeof(Element*))};
        if (hashmap->table == NULL) {
            delete_elements(elements, count);
            free(hashmap);
            return 1;
        }

        const clock_t start = clock();
        batch_insert(hashmap, delta, elements, PROFILE_SEED);
        const clock_t end = clock();
        *elapsed += end - start;
        const int inserted = hashmap->size;
        delete_elements(elements, count);
        delete_elastic_hashmap(hashmap);
        if (inserted != count) {
            return 1;
        }
    }
    return 0;
}

/// Profiles Funnel batch insertions without timing input allocation
static int profile_funnel(const int capacity, const int count, const double delta, const int repetitions, clock_t* elapsed) {
    for (int repetition = 0; repetition < repetitions; repetition++) {
        Element** elements = create_elements(count, repetition);
        FunnelHashMap* hashmap = create_funnel_hashmap(capacity);
        if (elements == NULL || hashmap == NULL) {
            delete_elements(elements, count);
            delete_funnel_hashmap(hashmap);
            return 1;
        }

        const clock_t start = clock();
        const bool inserted = batch_insert_funnel_hashmap(hashmap, delta, elements, PROFILE_SEED);
        const clock_t end = clock();
        *elapsed += end - start;
        delete_elements(elements, count);
        delete_funnel_hashmap(hashmap);
        if (!inserted) {
            return 1;
        }
    }
    return 0;
}

int main(const int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <standard|elastic|funnel> <repetitions>\n", argv[0]);
        return 1;
    }

    const int capacity = 65536;
    const float delta = 0.125f;
    const int count = capacity - (int)floor((double)delta * (double)capacity);
    const int repetitions = atoi(argv[2]);
    clock_t elapsed = 0;
    int result = 1;
    if (repetitions <= 0) {
        return 1;
    }
    if (strcmp(argv[1], "standard") == 0) {
        result = profile_standard(capacity, count, repetitions, &elapsed);
    } else if (strcmp(argv[1], "elastic") == 0) {
        result = profile_elastic(capacity, count, delta, repetitions, &elapsed);
    } else if (strcmp(argv[1], "funnel") == 0) {
        result = profile_funnel(capacity, count, (double)delta, repetitions, &elapsed);
    }
    if (result == 0) {
        printf("mode=%s insertions=%d cpu_seconds=%.6f\n", argv[1], count * repetitions, (double)elapsed / CLOCKS_PER_SEC);
    }
    return result;
}
