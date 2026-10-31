/* New diagnostic only. Pinned Blur SHA-256:
 * dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f.
 * VA 00ae9b00 / RVA 006e9b00 takes state at ESP+4 (pinned assembly).
 * Offset 0x10 is read as global-state pointer by VA 00ae5d20 / RVA 006e5d20.
 * The argument guard at VA 00ae2ba0 / RVA 006e2ba0 follows its first two MOVs
 * in the pinned listing. High confidence for these accesses; no repair occurs.
 */
#include "x86.h"
#include <stdio.h>
#include <stdlib.h>
extern const uint32_t recomp_func_addrs[], recomp_func_count;
extern void (*const recomp_raw_ptrs[])(X86 *);
extern RecompHookFn recomp_hook_ptrs[];
extern uint8_t recomp_hooked[];
static uint32_t observed_state;
static void observe_state(X86 *c, uint32_t index) {
  uint32_t state = rd32(c->r[R_ESP] + 4);
  if (recomp_func_addrs[index] == 0x00ae2ba0u) {
    if (observed_state && state == observed_state + 8) {
      fprintf(stderr,
              "lua.argument.invalid caller=%08x expected=%08x actual=%08x "
              "ESP=%08x ECX=%08x\n",
              rd32(c->r[R_ESP]), observed_state, state, c->r[R_ESP],
              c->r[R_ECX]);
      abort();
    }
    recomp_raw_ptrs[index](c);
    return;
  }
  if (state && state < GUEST_SIZE - 20) {
    if (g_watch_base != state + 0x10)
      fprintf(stderr, "lua.global.watch L=%08x G=%08x\n", state,
              rd32(state + 0x10));
    g_watch_base = state + 0x10;
    g_watch_len = 4;
    observed_state = state;
  }
  recomp_raw_ptrs[index](c);
}
__attribute__((constructor)) static void install_state_watch(void) {
  const uint32_t targets[] = {0x00ae9b00u, 0x00ae2ba0u};
  for (unsigned i = 0; i < 2; ++i) {
    uint32_t index = 0;
    while (index < recomp_func_count && recomp_func_addrs[index] != targets[i])
      ++index;
    if (index == recomp_func_count || recomp_hooked[index])
      abort();
    recomp_hook_ptrs[index] = observe_state;
    recomp_hooked[index] = 1;
  }
}
