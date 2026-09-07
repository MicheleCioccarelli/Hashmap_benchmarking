// How much of the walk is spent on k outside the image of phi?
// rank(phi) = how many k <= phi(i,j) ARE in the image (with i <= ns).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "elastic_hashing.h"
#include "hash_functions.h"
#include "api_hashmap.h"
static const uint8_t SEED[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
// count of (i',j') with i'<=ns and phi(i',j') <= K
static long image_count_upto(uint64_t K,int ns){
    long tot=0;
    for(int i=1;i<=ns;i++){
        long lo=0,hi=1;
        while(elastic_phi((uint64_t)i,(uint64_t)hi)<=K && hi<(1L<<40)) hi*=2;
        long a=1,b=hi;
        while(a<=b){ long mid=a+(b-a)/2;
            if(elastic_phi((uint64_t)i,(uint64_t)mid)<=K){lo=mid;a=mid+1;} else b=mid-1; }
        tot+=lo;
    }
    return tot;
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
    long double sphi=0,srank=0; long ok=0; uint64_t maxphi=0; long maxrank=0;
    for(int t=0;t<nk;t++){
        int slot=slot_of[t]; int lev=-1;
        for(int q=0;q<ns;q++) if(slot>=s[q].starting_index&&slot<s[q].starting_index+s[q].length) lev=q;
        long jm=(long)s[lev].length*64+1000; uint64_t phi=0;
        for(long j=1;j<=jm;j++){ uint64_t g=elastic_phi((uint64_t)(lev+1),(uint64_t)j);
            if(s[lev].starting_index+(int)(siphash_probe64(keys[t],g,SEED)%(uint64_t)s[lev].length)==slot){phi=g;break;} }
        if(!phi) continue;
        long r=image_count_upto(phi,ns);
        sphi+=phi; srank+=r; ok++;
        if(phi>maxphi)maxphi=phi; if(r>maxrank)maxrank=r;
    }
    printf("n=%-7d d=1/%-4.0f | mean phi=%9.1Lf  mean rank=%7.1Lf  ratio=%5.1Lf | max phi=%-10llu max rank=%ld\n",
        cap,1/(double)delta,sphi/ok,srank/ok,sphi/srank,(unsigned long long)maxphi,maxrank);
    return 0;
}
