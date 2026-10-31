/* Read-only provenance observer. Pinned Blur SHA-256:
 * dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f.
 * VA 005c2730/RVA 001c2730 calls device slot +60 and stores its output
 * at wrapper+8; VA 005c13a0/RVA 001c13a0 reads wrapper+8 and slot +4c.
 * Evidence: corresponding repaired listings and pinned PE. High confidence
 * for listed accesses; object provenance/behavior requires observed evidence.
 * This observer neither writes guest memory nor changes return values.
 */
#include "x86.h"
#include <stdio.h>
#include <stdlib.h>
extern const uint32_t recomp_func_addrs[], recomp_func_count;
extern void (*const recomp_raw_ptrs[])(X86 *);
extern RecompHookFn recomp_hook_ptrs[];
extern uint8_t recomp_hooked[];
static RecompHookFn previous[2];
static unsigned counts[2];
static int valid(uint32_t a, uint32_t n) {
    return a >= 0x10000 && a < GUEST_SIZE && n <= GUEST_SIZE - a;
}
static uint32_t word(uint32_t a) { return valid(a, 4) ? rd32(a) : 0; }
static void observe(X86 *c, uint32_t index) {
    unsigned kind = recomp_func_addrs[index] == 0x005c2730 ? 0 : 1;
    uint32_t sp = c->r[R_ESP], wrapper = kind ? c->r[R_ECX] : word(sp + 28);
    int log = counts[kind]++ < 16;
    if (log) {
        uint32_t object = word(wrapper + 8), table = word(object);
        fprintf(stderr, "volume.watch before va=%08x caller=%08x wrapper=%08x object=%08x vtable=%08x slot4c=%08x args=",
                recomp_func_addrs[index], word(sp), wrapper, object, table, word(table + 0x4c));
        for (unsigned i = 1; i <= (kind ? 3 : 9); ++i)
            fprintf(stderr, "%08x,", word(sp + i * 4));
        fputc('\n', stderr);
    }
    if (previous[kind]) previous[kind](c, index);
    else recomp_raw_ptrs[index](c);
    if (log) fprintf(stderr, "volume.watch after va=%08x eax=%08x wrapper=%08x object=%08x\n",
                     recomp_func_addrs[index], c->r[R_EAX], wrapper, word(wrapper + 8));
}
__attribute__((constructor)) static void install(void) {
    const uint32_t addresses[] = {0x005c2730, 0x005c13a0};
    for (unsigned k = 0; k < 2; ++k) {
        uint32_t i = 0;
        while (i < recomp_func_count && recomp_func_addrs[i] != addresses[k]) ++i;
        if (i == recomp_func_count) abort();
        previous[k] = recomp_hooked[i] ? recomp_hook_ptrs[i] : 0;
        recomp_hook_ptrs[i] = observe;
        recomp_hooked[i] = 1;
    }
}
