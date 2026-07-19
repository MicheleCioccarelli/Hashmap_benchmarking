#include "../include/api_hashmap.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 16
#define MAX_LOAD_FACTOR 0.75


#define HASHMAP_COUNT_PROBES 1

typedef struct api_hashmap {
    size_t capacity;
    size_t n_elements;
    // Array holding Element*
    Element** table;
#ifdef HASHMAP_COUNT_PROBES
    ApiHashmapProbeStats probe_stats;
#endif
} ApiHashmap;

#ifdef HASHMAP_COUNT_PROBES
#define RECORD_INSERT_OP(hashmap) ((hashmap)->probe_stats.insertion_ops++)
#define RECORD_INSERT_PROBE(hashmap) ((hashmap)->probe_stats.insertion_probes++)
#define RECORD_LOOKUP_OP(hashmap) (((ApiHashmap*)(hashmap))->probe_stats.lookup_ops++)
#define RECORD_LOOKUP_PROBE(hashmap) (((ApiHashmap*)(hashmap))->probe_stats.lookup_probes++)
#else
#define RECORD_INSERT_OP(hashmap) ((void)0)
#define RECORD_INSERT_PROBE(hashmap) ((void)0)
#define RECORD_LOOKUP_OP(hashmap) ((void)0)
#define RECORD_LOOKUP_PROBE(hashmap) ((void)0)
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

// 64-bit FNV-1a, simple and stable enough for a baseline map
size_t b_hash(const size_t tableSize, const char* key) {
    if (tableSize == 0 || key == NULL) {
        return 0;
    }

    uint64_t hash = 14695981039346656037ULL;

    for (const unsigned char* c = (const unsigned char*)key; *c != '\0'; c++) {
        hash ^= *c;
        hash *= 1099511628211ULL;
    }

    return (size_t)(hash % tableSize);
}

ApiHashmap* create_api_hashmap_with_size(const size_t tableSize) {
    if (tableSize == 0) {
        return NULL;
    }

    ApiHashmap* hashmap = malloc(sizeof(ApiHashmap));
    if (hashmap == NULL) {
        return NULL;
    }

    hashmap->n_elements = 0;
    hashmap->capacity = tableSize;
#ifdef HASHMAP_COUNT_PROBES
    hashmap->probe_stats = (ApiHashmapProbeStats){0};
#endif

    hashmap->table = calloc(hashmap->capacity, sizeof(Element*));
    if (hashmap->table == NULL) {
        free(hashmap);
        return NULL;
    }
    return hashmap;
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

    size_t index = b_hash(hashmap->capacity, key);

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

    return find_element(hashmap, key);
}

Option_Element_v retrieve_element_value_api_hashmap(const ApiHashmap* hashmap, const char* key) {
    const Option_Element_p result = retrieve_element_api_hashmap(hashmap, key);
    if (is_none(result.option)) {
        return (Option_Element_v){.option = None, .element_v = {0}};
    }

    return (Option_Element_v){.option = Some, .element_v = *result.element_p};
}

static bool insert_existing_element(ApiHashmap* hashmap, Element* element) {
    size_t index = b_hash(hashmap->capacity, element->key);

    while (hashmap->table[index] != NULL) {
        // Existing elements are only moved during resize, so there cannot be duplicates here
        index = (index + 1) % hashmap->capacity;
    }

    hashmap->table[index] = element;
    hashmap->n_elements++;
    return true;
}

bool resize_api_hashmap(ApiHashmap* hashmap) {
    if (hashmap == NULL || hashmap->capacity > SIZE_MAX / 2) {
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

    if ((double)(hashmap->n_elements + 1) / (double)hashmap->capacity > MAX_LOAD_FACTOR) {
        if (!resize_api_hashmap(hashmap)) {
            return NULL;
        }
    }

    size_t index = b_hash(hashmap->capacity, key);
    RECORD_INSERT_OP(hashmap);

    for (size_t checked = 0; checked < hashmap->capacity; checked++) {
        RECORD_INSERT_PROBE(hashmap);

        Element* candidate = hashmap->table[index];
        if (candidate == NULL) {
            Element* element = malloc(sizeof(Element));
            if (element == NULL) {
                return NULL;
            }

            char* stored_key = copy_string(key);
            if (stored_key == NULL) {
                free(element);
                return NULL;
            }

            element->key = stored_key;
            element->value = value;
            hashmap->table[index] = element;
            hashmap->n_elements++;

            return element;
        }

        if (strcmp(candidate->key, key) == 0) {
            candidate->value = value;

            return candidate;
        }

        // Linear probing keeps the table compact but creates clusters at high load
        index = (index + 1) % hashmap->capacity;
    }

    return NULL;
}

size_t api_hashmap_size(const ApiHashmap* hashmap) {
    return hashmap == NULL ? 0 : hashmap->n_elements;
}

size_t api_hashmap_capacity(const ApiHashmap* hashmap) {
    return hashmap == NULL ? 0 : hashmap->capacity;
}

void api_hashmap_reset_probe_stats(ApiHashmap* hashmap) {
#ifdef HASHMAP_COUNT_PROBES
    if (hashmap != NULL) {
        hashmap->probe_stats = (ApiHashmapProbeStats){0};
    }
#else
    (void)hashmap;
#endif
}

ApiHashmapProbeStats api_hashmap_probe_stats(const ApiHashmap* hashmap) {
#ifdef HASHMAP_COUNT_PROBES
    if (hashmap == NULL) {
        return (ApiHashmapProbeStats){0};
    }

    return hashmap->probe_stats;
#else
    (void)hashmap;
    return (ApiHashmapProbeStats){0};
#endif
}
