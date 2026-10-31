/* Diagnostic observer, not recovered game behavior. Calls the original body.
 * Executable SHA-256:
 * dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f Observed
 * entry VA 00ae9b00 / RVA 006e9b00: listing reads arg0 as the Lua-like state
 * used by the protected-call path. Its +8 cell is later used as stack top.
 * Evidence: pinned assembly and startup crash logs. Layout confidence: bounded
 * to these reads/writes; no general Lua ABI or gameplay symbol is claimed.
 * Other observed hooks: AE5D20/RVA 6E5D20 (allocation call), AF0DC0/RVA
 * 6F0DC0 (constant-table writes), 42C8A0/RVA 2C8A0 (free wrapper). Evidence is
 * the corresponding pinned assembly plus the captured allocation/free stack;
 * confidence is high for reached addresses and argument accesses only.
 */
#include "x86.h"
#include <stdio.h>
#include <stdlib.h>

extern const uint32_t recomp_func_addrs[], recomp_func_count;
extern void (*const recomp_raw_ptrs[])(X86 *);
extern RecompHookFn recomp_hook_ptrs[];
extern uint8_t recomp_hooked[];

static uint32_t state_top;

static void observe_call(X86 *c, uint32_t index) {
  if (recomp_func_addrs[index] == 0x00ae9b00u) {
    uint32_t state = rd32(c->r[R_ESP] + 4);
    if (state && state < GUEST_SIZE - 12 && state_top != state + 8) {
      state_top = state + 8;
      fprintf(stderr, "lua.watch state=%08x top=%08x value=%08x\n", state,
              state_top, rd32(state_top));
      g_watch_base = state_top;
      g_watch_len = 4;
    }
  }
  uint32_t before = state_top ? rd32(state_top) : 0;
  if (before >= GUEST_SIZE) {
    fprintf(stderr, "lua.top.invalid.enter target=%08x top=%08x\n",
            recomp_func_addrs[index], before);
    abort();
  }
  uint32_t target = recomp_func_addrs[index];
  uint32_t block = 0, size = 0;
  if (target == 0x00ae5d20u) {
    block = rd32(c->r[R_ESP] + 8);
    size = rd32(c->r[R_ESP] + 16);
  }
  if (target == 0x00af0dc0u) {
    uint32_t proto = rd32(c->r[R_ESP] + 4);
    fprintf(
        stderr,
        "lua.loadconstants enter proto=%08x k=%08x state=%08x loader=%08x\n",
        proto, rd32(proto + 8), state_top - 8, c->r[R_EAX]);
  }
  if (target == 0x0042c8a0u && state_top &&
      rd32(c->r[R_ESP] + 4) == state_top - 8) {
    fprintf(stderr, "lua.state.free caller=%08x state=%08x\n",
            rd32(c->r[R_ESP]), state_top - 8);
    abort(); // Diagnostic stop before the first observed live-state free.
  }
  recomp_raw_ptrs[index](c);
  if (target == 0x00ae5d20u && state_top && c->r[R_EAX] &&
      (c->r[R_EAX] == state_top - 8 || size > 0x01000000u))
    fprintf(stderr, "lua.alloc suspicious block=%08x size=%08x result=%08x\n",
            block, size, c->r[R_EAX]);
  uint32_t after = state_top ? rd32(state_top) : 0;
  if (after >= GUEST_SIZE) {
    fprintf(stderr,
            "lua.top.invalid.return target=%08x before=%08x after=%08x "
            "ESP=%08x EAX=%08x\n",
            recomp_func_addrs[index], before, after, c->r[R_ESP], c->r[R_EAX]);
    abort();
  }
}

__attribute__((constructor)) static void install_lua_watch(void) {
  for (uint32_t index = 0; index < recomp_func_count; ++index) {
    uint32_t target = recomp_func_addrs[index];
    if (target != 0x00ae9b00u && target != 0x00ae5d20u &&
        target != 0x00af0dc0u && target != 0x0042c8a0u)
      continue;
    if (recomp_hooked[index]) {
      fprintf(stderr, "lua.watch cannot replace another observer\n");
      abort();
    }
    recomp_hook_ptrs[index] = observe_call;
    recomp_hooked[index] = 1;
  }
}
