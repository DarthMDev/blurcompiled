/* Experimental rollback of handwritten Lua overrides to their existing
 * translated instruction bodies. This adds no replacement Lua implementation.
 * Blur SHA-256:
 * dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f Evidence:
 * corresponding pinned .asm files, funcs.h and table.c raw_ptrs. RVAs: 6e5d20,
 * 6e9390, 6e5e00, 6e5ec0, 6e5f60. Confidence: high for routing to existing raw
 * bodies; complete instruction/runtime parity remains unverified.
 */
#include "x86.h"
#include <stdio.h>
#include <stdlib.h>

extern const uint32_t recomp_func_addrs[], recomp_func_count;
extern void (*const recomp_raw_ptrs[])(X86 *);
extern RecompHookFn recomp_hook_ptrs[];
extern uint8_t recomp_hooked[];

static void raw_body(X86 *c, uint32_t index) {
#ifdef BLUR_LUA_TRACE_GC
  static unsigned gc_calls;
  uint32_t state = c->r[R_EAX];
  int gc = recomp_func_addrs[index] == 0x00ae9390u;
  if (gc) {
    ++gc_calls;
    if (gc_calls < 8 || gc_calls % 100000 == 0) {
      uint32_t g = rd32(state + 0x10);
      fprintf(
          stderr,
          "lua.raw.gc enter n=%u L=%08x G=%08x phase=%u grey=%08x total=%u\n",
          gc_calls, state, g, rd8(g + 0x15), rd32(g + 0x24), rd32(g + 0x44));
    }
  }
  recomp_raw_ptrs[index](c);
  if (gc && (gc_calls < 8 || gc_calls % 100000 == 0))
    fprintf(stderr, "lua.raw.gc return n=%u result=%08x budget=%08x\n",
            gc_calls, c->r[R_EAX], c->r[R_EDI]);
#else
  recomp_raw_ptrs[index](c);
#endif
}

__attribute__((constructor)) static void install_raw_lua(void) {
  const uint32_t targets[] = {0x00ae5d20u, 0x00ae9390u, 0x00ae5e00u,
                              0x00ae5ec0u, 0x00ae5f60u};
  for (unsigned i = 0; i < sizeof targets / sizeof targets[0]; ++i) {
    uint32_t index = 0;
    while (index < recomp_func_count && recomp_func_addrs[index] != targets[i])
      ++index;
    if (index == recomp_func_count || recomp_hooked[index]) {
      fprintf(stderr, "lua.raw cannot install %08x\n", targets[i]);
      abort();
    }
    recomp_hook_ptrs[index] = raw_body;
    recomp_hooked[index] = 1;
    fprintf(stderr, "lua.raw installed %08x\n", targets[i]);
  }
}
