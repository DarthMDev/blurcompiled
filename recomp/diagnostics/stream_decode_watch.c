/* New observer, no decoded result or guest memory is changed.
 * Blur executable SHA-256:
 * dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f.
 * Decoder VA 00add770 / RVA 006dd770: pinned PE/listing reads the stream at
 * ESP+4, input at +0/+4, output at +c/+10 and internal state at +1c.
 * Error branches (e.g. VA 00add8bb / RVA 006dd8bb) store an image string
 * address at stream+18. High confidence for field accesses/return value;
 * complete decoder parity and the cause of any error remain unverified.
 */
#include "x86.h"
#include <stdio.h>
#include <stdlib.h>
extern const uint32_t recomp_func_addrs[], recomp_func_count;
extern void (*const recomp_raw_ptrs[])(X86 *);
extern RecompHookFn recomp_hook_ptrs[];
extern uint8_t recomp_hooked[];
static unsigned logged;
static int valid(uint32_t address, uint32_t size) {
  return address && address < GUEST_SIZE && size <= GUEST_SIZE - address;
}
static void observe(X86 *c, uint32_t index) {
  uint32_t sp = c->r[R_ESP], stream = rd32(sp + 4), caller = rd32(sp);
  uint32_t input = 0, available = 0, output = 0;
  unsigned char prefix[16] = {0};
  unsigned count = 0;
  if (valid(stream, 0x20)) {
    input = rd32(stream);
    available = rd32(stream + 4);
    output = rd32(stream + 0x10);
    count = available < sizeof prefix ? available : sizeof prefix;
    if (valid(input, count))
      for (unsigned i = 0; i < count; ++i)
        prefix[i] = rd8(input + i);
    else
      count = 0;
  }
  recomp_raw_ptrs[index](c);
  if ((int32_t)c->r[R_EAX] >= 0 || logged >= 8 || !valid(stream, 0x20))
    return;
  ++logged;
  uint32_t message = rd32(stream + 0x18);
  char text[160] = {0};
  for (unsigned i = 0; i + 1 < sizeof text && valid(message, 1);
       ++i, ++message) {
    unsigned char byte = rd8(message);
    if (!byte)
      break;
    text[i] = byte >= 32 && byte < 127 ? (char)byte : '?';
  }
  fprintf(stderr,
          "stream.decode.error caller=%08x stream=%08x result=%d "
          "input_before=%08x available_before=%u output_before=%u "
          "input_after=%08x available_after=%u output_after=%u "
          "total_in=%u total_out=%u message=\"%s\" prefix=",
          caller, stream, (int32_t)c->r[R_EAX], input, available, output,
          rd32(stream), rd32(stream + 4), rd32(stream + 0x10), rd32(stream + 8),
          rd32(stream + 0x14), text);
  for (unsigned i = 0; i < count; ++i)
    fprintf(stderr, "%02x", prefix[i]);
  fputc('\n', stderr);
}
__attribute__((constructor)) static void install(void) {
  uint32_t index = 0;
  while (index < recomp_func_count && recomp_func_addrs[index] != 0x00add770u)
    ++index;
  if (index == recomp_func_count || recomp_hooked[index])
    abort();
  recomp_hook_ptrs[index] = observe;
  recomp_hooked[index] = 1;
}
