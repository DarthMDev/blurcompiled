/* Bounded host for one translated pure routine. No platform/game services.
 * Guest addresses remain uint32_t indexes into a separate zeroed arena.
 */
#include "x86.h"
#include <stdlib.h>
#include <stdio.h>

uint8_t *g_mem;
uint32_t g_watch_base, g_watch_len, g_dirty_count;
RecompDirty g_dirty[RECOMP_DIRTY_SLOTS];
const int recomp_resumable_stacks = 0;
void fn_00cb6460(X86 *c);

/* The one routine returns to its caller; dispatch to anything else is a bug. */
int32_t recomp_module_lookup(uint32_t addr) { (void)addr; return -1; }
int32_t recomp_index_of(uint32_t addr) { (void)addr; return -1; }
int recomp_is_call_return(uint32_t addr) { (void)addr; return 0; }
int recomp_module_is_call_return(uint32_t addr) { (void)addr; return 0; }
void recomp_call(X86 *c, uint32_t addr) { (void)c; (void)addr; abort(); }
void recomp_callback_return(X86 *c) { (void)c; abort(); }

void recomp_watch_hit(uint32_t addr, uint32_t n, uint64_t value) {
    (void)addr; (void)n; (void)value;
    abort(); /* This slice must never invoke runtime services. */
}
void recomp_null_access(uint32_t addr, int write) {
    fprintf(stderr, "slice null access %08x (%d)\n", addr, write);
    abort();
}

/* Expose guest bytes to the verification driver, never to recovered logic. */
uint8_t *blur_slice_memory(void) {
    if (!g_mem) g_mem = calloc(1, GUEST_SIZE);
    return g_mem;
}

/* Run only the CRC table entry. Nine words: x86's eight registers, then EIP. */
int blur_slice_run(uint32_t *regs, uint32_t *flags) {
    if (!blur_slice_memory()) return 0;
    X86 c = {0};
    for (unsigned i = 0; i < 8; ++i) c.r[i] = regs[i];
    x86_set_eflags(&c, *flags);
    fn_00cb6460(&c);
    for (unsigned i = 0; i < 8; ++i) regs[i] = c.r[i];
    regs[8] = c.eip;
    *flags = x86_get_eflags(&c);
    return 1;
}
