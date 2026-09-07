// Adversarial audit 1: is inverse_phi an exact inverse of phi, everywhere?
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "elastic_hashing.h"
static int cmp(const void*a,const void*b){uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return x<y?-1:(x>y);}
// re-declare the static decoder via a copy of its logic is impossible; instead we
// probe it indirectly through the public lookup on a synthetic single-key table.
// Direct approach: replicate phi, then check uniqueness (injectivity) by hashing.
int main(void){
    // 1) injectivity of phi over a large (i,j) range
    const int IMAX=24, JMAX=4000;
    size_t n=(size_t)IMAX*JMAX;
    uint64_t* v=malloc(n*sizeof(uint64_t));
    size_t k=0;
    for(int i=1;i<=IMAX;i++) for(int j=1;j<=JMAX;j++) v[k++]=elastic_phi(i,j);
    // sort & check duplicates
    qsort(v,n,sizeof(uint64_t),cmp);
    size_t dup=0; for(size_t t=1;t<n;t++) if(v[t]==v[t-1]) dup++;
    printf("phi injective over i<=%d, j<=%d (%zu pairs): %s (%zu duplicates)\n",
           IMAX,JMAX,n, dup?"NO":"YES", dup);
    // 2) zero values?
    size_t zeros=0; for(size_t t=0;t<n;t++) if(v[t]==0) zeros++;
    printf("phi(i,j)==0 anywhere: %s (%zu)\n", zeros?"YES (BUG)":"no", zeros);
    free(v);
    // 3) no zero-length subarrays for any capacity
    int bad=0, badcap=-1;
    for(int cap=1;cap<=200000;cap++){
        ElasticSubArray* s=partition_elastic_hashmap(cap);
        int ns=n_elastic_subarrays(cap); long sum=0;
        for(int i=0;i<ns;i++){ if(s[i].length<=0){bad++; if(badcap<0)badcap=cap;} sum+=s[i].length; }
        if(sum!=cap){ printf("capacity %d: lengths sum to %ld, not %d (BUG)\n",cap,sum,cap); bad++; }
        free(s);
    }
    printf("zero-length or non-covering partitions for cap 1..200000: %s\n",
           bad?"FOUND (BUG)":"none");
    return 0;
}
