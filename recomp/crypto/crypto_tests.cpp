#include "crypto.h"
#include "sha1.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
extern "C" {
uint8_t *g_mem;
uint32_t g_watch_base = 0, g_watch_len = 0, g_dirty_count = 0;
RecompDirty g_dirty[RECOMP_DIRTY_SLOTS];
void recomp_watch_hit(uint32_t, uint32_t, uint64_t) { abort(); }
}
static uint32_t error;
void set_last_error(uint32_t n) { error = n; }
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "check failed line %d: %s\n", __LINE__, #x);             \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static std::string hex(const std::array<uint8_t, 20> &d) {
  char out[41];
  for (unsigned i = 0; i < 20; ++i)
    snprintf(out + 2 * i, 3, "%02x", d[i]);
  return out;
}
static uint32_t call(const char *name, std::initializer_list<uint32_t> args) {
  X86 c{};
  c.r[R_ESP] = 0x1000;
  unsigned i = 1;
  for (uint32_t a : args)
    wr32(0x1000 + 4 * i++, a);
  for (size_t j = 0; j < g_crypto_shim_count; ++j)
    if (!strcmp(name, g_crypto_shims[j].name)) {
      CHECK(args.size() == g_crypto_shims[j].argc_stdcall);
      g_crypto_shims[j].fn(&c);
      return c.r[R_EAX];
    }
  abort();
}
static uint32_t provider() {
  CHECK(call("CryptAcquireContextA", {0x2000, 0, 0, 1, 0xf0000000}) == 1);
  return rd32(0x2000);
}
static uint32_t hash(uint32_t p) {
  CHECK(call("CryptCreateHash", {p, 0x8004, 0, 0, 0x2004}) == 1);
  return rd32(0x2004);
}
int main() {
  struct Vector {
    size_t n;
    const char *digest;
  } vectors[] = {
#include "crypto_vectors.inc"
  };
  for (auto v : vectors) {
    std::vector<uint8_t> bytes(v.n);
    for (size_t i = 0; i < v.n; ++i)
      bytes[i] = uint8_t(i * 131 + 17);
    for (size_t stride : {size_t(1), size_t(7), size_t(64), size_t(1017)}) {
      GuestSha1 sha;
      for (size_t i = 0; i < v.n;) {
        size_t n = std::min(stride, v.n - i);
        sha.update(bytes.data() + i, n);
        i += n;
      }
      CHECK(hex(sha.digest()) == v.digest);
      CHECK(hex(sha.digest()) == v.digest);
    }
  }
  GuestSha1 million;
  const uint8_t a = 'a';
  for (unsigned i = 0; i < 1000000; ++i)
    million.update(&a, 1);
  CHECK(hex(million.digest()) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
  std::vector<uint8_t> memory(GUEST_SIZE, 0xa5);
  g_mem = memory.data();
  auto p = provider(), p2 = provider();
  CHECK(p != p2);
  auto h = hash(p), other = hash(p2);
  CHECK(h != other);
  memcpy(g_mem + 0x3000, "abc", 3);
  CHECK(call("CryptHashData", {h, 0x3000, 1, 0}) == 1);
  CHECK(call("CryptHashData", {other, 0, 0, 0}) == 1);
  CHECK(call("CryptHashData", {h, 0x3001, 2, 0}) == 1);
  CHECK(call("CryptHashData", {h, GUEST_SIZE - 1, 4, 0}) == 0 && error == 87);
  wr32(0x2100, 4);
  CHECK(call("CryptGetHashParam", {h, 4, 0x2200, 0x2100, 0}) == 1);
  CHECK(rd32(0x2200) == 20 && rd32(0x2100) == 4);
  wr32(0x2100, 0);
  CHECK(call("CryptGetHashParam", {h, 2, 0, 0x2100, 0}) == 1 &&
        rd32(0x2100) == 20);
  wr32(0x2100, 19);
  CHECK(call("CryptGetHashParam", {h, 2, 0x2200, 0x2100, 0}) == 0 &&
        error == 234);
  CHECK(rd32(0x2100) == 20);
  wr32(0x2100, 20);
  CHECK(call("CryptGetHashParam", {h, 2, 0x2300, 0x2100, 1}) == 0);
  CHECK(call("CryptGetHashParam", {h, 2, 0x2300, 0x2100, 0}) == 1);
  std::array<uint8_t, 20> digest;
  memcpy(digest.data(), g_mem + 0x2300, 20);
  CHECK(hex(digest) == "a9993e364706816aba3e25717850c26c9cd0d89d");
  CHECK(g_mem[0x22ff] == 0xa5 && g_mem[0x2314] == 0xa5);
  CHECK(call("CryptHashData", {h, 0, 0, 0}) == 0 && error == 0x8009000c);
  CHECK(call("CryptGetHashParam", {h, 2, 0x2300, 0x2100, 0}) == 1);
  CHECK(call("CryptReleaseContext", {p, 0}) == 0 && error == 170);
  CHECK(call("CryptGetHashParam", {other, 2, 0x2300, 0x2100, 0}) == 1);
  memcpy(digest.data(), g_mem + 0x2300, 20);
  CHECK(hex(digest) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  CHECK(call("CryptCreateHash", {p, 0x8003, 0, 0, 0x2004}) == 0 &&
        error == 0x80090008);
  CHECK(call("CryptCreateHash", {p, 0x8004, 1, 0, 0x2004}) == 0 &&
        error == 0x80090003);
  CHECK(call("CryptDestroyHash", {h}) == 1 &&
        call("CryptDestroyHash", {h}) == 0);
  CHECK(call("CryptDestroyHash", {other}) == 1);
  CHECK(call("CryptReleaseContext", {p, 0}) == 1 &&
        call("CryptReleaseContext", {p2, 0}) == 1);
  CHECK(call("CryptCreateHash", {p, 0x8004, 0, 0, 0x2004}) == 0);
  puts("crypto_tests: SHA-1 vectors, streaming, isolated handles, "
       "finalization, errors and output guards pass");
}
