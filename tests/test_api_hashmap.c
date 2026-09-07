#include "api_hashmap.h"

#include <stdio.h>
#include <string.h>

#define EXPECT(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "test failed: %s at %s:%d\n", #condition, __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

static int test_create_insert_update_lookup(void) {
    ApiHashmap* hashmap = create_api_hashmap_with_size(4);
    EXPECT(hashmap != NULL);

    Element* alice = insert_element_api_hashmap(hashmap, "alice", 1);
    EXPECT(alice != NULL);
    EXPECT(strcmp(alice->key, "alice") == 0);
    EXPECT(alice->value == 1);
    EXPECT(api_hashmap_size(hashmap) == 1);

    Element* updated = insert_element_api_hashmap(hashmap, "alice", 7);
    EXPECT(updated == alice);
    EXPECT(updated->value == 7);
    EXPECT(api_hashmap_size(hashmap) == 1);

    Option_Element_p retrieved = retrieve_element_api_hashmap(hashmap, "alice");
    EXPECT(is_some(retrieved.option));
    EXPECT(retrieved.element_p == alice);
    EXPECT(retrieved.element_p->value == 7);

    Option_Element_v copy = retrieve_element_value_api_hashmap(hashmap, "alice");
    EXPECT(is_some(copy.option));
    EXPECT(copy.element_v.value == 7);
    EXPECT(strcmp(copy.element_v.key, "alice") == 0);

    Option_Element_p missing = retrieve_element_api_hashmap(hashmap, "missing");
    EXPECT(is_none(missing.option));
    EXPECT(missing.element_p == NULL);

    destroy_api_hashmap(hashmap);
    return 0;
}

static int test_resize_keeps_elements(void) {
    ApiHashmap* hashmap = create_api_hashmap_with_size(4);
    EXPECT(hashmap != NULL);

    char key[32];
    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "word-%d", i);
        EXPECT(insert_element_api_hashmap(hashmap, key, i * 3) != NULL);
    }

    EXPECT(api_hashmap_size(hashmap) == 100);
    EXPECT(api_hashmap_capacity(hashmap) >= 128);

    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "word-%d", i);
        Option_Element_p result = retrieve_element_api_hashmap(hashmap, key);
        EXPECT(is_some(result.option));
        EXPECT(result.element_p->value == i * 3);
    }

    destroy_api_hashmap(hashmap);
    return 0;
}

static int test_update_does_not_resize(void) {
    ApiHashmap* hashmap = create_api_hashmap_with_size(4);
    EXPECT(hashmap != NULL);

    EXPECT(insert_element_api_hashmap(hashmap, "one", 1) != NULL);
    EXPECT(insert_element_api_hashmap(hashmap, "two", 2) != NULL);
    EXPECT(insert_element_api_hashmap(hashmap, "three", 3) != NULL);
    EXPECT(api_hashmap_size(hashmap) == 3);
    EXPECT(api_hashmap_capacity(hashmap) == 4);

    EXPECT(insert_element_api_hashmap(hashmap, "one", 11) != NULL);
    EXPECT(api_hashmap_size(hashmap) == 3);
    EXPECT(api_hashmap_capacity(hashmap) == 4);
    EXPECT(retrieve_element_api_hashmap(hashmap, "one").element_p->value == 11);

    destroy_api_hashmap(hashmap);
    return 0;
}

static int test_fixed_capacity(void) {
    ApiHashmap* hashmap = create_fixed_api_hashmap_with_size(4);
    EXPECT(hashmap != NULL);

    EXPECT(insert_element_api_hashmap(hashmap, "fixed-0", 0) != NULL);
    EXPECT(insert_element_api_hashmap(hashmap, "fixed-1", 1) != NULL);
    EXPECT(insert_element_api_hashmap(hashmap, "fixed-2", 2) != NULL);
    EXPECT(insert_element_api_hashmap(hashmap, "fixed-3", 3) != NULL);
    EXPECT(api_hashmap_capacity(hashmap) == 4);
    EXPECT(insert_element_api_hashmap(hashmap, "fixed-4", 4) == NULL);
    EXPECT(resize_api_hashmap(hashmap) == false);
    EXPECT(insert_element_api_hashmap(hashmap, "fixed-2", 22) != NULL);
    EXPECT(retrieve_element_api_hashmap(hashmap, "fixed-2").element_p->value == 22);

    destroy_api_hashmap(hashmap);
    return 0;
}

static int test_invalid_inputs(void) {
    EXPECT(create_api_hashmap_with_size(0) == NULL);
    EXPECT(insert_element_api_hashmap(NULL, "key", 1) == NULL);
    EXPECT(insert_element_api_hashmap(create_api_hashmap_with_size(0), "key", 1) == NULL);
    EXPECT(is_none(retrieve_element_api_hashmap(NULL, "key").option));
    EXPECT(is_none(retrieve_element_api_hashmap(create_api_hashmap_with_size(0), "key").option));
    EXPECT(b_hash(0, "key") == 0);
    EXPECT(b_hash(8, NULL) == 0);

    return 0;
}

static int test_probe_stats(void) {
    ApiHashmap* hashmap = create_api_hashmap_with_size(8);
    EXPECT(hashmap != NULL);

    api_hashmap_reset_probe_stats(hashmap);
    EXPECT(insert_element_api_hashmap(hashmap, "alpha", 1) != NULL);
    EXPECT(insert_element_api_hashmap(hashmap, "beta", 2) != NULL);
    EXPECT(is_some(retrieve_element_api_hashmap(hashmap, "alpha").option));
    EXPECT(is_none(retrieve_element_api_hashmap(hashmap, "gamma").option));

    ApiHashmapProbeStats stats = api_hashmap_probe_stats(hashmap);

#if defined(HASHMAP_COUNT_PROBES) && HASHMAP_COUNT_PROBES
    EXPECT(stats.insertion_ops == 2);
    EXPECT(stats.insertion_probes >= stats.insertion_ops);
    EXPECT(stats.maximum_insertion_probes >= 1);
    EXPECT(stats.lookup_ops == 2);
    EXPECT(stats.lookup_probes >= stats.lookup_ops);
    EXPECT(stats.maximum_lookup_probes >= 1);

    api_hashmap_reset_probe_stats(hashmap);
    stats = api_hashmap_probe_stats(hashmap);
    EXPECT(stats.insertion_ops == 0);
    EXPECT(stats.insertion_probes == 0);
    EXPECT(stats.maximum_insertion_probes == 0);
    EXPECT(stats.lookup_ops == 0);
    EXPECT(stats.lookup_probes == 0);
    EXPECT(stats.maximum_lookup_probes == 0);
#else
    EXPECT(stats.insertion_ops == 0);
    EXPECT(stats.insertion_probes == 0);
    EXPECT(stats.maximum_insertion_probes == 0);
    EXPECT(stats.lookup_ops == 0);
    EXPECT(stats.lookup_probes == 0);
    EXPECT(stats.maximum_lookup_probes == 0);
#endif

    destroy_api_hashmap(hashmap);
    return 0;
}

int main(void) {
    EXPECT(test_create_insert_update_lookup() == 0);
    EXPECT(test_resize_keeps_elements() == 0);
    EXPECT(test_update_does_not_resize() == 0);
    EXPECT(test_fixed_capacity() == 0);
    EXPECT(test_invalid_inputs() == 0);
    EXPECT(test_probe_stats() == 0);

    return 0;
}
