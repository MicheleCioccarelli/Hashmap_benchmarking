// Is the MODEL's quantity (mean of phi(i,j) over keys) constant in n, as Theorem 1 says?
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "elastic_hashing.h"
#include "hash_functions.h"
#include "api_hashmap.h"
static const uint8_t SEED[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

int main(int argc,char**argv){
    const int cap=atoi(argv[1]); const float delta=(float)atof(argv[2]);
    const double cc = argc>3 ? atof(argv[3]) : 100.0;
    const int nk=cap-(int)(delta*cap);
    ElasticHashmap* m=calloc(1,sizeof(ElasticHashmap));
    m->capacity=cap; m->table=calloc((size_t)cap,sizeof(Element*));
    char (*keys)[40]=malloc(sizeof(char[40])*(size_t)nk);
    Element** els=calloc((size_t)nk+1,sizeof(Element*));
    for(int i=0;i<nk;i++){ snprintf(keys[i],40,"key-%d",i);
        els[i]=malloc(sizeof(Element)); char*k=malloc(40); memcpy(k,keys[i],40);
        els[i]->key=k; els[i]->value=i; }
    batch_insert_with_c(m,delta,cc,els,SEED);
    ElasticSubArray* s=partition_elastic_hashmap(cap);
    int ns=n_elastic_subarrays(cap);
    int* slot_of=malloc(sizeof(int)*(size_t)nk);
    for(int i=0;i<nk;i++) slot_of[i]=-1;
    for(int q=0;q<cap;q++) if(m->table[q]) slot_of[m->table[q]->value]=q;

    long double sumphi=0; uint64_t maxphi=0; long jsum=0; long isum=0; long jmax=0;
    long hist_i[40]={0}; long hist_j[12]={0}; long unrec=0;
    for(int t=0;t<nk;t++){
        int slot=slot_of[t]; if(slot<0){unrec++;continue;}
        int lev=-1; for(int q=0;q<ns;q++) if(slot>=s[q].starting_index && slot<s[q].starting_index+s[q].length) lev=q;
        long jmaxs=(long)s[lev].length*64+1000; uint64_t phi=0; long jj=0;
        for(long j=1;j<=jmaxs;j++){
            uint64_t g=elastic_phi((uint64_t)(lev+1),(uint64_t)j);
            if(s[lev].starting_index+(int)(siphash_probe64(keys[t],g,SEED)%(uint64_t)s[lev].length)==slot){phi=g;jj=j;break;}
        }
        if(!phi){unrec++;continue;}
        sumphi+=phi; if(phi>maxphi)maxphi=phi;
        jsum+=jj; isum+=lev+1; if(jj>jmax)jmax=jj;
        if(lev<40) hist_i[lev]++;
        int b=0; while((1L<<b)<=jj && b<11) b++;
        hist_j[b]++;
    }
    long ok=nk-unrec;
    printf("c=%-6g n=%-8d d=1/%-5.0f keys=%-8d | mean phi=%12.1Lf  max phi=%-12llu | mean i=%5.2f mean j=%5.2f max j=%ld\n",
        cc,cap,1/(double)delta,nk, sumphi/ok, (unsigned long long)maxphi,
        (double)isum/ok,(double)jsum/ok,jmax);
    printf("            j histogram (by power of two):");
    for(int b=0;b<9;b++) printf(" %ld", hist_j[b]);
    printf("\n            i histogram:");
    for(int b=0;b<12;b++) if(hist_i[b]) printf(" A%d=%ld",b+1,hist_i[b]);
    printf("\n");
    return 0;
}
