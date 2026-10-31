/* Read-only startup lifecycle observer. Executable SHA-256:
 * dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f.
 * VAs/RVAs: ctor 0041eff0/0001eff0, init 00419e70/00019e70,
 * teardown 00419fb0/00019fb0, update 00422610/00022610,
 * phase 0041a4b0/0001a4b0, indirect caller 00858930/00458930.
 * Evidence: pinned PE / corresponding repaired listings. High confidence for
 * instruction/field identities; names are descriptive, behavior unverified.
 * Init requires +a8/+3b4/+3b8 and stores the object at +5c; the update reads
 * +5c before the indirect caller uses slots +8/+4. No guest writes.
 */
#include "x86.h"
#include <stdio.h>
#include <stdlib.h>
extern const uint32_t recomp_func_addrs[], recomp_func_count;
extern void (*const recomp_raw_ptrs[])(X86 *);
extern RecompHookFn recomp_hook_ptrs[];
extern uint8_t recomp_hooked[];
static const uint32_t addresses[] = {0x0041eff0,0x00419e70,0x00419fb0,0x00422610,0x0041a4b0,0x00858930};
static RecompHookFn previous[6];
static unsigned counts[6];
static uint32_t word(uint32_t a) {
    return a >= 0x10000 && a < GUEST_SIZE && 4 <= GUEST_SIZE-a ? rd32(a) : 0;
}
static void snapshot(const char *when, uint32_t va, uint32_t object, uint32_t caller, X86 *c) {
    fprintf(stderr,"startup.object %s va=%08x object=%08x caller=%08x eax=%08x vtable=%08x field5c=%08x field60=%08x fielda8=%08x field3b4=%08x field3b8=%08x\n",
        when,va,object,caller,c->r[R_EAX],word(object),word(object+0x5c),word(object+0x60),word(object+0xa8),word(object+0x3b4),word(object+0x3b8));
}
static void observe(X86 *c,uint32_t index) {
    unsigned k=0; while(k<6 && addresses[k]!=recomp_func_addrs[index]) ++k;
    if(k==6) abort();
    uint32_t object=c->r[R_ECX], caller=word(c->r[R_ESP]);
    int log=counts[k]++<8 || (k==5 && !object);
    if(log) snapshot("before",addresses[k],object,caller,c);
    if(previous[k]) previous[k](c,index); else recomp_raw_ptrs[index](c);
    if(log) snapshot("after",addresses[k],object,caller,c);
}
__attribute__((constructor)) static void install(void) {
    for(unsigned k=0;k<6;++k) {
        uint32_t i=0; while(i<recomp_func_count && recomp_func_addrs[i]!=addresses[k]) ++i;
        if(i==recomp_func_count) abort();
        previous[k]=recomp_hooked[i]?recomp_hook_ptrs[i]:0;
        recomp_hook_ptrs[i]=observe; recomp_hooked[i]=1;
    }
}
