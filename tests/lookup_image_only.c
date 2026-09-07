// Proposed variant: walk ONLY the image of phi, in increasing order.
// A key at (i,j) is then found after rank(phi(i,j)) probes instead of phi(i,j).
// Since rank(x) <= x, every upper bound in the paper still applies.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "elastic_hashing.h"
#include "hash_functions.h"
#include "api_hashmap.h"
static const uint8_t SEED[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

// faithful re-implementation of the decoder in src/elastic_hashing.c
static int inverse_phi(uint64_t k, uint64_t*sub, uint64_t*loc){
    int bl=0; { uint64_t r=k; while(r){bl++;r>>=1;} }
    int cur=bl-1; uint64_t dp=0; int dpb=0;
    while(cur>=0){
        if(((k>>cur)&1)==0) return 0;
        cur--; if(cur<0) return 0;
        dp=(dp<<1)|((k>>cur)&1); dpb++;
        cur--; if(cur<0) return 0;
        if(((k>>cur)&1)==0){
            int sb=cur; if(sb<=0) return 0;
            uint64_t ds=k&((UINT64_C(1)<<sb)-1);
            int bd=0,bs=0; {uint64_t r=dp;while(r){bd++;r>>=1;} r=ds;while(r){bs++;r>>=1;}}
            if(dp==0||bd!=dpb||ds==0||bs!=sb) return 0;
            *sub=ds; *loc=dp; return 1;
        }
    }
    return 0;
}
int main(int argc,char**argv){
    const int cap=atoi(argv[1]); const float delta=(float)atof(argv[2]);
    const int nk=cap-(int)(delta*cap);
    ElasticHashmap* m=calloc(1,sizeof(ElasticHashmap));
    m->capacity=cap; m->table=calloc((size_t)cap,sizeof(Element*));
    char (*keys)[40]=malloc(sizeof(char[40])*(size_t)nk);
    Element** els=calloc((size_t)nk+1,sizeof(Element*));
    for(int i=0;i<nk;i++){snprintf(keys[i],40,"key-%d",i);
        els[i]=malloc(sizeof(Element));char*k=malloc(40);memcpy(k,keys[i],40);
        els[i]->key=k;els[i]->value=i;}
    batch_insert(m,delta,els,SEED);
    if(m->size!=nk){printf("insert failed\n");return 1;}
    ElasticSubArray* s=partition_elastic_hashmap(cap);
    int ns=n_elastic_subarrays(cap);

    long double sum=0; long mx=0, missed=0;
    for(int t=0;t<nk;t++){
        long probes=0; int found=0;
        for(uint64_t k=1; k<UINT64_C(1)<<40; k++){
            uint64_t si,lj;
            if(!inverse_phi(k,&si,&lj)) continue;      // SKIP: costs no probe
            if(si>(uint64_t)ns) continue;
            int len=s[si-1].length;
            int idx=s[si-1].starting_index+(int)(siphash_probe64(keys[t],k,SEED)%(uint64_t)len);
            probes++;
            Element* e=m->table[idx];
            if(e && strcmp(e->key,keys[t])==0){found=1;break;}
            if(probes> (long)cap*8) break;
        }
        if(!found){missed++;continue;}
        sum+=probes; if(probes>mx)mx=probes;
    }
    printf("n=%-7d d=1/%-4.0f | image-only lookup: mean=%7.2Lf  max=%-6ld  MISSED=%ld\n",
        cap,1/(double)delta,sum/(nk-missed),mx,missed);
    return 0;
}
