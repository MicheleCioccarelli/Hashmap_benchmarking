#include "../include/api_hashmap.h"

#include <stdlib.h>
#include <string.h>

#include "hash_functions.h"

#define INITIAL_CAPACITY 16
#define MAX_LOAD_FACTOR 0.75


typedef struct api_hashmap {
    size_t capacity;
    size_t n_elements;
    bool fixed_capacity;
    uint8_t seed[SIPHASH_2_4_KEY_SIZE];
    // Array holding Element*
    Element** table;
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    ApiHashmapProbeStats probe_stats;
    uint64_t current_insertion_probes;
    uint64_t current_lookup_probes;
#endif
} ApiHashmap;

static const uint8_t API_HASHMAP_DEFAULT_SEED[SIPHASH_2_4_KEY_SIZE] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
#define RECORD_INSERT_OP(hashmap) do { (hashmap)->probe_stats.insertion_ops++; (hashmap)->current_insertion_probes = 0; } while (0)
#define RECORD_INSERT_PROBE(hashmap) do { (hashmap)->probe_stats.insertion_probes++; (hashmap)->current_insertion_probes++; } while (0)
#define FINISH_INSERT_OP(hashmap) do { if ((hashmap)->current_insertion_probes > (hashmap)->probe_stats.maximum_insertion_probes) { (hashmap)->probe_stats.maximum_insertion_probes = (hashmap)->current_insertion_probes; } } while (0)
#define RECORD_LOOKUP_OP(hashmap) do { ((ApiHashmap*)(hashmap))->probe_stats.lookup_ops++; ((ApiHashmap*)(hashmap))->current_lookup_probes = 0; } while (0)
#define RECORD_LOOKUP_PROBE(hashmap) do { ((ApiHashmap*)(hashmap))->probe_stats.lookup_probes++; ((ApiHashmap*)(hashmap))->current_lookup_probes++; } while (0)
#define FINISH_LOOKUP_OP(hashmap) do { if (((ApiHashmap*)(hashmap))->current_lookup_probes > ((ApiHashmap*)(hashmap))->probe_stats.maximum_lookup_probes) { ((ApiHashmap*)(hashmap))->probe_stats.maximum_lookup_probes = ((ApiHashmap*)(hashmap))->current_lookup_probes; } } while (0)
#else
#define RECORD_INSERT_OP(hashmap) ((void)0)
#define RECORD_INSERT_PROBE(hashmap) ((void)0)
#define FINISH_INSERT_OP(hashmap) ((void)0)
#define RECORD_LOOKUP_OP(hashmap) ((void)0)
#define RECORD_LOOKUP_PROBE(hashmap) ((void)0)
#define FINISH_LOOKUP_OP(hashmap) ((void)0)
#endif

bool is_some(const Option op) {
    return op == Some;
}

bool is_none(const Option op) {
    return op == None;
}

static char* copy_string(const char* source) {
    const size_t length = strlen(source) + 1;
    char* copy = malloc(length);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, source, length);
    return copy;
}

/// Uses the same SipHash draw as the first full-table probe in the experimental maps
static size_t b_hash_seeded(const size_t table_size, const char* key, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    if (table_size == 0 || key == NULL || seed == NULL) {
        return 0;
    }

    return (size_t)(siphash_probe64(key, 1, seed) % (uint64_t)table_size);
}

size_t b_hash(const size_t tableSize, const char* key) {
    return b_hash_seeded(tableSize, key, API_HASHMAP_DEFAULT_SEED);
}

static ApiHashmap* create_api_hashmap_configured(const size_t tableSize, const uint8_t seed[SIPHASH_2_4_KEY_SIZE], const bool fixed_capacity) {
    if (tableSize == 0 || seed == NULL) {
        return NULL;
    }

    ApiHashmap* hashmap = malloc(sizeof(ApiHashmap));
    if (hashmap == NULL) {
        return NULL;
    }

    hashmap->n_elements = 0;
    hashmap->capacity = tableSize;
    hashmap->fixed_capacity = fixed_capacity;
    memcpy(hashmap->seed, seed, sizeof(hashmap->seed));
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    hashmap->probe_stats = (ApiHashmapProbeStats){0};
    hashmap->current_insertion_probes = 0;
    hashmap->current_lookup_probes = 0;
#endif

    hashmap->table = calloc(hashmap->capacity, sizeof(Element*));
    if (hashmap->table == NULL) {
        free(hashmap);
        return NULL;
    }
    return hashmap;
}

ApiHashmap* create_api_hashmap_with_size_and_seed(const size_t tableSize, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    return create_api_hashmap_configured(tableSize, seed, false);
}

ApiHashmap* create_fixed_api_hashmap_with_size(const size_t tableSize) {
    return create_api_hashmap_configured(tableSize, API_HASHMAP_DEFAULT_SEED, true);
}

ApiHashmap* create_fixed_api_hashmap_with_size_and_seed(const size_t tableSize, const uint8_t seed[SIPHASH_2_4_KEY_SIZE]) {
    return create_api_hashmap_configured(tableSize, seed, true);
}

ApiHashmap* create_api_hashmap_with_size(const size_t tableSize) {
    return create_api_hashmap_with_size_and_seed(tableSize, API_HASHMAP_DEFAULT_SEED);
}

ApiHashmap* create_api_hashmap(void) {
    return create_api_hashmap_with_size(INITIAL_CAPACITY);
}

void destroy_api_hashmap(ApiHashmap* hashmap) {
    if (hashmap != NULL) {
        for (size_t i = 0; i < hashmap->capacity; i++) {
            if (hashmap->table[i] != NULL) {
                free((char*)hashmap->table[i]->key);
                free(hashmap->table[i]);
            }
        }
        free(hashmap->table);
        free(hashmap);
    }
}

static Option_Element_p find_element(const ApiHashmap* hashmap, const char* key) {
    if (hashmap == NULL || key == NULL || hashmap->capacity == 0) {
        return (Option_Element_p){.option = None, .element_p = NULL};
    }

    size_t index = b_hash_seeded(hashmap->capacity, key, hashmap->seed);

    for (size_t checked = 0; checked < hashmap->capacity; checked++) {
        RECORD_LOOKUP_PROBE(hashmap);

        Element* candidate = hashmap->table[index];
        if (candidate == NULL) {
            // Stop at the first empty slot because deletion is not implemented
            return (Option_Element_p){.option = None, .element_p = NULL};
        }

        if (strcmp(candidate->key, key) == 0) {
            return (Option_Element_p){.option = Some, .element_p = candidate};
        }

        // Linear probing moves to the next slot and wraps at the end
        index = (index + 1) % hashmap->capacity;
    }

    return (Option_Element_p){.option = None, .element_p = NULL};
}

Option_Element_p retrieve_element_api_hashmap(const ApiHashmap* hashmap, const char* key) {
    if (hashmap != NULL) {
        RECORD_LOOKUP_OP(hashmap);
    }

    const Option_Element_p result = find_element(hashmap, key);
    if (hashmap != NULL) {
        FINISH_LOOKUP_OP(hashmap);
    }
    return result;
}

Option_Element_v retrieve_element_value_api_hashmap(const ApiHashmap* hashmap, const char* key) {
    const Option_Element_p result = retrieve_element_api_hashmap(hashmap, key);
    if (is_none(result.option)) {
        return (Option_Element_v){.option = None, .element_v = {0}};
    }

    return (Option_Element_v){.option = Some, .element_v = *result.element_p};
}

static bool insert_existing_element(ApiHashmap* hashmap, Element* element) {
    size_t index = b_hash_seeded(hashmap->capacity, element->key, hashmap->seed);

    while (hashmap->table[index] != NULL) {
        // Existing elements are only moved during resize, so there cannot be duplicates here
        index = (index + 1) % hashmap->capacity;
    }

    hashmap->table[index] = element;
    hashmap->n_elements++;
    return true;
}

bool resize_api_hashmap(ApiHashmap* hashmap) {
    if (hashmap == NULL || hashmap->fixed_capacity || hashmap->capacity > SIZE_MAX / 2) {
        return false;
    }

    const size_t old_capacity = hashmap->capacity;
    Element** old_table = hashmap->table;

    // Indices depend on capacity, so every existing element must be inserted again
    hashmap->capacity *= 2;
    hashmap->n_elements = 0;
    hashmap->table = calloc(hashmap->capacity, sizeof(Element*));

    if (hashmap->table == NULL) {
        hashmap->capacity = old_capacity;
        hashmap->n_elements = 0;
        hashmap->table = old_table;
        for (size_t i = 0; i < old_capacity; i++) {
            if (old_table[i] != NULL) {
                hashmap->n_elements++;
            }
        }
        return false;
    }

    for (size_t i = 0; i < old_capacity; i++) {
        if (old_table[i] != NULL) {
            insert_existing_element(hashmap, old_table[i]);
        }
    }

    free(old_table);
    return true;
}

Element* insert_element_api_hashmap(ApiHashmap* hashmap, const char* key, const int value) {
    if (hashmap == NULL || key == NULL) {
        return NULL;
    }

    RECORD_INSERT_OP(hashmap);
    size_t index = b_hash_seeded(hashmap->capacity, key, hashmap->seed);
    bool found_empty_slot = false;

    for (size_t checked = 0; checked < hashmap->capacity; checked++) {
        RECORD_INSERT_PROBE(hashmap);

        Element* candidate = hashmap->table[index];
        if (candidate == NULL) {
            found_empty_slot = true;
            break;
        }

        if (strcmp(candidate->key, key) == 0) {
            candidate->value = value;
            FINISH_INSERT_OP(hashmap);
            return candidate;
        }

        // Linear probing keeps the table compact but creates clusters at high load
        index = (index + 1) % hashmap->capacity;
    }

    // Search for an existing key before resizing because an update does not increase the load factor
    if (!hashmap->fixed_capacity && (double)(hashmap->n_elements + 1) / (double)hashmap->capacity > MAX_LOAD_FACTOR) {
        if (!resize_api_hashmap(hashmap)) {
            FINISH_INSERT_OP(hashmap);
            return NULL;
        }

        index = b_hash_seeded(hashmap->capacity, key, hashmap->seed);
        found_empty_slot = false;
        for (size_t checked = 0; checked < hashmap->capacity; checked++) {
            RECORD_INSERT_PROBE(hashmap);
            if (hashmap->table[index] == NULL) {
                found_empty_slot = true;
                break;
            }
            index = (index + 1) % hashmap->capacity;
        }
    }

    if (!found_empty_slot) {
        FINISH_INSERT_OP(hashmap);
        return NULL;
    }

    Element* element = malloc(sizeof(Element));
    if (element == NULL) {
        FINISH_INSERT_OP(hashmap);
        return NULL;
    }

    char* stored_key = copy_string(key);
    if (stored_key == NULL) {
        free(element);
        FINISH_INSERT_OP(hashmap);
        return NULL;
    }

    element->key = stored_key;
    element->value = value;
    hashmap->table[index] = element;
    hashmap->n_elements++;
    FINISH_INSERT_OP(hashmap);
    return element;
}

size_t api_hashmap_size(const ApiHashmap* hashmap) {
    return hashmap == NULL ? 0 : hashmap->n_elements;
}

size_t api_hashmap_capacity(const ApiHashmap* hashmap) {
    return hashmap == NULL ? 0 : hashmap->capacity;
}

void api_hashmap_reset_probe_stats(ApiHashmap* hashmap) {
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    if (hashmap != NULL) {
        hashmap->probe_stats = (ApiHashmapProbeStats){0};
        hashmap->current_insertion_probes = 0;
        hashmap->current_lookup_probes = 0;
    }
#else
    (void)hashmap;
#endif
}

ApiHashmapProbeStats api_hashmap_probe_stats(const ApiHashmap* hashmap) {
#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    if (hashmap == NULL) {
        return (ApiHashmapProbeStats){0};
    }

    return hashmap->probe_stats;
#else
    (void)hashmap;
    return (ApiHashmapProbeStats){0};
#endif
}
