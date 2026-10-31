/* Read-only import observer for startup diagnostics. These asm labels bridge
 * existing C++ runtime exports; they are host APIs, not recovered game symbols.
 * Preserve any earlier observer and never suppress a guest import or its
 * result. Pinned Blur SHA
 * dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f:
 * observed/listed entries: string copy RVA 000048a0, application constructor
 * RVA 0002efc0, shutdown RVA 0002f620, window-message handler RVA 0044ae70,
 * quit-state setter RVA 004528b0. Confidence: high instruction/entry identity;
 * names describe this bounded observation, not full function recovery.
 * Watchpoint changes affect host diagnostics only; guest memory is read-only.
 */
#include "x86.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef bool (*CallObserver)(const char *, const uint32_t *, uint32_t,
                             uint32_t *);
extern X86 *current_context(void) __asm__("__Z21guest_current_contextv");
extern CallObserver
get_observer(void) __asm__("__Z29imports_set_call_observer_getv");
extern void set_observer(CallObserver) __asm__(
    "__Z25imports_set_call_observerPFbPKcPKjjPjE");
static CallObserver previous;
static unsigned count;
static int valid(uint32_t a, uint32_t n) {
  return a && a < GUEST_SIZE && n <= GUEST_SIZE - a;
}
static void string_at(uint32_t a) {
  fputc('"', stderr);
  for (unsigned i = 0; i < 240 && valid(a + i, 1); ++i) {
    unsigned ch = rd8(a + i);
    if (!ch)
      break;
    fputc(ch >= 32 && ch < 127 ? (int)ch : '?', stderr);
  }
  fputc('"', stderr);
}
static void ensure_return_observer(void);
static bool observe(const char *name, const uint32_t *args, uint32_t argc,
                    uint32_t *result) {
  ensure_return_observer();
  int terminal =
      strstr(name, "TerminateProcess") || strstr(name, "ExitProcess");
  if (count < 128 &&
      (terminal || strstr(name, "Bink") || strstr(name, "MessageBox") ||
       strstr(name, "OutputDebugString") || strstr(name, "PostQuitMessage") ||
       strstr(name, "SendMessageA"))) {
    ++count;
    X86 *c = current_context();
    uint32_t sp = c ? c->r[R_ESP] : 0;
    fprintf(stderr, "startup.import %s caller=%08x", name,
            valid(sp, 4) ? rd32(sp) : 0);
    for (unsigned i = 0; i < argc && i < 8; ++i)
      fprintf(stderr, " a%u=%08x", i, args[i]);
    if (argc &&
        (strstr(name, "BinkOpen@") || strstr(name, "OutputDebugString"))) {
      fputc(' ', stderr);
      string_at(args[0]);
    }
    if (terminal && c) {
      fprintf(stderr, " ebp=%08x stack=", c->r[R_EBP]);
      for (unsigned i = 0; i < 96 && valid(sp + i * 4, 4); ++i)
        fprintf(stderr, "%08x,", rd32(sp + i * 4));
    }
    fputc('\n', stderr);
  }
  return previous ? previous(name, args, argc, result) : true;
}
typedef void (*ReturnObserver)(const char *, uint32_t);
extern ReturnObserver
get_return_observer(void) __asm__("__Z31imports_set_return_observer_getv");
extern void set_return_observer(ReturnObserver) __asm__(
    "__Z27imports_set_return_observerPFvPKcjE");
static ReturnObserver previous_return;
static unsigned lock_returns;
static void returned(const char *name, uint32_t result) {
  if (strstr(name, "GetCurrentThreadId") && lock_returns < 32) {
    X86 *c = current_context();
    if (c && valid(c->r[R_ESP], 4) && rd32(c->r[R_ESP]) == 0x0084700du &&
        valid(c->r[R_EBP] + 12, 4)) {
      ++lock_returns;
      uint32_t cell = rd32(c->r[R_EBP] + 12);
      fprintf(stderr, "startup.lock tid=%u ctx=%08x cell=%08x", result,
              c->r[R_EBP], cell);
      if (valid(cell, 12))
        fprintf(stderr, " owner=%08x:%08x count=%u", rd32(cell + 4), rd32(cell),
                rd32(cell + 8));
      fputc('\n', stderr);
    }
  }
  if (previous_return)
    previous_return(name, result);
}
static void ensure_return_observer(void) {
  ReturnObserver active = get_return_observer();
  if (active != returned) {
    previous_return = active;
    set_return_observer(returned);
  }
}
extern const uint32_t recomp_func_addrs[], recomp_func_count;
extern void (*const recomp_raw_ptrs[])(X86 *);
extern RecompHookFn recomp_hook_ptrs[];
extern uint8_t recomp_hooked[];
static unsigned copies, shutdowns, messages;
static void dump_words(uint32_t a, unsigned words) {
  for (unsigned i = 0; i < words && valid(a + 4 * i, 4); ++i)
    fprintf(stderr, "%08x,", rd32(a + 4 * i));
}
static void guest_entry(X86 *c, uint32_t index) {
  uint32_t va = recomp_func_addrs[index], sp = c->r[R_ESP];
  uint32_t caller = valid(sp, 4) ? rd32(sp) : 0;
  uint32_t dst = c->r[R_ECX];
  if (va == 0x0084ae70u && messages++ < 32) {
    fprintf(stderr, "startup.message caller=%08x object=%08x args=", caller,
            dst);
    dump_words(sp + 4, 4);
    fputc('\n', stderr);
  }
  if (va == 0x008528b0u) {
    fprintf(stderr, "startup.quit.request caller=%08x stack=", caller);
    dump_words(sp, 64);
    fputc(' ', stderr);
    string_at(rd32(sp + 4));
    fputc('\n', stderr);
  }
  int copy = va == 0x004048a0u && caller >= 0x006849f0u &&
             caller < 0x00685140u && copies < 8;
  if (copy) {
    ++copies;
    uint32_t src = rd32(sp + 4);
    fprintf(stderr, "startup.string.copy caller=%08x src=%08x words=", caller,
            src);
    dump_words(src, 4);
    fputc(' ', stderr);
    string_at(src);
    fputc('\n', stderr);
  }
  if (va == 0x0042f620u && shutdowns++ < 4) {
    fprintf(stderr, "startup.shutdown caller=%08x object=%08x stack=", caller,
            dst);
    dump_words(sp, 48);
    fputc('\n', stderr);
  }
  recomp_raw_ptrs[index](c);
  if (va == 0x0042efc0u && !g_watch_len && valid(dst + 4, 4)) {
    g_watch_base = dst + 4;
    g_watch_len = 4;
    fprintf(stderr, "startup.watch.shutdown_state %08x initial=%08x\n",
            g_watch_base, rd32(g_watch_base));
  }
  if (copy) {
    fprintf(stderr, "startup.string.result dst=%08x words=", dst);
    dump_words(dst, 4);
    fputc(' ', stderr);
    string_at(dst);
    fputc('\n', stderr);
  }
}
__attribute__((constructor)) static void install(void) {
  previous_return = get_return_observer();
  set_return_observer(returned);
  previous = get_observer();
  set_observer(observe);
  const uint32_t addresses[] = {0x004048a0u, 0x0042f620u, 0x0084ae70u,
                                0x0042efc0u, 0x008528b0u};
  for (unsigned j = 0; j < 5; ++j) {
    uint32_t va = addresses[j], index = 0;
    while (index < recomp_func_count && recomp_func_addrs[index] != va)
      ++index;
    if (index == recomp_func_count || recomp_hooked[index])
      __builtin_trap();
    recomp_hook_ptrs[index] = guest_entry;
    recomp_hooked[index] = 1;
  }
}
