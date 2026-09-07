// Three questions at once:
//  (1) when a key is found BEFORE phi, was it a phi-probe or a non-phi probe that found it?
//  (2) how does measured cost relate to phi, split by phi magnitude?
//  (3) our lookup vs full-table siphash, same table, across n
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "elastic_hashing.h"
#include "hash_functions.h"
#include "api_hashmap.h"
static const uint8_t SEED[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

// replicate the decoder's ACCEPT test: is k in the image of phi with i <= ns ?
// phi(i,.) is strictly increasing in j, so membership is a binary search per level
static int k_is_phi(uint64_t k,int ns,int*oi,long*oj){
    for(int i=1;i<=ns;i++){
        long lo=1, hi=1;
        while(elastic_phi((uint64_t)i,(uint64_t)hi)<k && hi<(1L<<40)) hi*=2;
        while(lo<=hi){
            long mid=lo+(hi-lo)/2;
            uint64_t p=elastic_phi((uint64_t)i,(uint64_t)mid);
            if(p==k){*oi=i;*oj=mid;return 1;}
            if(p<k) lo=mid+1; else hi=mid-1;
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
    ElasticSubArray* s=partition_elastic_hashmap(cap);
    int ns=n_elastic_subarrays(cap);
    int* slot_of=malloc(sizeof(int)*(size_t)nk);
    for(int i=0;i<nk;i++) slot_of[i]=-1;
    for(int q=0;q<cap;q++) if(m->table[q]) slot_of[m->table[q]->value]=q;

    long found_by_phi_probe=0, found_by_nonphi_probe=0, exactly_at=0;
    long double sum_meas=0,sum_phi=0, sum_meas_small=0,sum_phi_small=0, sum_meas_big=0,sum_phi_big=0;
    long nsmall=0,nbig=0;
    long double sum_full=0; long full_max=0;

    for(int t=0;t<nk;t++){
        int slot=slot_of[t];
        int lev=-1; for(int q=0;q<ns;q++) if(slot>=s[q].starting_index&&slot<s[q].starting_index+s[q].length) lev=q;
        long jmaxs=(long)s[lev].length*64+1000; uint64_t phi=0;
        for(long j=1;j<=jmaxs;j++){
            uint64_t g=elastic_phi((uint64_t)(lev+1),(uint64_t)j);
            if(s[lev].starting_index+(int)(siphash_probe64(keys[t],g,SEED)%(uint64_t)s[lev].length)==slot){phi=g;break;}
        }
        if(!phi) continue;
        // walk the same global sequence the lookup walks, and note WHICH k finds it
        uint64_t k; int oi; long oj;
        for(k=1;;k++){
            int idx; int isphi=k_is_phi(k,ns,&oi,&oj);
            if(isphi) idx=s[oi-1].starting_index+(int)(siphash_probe64(keys[t],k,SEED)%(uint64_t)s[oi-1].length);
            else      idx=(int)(siphash_probe64(keys[t],k,SEED)%(uint64_t)cap);
            if(idx==slot){ if(k==phi) exactly_at++; else if(isphi) found_by_phi_probe++; else found_by_nonphi_probe++; break; }
        }
        sum_meas+=k; sum_phi+=phi;
        if(phi<=1000){ nsmall++; sum_meas_small+=k; sum_phi_small+=phi; }
        else          { nbig++;  sum_meas_big+=k;  sum_phi_big+=phi;  }
        // full-table siphash lookup on the same table
        long fk;
        for(fk=1;fk<=8L*cap;fk++){ if((int)(siphash_probe64(keys[t],(uint64_t)fk,SEED)%(uint64_t)cap)==slot) break; }
        sum_full+=fk; if(fk>full_max) full_max=fk;
    }
    long ok=exactly_at+found_by_phi_probe+found_by_nonphi_probe;
    printf("n=%-7d d=1/%-4.0f keys=%d\n",cap,1/(double)delta,nk);
    printf("  who finds the key: exactly at phi=%ld (%.1f%%)  earlier phi-probe=%ld (%.1f%%)  NON-phi probe=%ld (%.1f%%)\n",
        exactly_at,100.0*exactly_at/ok, found_by_phi_probe,100.0*found_by_phi_probe/ok,
        found_by_nonphi_probe,100.0*found_by_nonphi_probe/ok);
    printf("  mean measured=%.1Lf  mean phi=%.1Lf\n", sum_meas/ok, sum_phi/ok);
    printf("    keys with phi<=1000  (%ld): mean measured=%.1Lf  mean phi=%.1Lf\n",nsmall,
        nsmall?sum_meas_small/nsmall:0, nsmall?sum_phi_small/nsmall:0);
    printf("    keys with phi >1000  (%ld): mean measured=%.1Lf  mean phi=%.1Lf\n",nbig,
        nbig?sum_meas_big/nbig:0, nbig?sum_phi_big/nbig:0);
    printf("  full-table siphash lookup: mean=%.1Lf max=%ld\n", sum_full/ok, full_max);
    return 0;
}
