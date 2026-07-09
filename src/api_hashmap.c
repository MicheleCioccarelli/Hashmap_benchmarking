#include "../include/api_hashmap.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 16
#define MAX_LOAD_FACTOR 0.75

typedef struct api_hashmap {
    size_t capacity;
    size_t n_elements;
    // Array holding Element*
    Element** table;
    ApiHashmapProbeStats probe_stats;
} ApiHashmap;

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

// 64-bit FNV-1a. It is simple, stable, and good enough for a baseline map.
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
    hashmap->probe_stats = (ApiHashmapProbeStats){0};

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

static Option_Element_p find_element(const ApiHashmap* hashmap, const char* key, uint64_t* probes) {
    if (hashmap == NULL || key == NULL || hashmap->capacity == 0) {
        return (Option_Element_p){.option = None, .element_p = NULL};
    }

    size_t index = b_hash(hashmap->capacity, key);

    for (size_t checked = 0; checked < hashmap->capacity; checked++) {
        if (probes != NULL) {
            (*probes)++;
        }

        Element* candidate = hashmap->table[index];
        if (candidate == NULL) {
            return (Option_Element_p){.option = None, .element_p = NULL};
        }

        if (strcmp(candidate->key, key) == 0) {
            return (Option_Element_p){.option = Some, .element_p = candidate};
        }

        index = (index + 1) % hashmap->capacity;
    }

    return (Option_Element_p){.option = None, .element_p = NULL};
}

Option_Element_p retrieve_element_api_hashmap(const ApiHashmap* hashmap, const char* key) {
    uint64_t probes = 0;
    Option_Element_p result = find_element(hashmap, key, &probes);

#ifdef HASHMAP_COUNT_PROBES
    if (hashmap != NULL) {
        ((ApiHashmap*)hashmap)->probe_stats.lookup_ops++;
        ((ApiHashmap*)hashmap)->probe_stats.lookup_probes += probes;
    }
#endif

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
    size_t index = b_hash(hashmap->capacity, element->key);

    while (hashmap->table[index] != NULL) {
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

    uint64_t probes = 0;
    (void)probes;
    size_t index = b_hash(hashmap->capacity, key);

    for (size_t checked = 0; checked < hashmap->capacity; checked++) {
        probes++;

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

#ifdef HASHMAP_COUNT_PROBES
            hashmap->probe_stats.insertion_ops++;
            hashmap->probe_stats.insertion_probes += probes;
#endif

            return element;
        }

        if (strcmp(candidate->key, key) == 0) {
            candidate->value = value;

#ifdef HASHMAP_COUNT_PROBES
            hashmap->probe_stats.insertion_ops++;
            hashmap->probe_stats.insertion_probes += probes;
#endif

            return candidate;
        }

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
    if (hashmap != NULL) {
        hashmap->probe_stats = (ApiHashmapProbeStats){0};
    }
}

ApiHashmapProbeStats api_hashmap_probe_stats(const ApiHashmap* hashmap) {
    if (hashmap == NULL) {
        return (ApiHashmapProbeStats){0};
    }

    return hashmap->probe_stats;
}
