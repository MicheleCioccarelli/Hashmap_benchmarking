#include <stdio.h>
#include "include/api_hashmap.h"


int main(void) {
    ApiHashmap* hashmap = create_api_hashmap();
    if (hashmap == NULL) {
        return 1;
    }

    insert_element_api_hashmap(hashmap, "alice", 1);
    insert_element_api_hashmap(hashmap, "bob", 2);
    insert_element_api_hashmap(hashmap, "alice", 3);

    const Option_Element_p result = retrieve_element_api_hashmap(hashmap, "alice");
    if (is_some(result.option)) {
        printf("%s -> %d\n", result.element_p->key, result.element_p->value);
    }

#ifdef HASHMAP_COUNT_PROBES
    const ApiHashmapProbeStats stats = api_hashmap_probe_stats(hashmap);
    printf("insert probes: %llu/%llu, lookup probes: %llu/%llu\n",
           (unsigned long long)stats.insertion_probes,
           (unsigned long long)stats.insertion_ops,
           (unsigned long long)stats.lookup_probes,
           (unsigned long long)stats.lookup_ops);
#endif

    destroy_api_hashmap(hashmap);
    return 0;
}
