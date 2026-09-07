// Adversarial audit 2: for EVERY key, is the lookup probe count <= phi(i,j)?
// Reconstruction of (i,j) is post-hoc and independent of the lookup.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "elastic_hashing.h"
#include "hash_functions.h"
#include "api_hashmap.h"

static int run(int cap, float delta, const uint8_t* seed, int verbose){
    const int nk = cap - (int)(delta*cap);
    ElasticHashmap* m = calloc(1,sizeof(ElasticHashmap));
    m->capacity=cap; m->table=calloc((size_t)cap,sizeof(Element*));
    char (*keys)[40]=malloc(sizeof(char[40])*(size_t)nk);
    Element** els=calloc((size_t)nk+1,sizeof(Element*));
    for(int i=0;i<nk;i++){
        snprintf(keys[i],40,"k%d-%d",cap,i);
        els[i]=malloc(sizeof(Element)); char* k=malloc(40); memcpy(k,keys[i],40);
        els[i]->key=k; els[i]->value=i;
    }
    batch_insert(m,delta,els,seed);
    if(m->size!=nk){ printf("  cap=%-7d d=%-10g INSERT FAILED (%d/%d)\n",cap,(double)delta,m->size,nk); return -1; }

    ElasticSubArray* s=partition_elastic_hashmap(cap);
    int ns=n_elastic_subarrays(cap);
    // slot -> key index
    int* slot_of=malloc(sizeof(int)*(size_t)nk);
    for(int i=0;i<nk;i++) slot_of[i]=-1;
    for(int q=0;q<cap;q++) if(m->table[q]) slot_of[m->table[q]->value]=q;

    long violations=0, unreconstructed=0, at=0, before=0;
    uint64_t maxphi=0, maxprobe=0;
    elastic_hashmap_reset_probe_stats(m);
    uint64_t prev=0;
    for(int i=0;i<nk;i++){
        Option_Element_p r=retrieve_element_elastic_hashmap(m,keys[i],seed);
        uint64_t now=elastic_hashmap_probe_stats(m).lookup_probes;
        uint64_t probes=now-prev; prev=now;
        if(is_none(r.option)){ printf("  KEY NOT FOUND: %s\n",keys[i]); violations++; continue; }
        int slot=slot_of[i];
        int lev=-1; for(int t=0;t<ns;t++) if(slot>=s[t].starting_index && slot<s[t].starting_index+s[t].length) lev=t;
        // smallest j whose local draw equals slot; search generously past |A_i|
        long jmax=(long)s[lev].length*64+1000;
        uint64_t phi=0;
        for(long j=1;j<=jmax;j++){
            uint64_t g=elastic_phi((uint64_t)(lev+1),(uint64_t)j);
            uint64_t h=siphash_probe64(keys[i],g,seed);
            if(s[lev].starting_index+(int)(h%(uint64_t)s[lev].length)==slot){ phi=g; break; }
        }
        if(phi==0){ unreconstructed++; continue; }
        if(phi>maxphi) maxphi=phi;
        if(probes>maxprobe) maxprobe=probes;
        if(probes>phi){ violations++;
            if(verbose&&violations<4) printf("  VIOLATION %s: probes=%llu > phi=%llu\n",
                keys[i],(unsigned long long)probes,(unsigned long long)phi); }
        else if(probes==phi) at++; else before++;
    }
    printf("  cap=%-7d d=%-10g keys=%-7d | after-phi=%-4ld at=%-7ld before=%-7ld unrec=%-3ld | maxphi=%-10llu maxprobe=%llu\n",
        cap,(double)delta,nk,violations,at,before,unreconstructed,
        (unsigned long long)maxphi,(unsigned long long)maxprobe);
    return (int)(violations+unreconstructed);
}

int main(void){
    int total=0;
    const int caps[]={64,256,1024,4096,16384};
    const float deltas[]={0.5f,0.25f,0.125f,0.0625f,0.03125f,0.015625f,0.0078125f,0.00390625f};
    for(int sd=0; sd<3; sd++){
        uint8_t seed[16]; for(int b=0;b<16;b++) seed[b]=(uint8_t)(b*17+sd*101);
        printf("--- seed #%d ---\n",sd);
        for(size_t ci=0;ci<sizeof(caps)/sizeof(caps[0]);ci++)
            for(size_t di=0;di<sizeof(deltas)/sizeof(deltas[0]);di++){
                int v=run(caps[ci],deltas[di],seed,sd==0);
                if(v>0) total+=v;
            }
    }
    printf("\nTOTAL violations + unreconstructed: %d\n", total);
    return total!=0;
}
