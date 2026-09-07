// Negative-query cost of the level-by-level (i,j) scan on an Elastic Hashing table.
// Backs the "(i,j) scan" row of the negative-query table in the report, which previously
// had no program behind it. Same table construction as compare_lookup_models.c (keys
// "key-%d", default seed). The scan enumerates j = 1, 2, ... inside each level and leaves a
// level at its first empty slot, exactly as in compare_lookup_models.c.
//
//   usage:  scan_negative_queries <capacity> <delta> [absent_keys]
//
//   build:  cc -O2 -std=c11 -Iinclude -Ithird_party -DHASHMAP_COUNT_PROBES=1 \
//              tests/scan_negative_queries.c src/elastic_hashing.c src/funnel_hashing.c \
//              src/hash_functions.c src/api_hashmap.c third_party/siphash.c -lm
//
// Prints the free slots per level, the cost of one fixed absent key, and the average over
// <absent_keys> distinct absent keys ("absent-%d") for the j <= |Ai| and j <= 8|Ai| caps.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "elastic_hashing.h"
#include "hash_functions.h"
#include "api_hashmap.h"
static const uint8_t SEED[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static long scan(ElasticHashmap* map, ElasticSubArray* subs, int nsubs, const char* key, int mult, int* found){
    long probes=0; *found=0;
    for(int lev=0; lev<nsubs; lev++){
        int len=subs[lev].length; if(len<=0) continue;
        long jmax=(long)len*mult;
        for(long j=1;j<=jmax;j++){
            int idx=subs[lev].starting_index+(int)(siphash_probe64(key,elastic_phi((uint64_t)(lev+1),(uint64_t)j),SEED)%(uint64_t)len);
            probes++;
            Element* e=map->table[idx];
            if(e==NULL) break;
            if(strcmp(e->key,key)==0){*found=1;return probes;}
        }
    }
    return probes;
}
int main(int argc,char**argv){
    int cap=atoi(argv[1]); float delta=(float)atof(argv[2]); int nabs=argc>3?atoi(argv[3]):1000;
    int nkeys=cap-(int)(delta*cap);
    ElasticHashmap* map=calloc(1,sizeof(ElasticHashmap)); map->capacity=cap; map->table=calloc((size_t)cap,sizeof(Element*));
    Element** els=calloc((size_t)nkeys+1,sizeof(Element*));
    for(int i=0;i<nkeys;i++){ char* k=malloc(40); snprintf(k,40,"key-%d",i); els[i]=malloc(sizeof(Element)); els[i]->key=k; els[i]->value=i; }
    batch_insert(map,delta,els,SEED);
    if(map->size!=nkeys){fprintf(stderr,"insert failed\n");return 1;}
    ElasticSubArray* subs=partition_elastic_hashmap(cap); int nsubs=n_elastic_subarrays(cap);
    // count free slots per level
    printf("n=%d delta=%g keys=%d levels=%d\n",cap,(double)delta,nkeys,nsubs);
    for(int lev=0;lev<nsubs;lev++){int fr=0;for(int q=0;q<subs[lev].length;q++) if(!map->table[subs[lev].starting_index+q]) fr++; printf("  A%d len=%d free=%d\n",lev+1,subs[lev].length,fr);}
    int f; long single=scan(map,subs,nsubs,"absent-key-that-is-not-there",1,&f);
    printf("  single absent key 'absent-key-that-is-not-there': %ld probes (found=%d)\n",single,f);
    for(int mult=1; mult<=8; mult*=8){
        double tot=0; long mx=0, mn=1L<<40; int fnd=0;
        for(int a=0;a<nabs;a++){ char k[40]; snprintf(k,40,"absent-%d",a); long p=scan(map,subs,nsubs,k,mult,&f); if(f) fnd++; tot+=p; if(p>mx)mx=p; if(p<mn)mn=p; }
        printf("  j<=%d|Ai|: %d absent keys: avg=%.2f min=%ld max=%ld (wrongly found=%d)\n",mult,nabs,tot/nabs,mn,mx,fnd);
    }
    return 0;
}
