// Synthetic adapter-contract tests. No game instructions/assets or platform APIs.
// The allocation/mutex/callback doubles isolate the new adapter from kit services.
#include "runtime/imports.h"
#include "runtime/memory.h"
#include "runtime/guest.h"
#include <algorithm>
#include <map>
#include <vector>
#include <cstdio>
#include <cstdlib>

uint8_t *g_mem;
uint32_t g_watch_base, g_watch_len, g_dirty_count;
RecompDirty g_dirty[RECOMP_DIRTY_SLOTS];
static uint32_t watches;
void recomp_watch_hit(uint32_t, uint32_t, uint64_t) { ++watches; }
void recomp_null_access(uint32_t, int) { abort(); }
static auto &registry() { static std::map<std::string, ImportShim> m; return m; }
void imports_register(const ImportShim *shims, size_t n) {
    for (size_t i = 0; i < n; ++i) registry()[std::string(shims[i].dll) + "!" + shims[i].name] = shims[i];
}
void imports_register_data(const char *, const char *, uint32_t) {}
static std::map<uint32_t, uint32_t> blocks;
static uint32_t next_address = HEAP_BASE;
static bool fail_allocation;
uint32_t heap_alloc(uint32_t n, bool zero, uint32_t align) {
    if (fail_allocation) return 0;
    uint32_t p = (next_address + align - 1) & ~(align - 1);
    next_address = p + n + 32;
    blocks[p] = n;
    memset(g_mem + p, zero ? 0 : 0xcc, n);
    return p;
}
bool heap_owns(uint32_t p) { return blocks.count(p); }
uint32_t heap_size(uint32_t p) { return blocks.at(p); }
bool heap_free(uint32_t p) { return blocks.erase(p) != 0; }
uint32_t heap_realloc(uint32_t p, uint32_t n, bool zero) {
    uint32_t q = heap_alloc(n, zero, 16);
    if (q) { memcpy(g_mem + q, g_mem + p, std::min(n, heap_size(p))); heap_free(p); }
    return q;
}
std::string gm_str(uint32_t p, size_t max) { return std::string(reinterpret_cast<char *>(g_mem + p), strnlen(reinterpret_cast<char *>(g_mem + p), max)); }
void open_mutex_named(X86 *c, const std::string &name) {
    if (name != "synthetic-mutex") abort();
    set_eax(c, 0x1234);
}
static std::vector<uint32_t> callbacks;
static std::map<uint32_t, uint32_t> results;
static std::vector<std::pair<uint32_t, uint32_t>> section_calls;
uint32_t imports_resolve(const char *, const char *name) {
    if (strcmp(name, "InitializeCriticalSection") == 0) return 0x50000;
    if (strcmp(name, "EnterCriticalSection") == 0) return 0x50010;
    if (strcmp(name, "LeaveCriticalSection") == 0) return 0x50020;
    abort();
}
uint32_t guest_call(X86 *, uint32_t fn, const uint32_t *args, int nargs) {
    if (fn >= 0x50000 && fn <= 0x50020) {
        if (nargs != 1) abort();
        section_calls.emplace_back(fn, args[0]);
        return 0;
    }
    if (nargs) abort();
    callbacks.push_back(fn);
    return results.at(fn);
}
static void require(bool condition, const char *what) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", what); exit(1); }
}
static X86 cpu;
static uint32_t invoke(const char *name, std::initializer_list<uint32_t> args) {
    cpu.r[R_ESP] = 0x30000;
    uint32_t p = cpu.r[R_ESP] + 4;
    for (uint32_t a : args) { wr32(p, a); p += 4; }
    registry().at(name).fn(&cpu);
    require(cpu.r[R_ESP] == 0x30000, "shim must leave stack cleanup to dispatcher");
    return cpu.r[R_EAX];
}
int main() {
    g_mem = static_cast<uint8_t *>(calloc(1, GUEST_SIZE));
    require(g_mem != nullptr, "guest arena");
    require(registry().at("KERNEL32.dll!InterlockedCompareExchange").argc_stdcall == 3, "atomic stdcall ABI");
    require(registry().at("MSVCR90.dll!_initterm_e").argc_stdcall == ARGC_CDECL, "CRT cdecl ABI");
    wr32(0x20000, 0xffffffff);
    require(invoke("KERNEL32.dll!InterlockedCompareExchange", {0x20000, 7, 3}) == 0xffffffff && rd32(0x20000) == 0xffffffff, "failed comparison preserves destination/old value");
    require(invoke("KERNEL32.dll!InterlockedCompareExchange", {0x20000, 7, 0xffffffff}) == 0xffffffff && rd32(0x20000) == 7, "successful comparison updates destination");
    memcpy(g_mem + 0x21000, "synthetic-mutex", 16);
    require(invoke("KERNEL32.dll!OpenMutexA", {1, 0, 0x21000}) == 0x1234, "mutex name forwarded to existing service");
    uint32_t values[] = {0, 0x100000, 0x100010, 0x100020};
    memcpy(g_mem + 0x22000, values, sizeof(values));
    results = {{0x100000, 0}, {0x100010, 42}, {0x100020, 0}};
    require(invoke("MSVCR90.dll!_initterm_e", {0x22000, 0x22010}) == 42, "initializer propagates first failure");
    require(callbacks == std::vector<uint32_t>({0x100000, 0x100010}), "failure stops callback traversal, null skipped");
    callbacks.clear();
    invoke("MSVCR90.dll!_initterm", {0x22000, 0x22010});
    require(callbacks == std::vector<uint32_t>({0x100000, 0x100010, 0x100020}), "void initializer walks complete ordered range");
    callbacks.clear();
    require(invoke("MSVCR90.dll!_initterm_e", {0x22000, 0x22000}) == 0 && callbacks.empty(), "empty table succeeds without calls");
    const uint32_t fm = invoke("MSVCR90.dll!__p__fmode", {});
    require(fm >= HEAP_BASE && rd32(fm) == 0x4000, "fmode guest storage/default");
    wr32(fm, 0x8000);
    require(invoke("MSVCR90.dll!__p__fmode", {}) == fm && rd32(fm) == 0x8000, "fmode pointer remains writable and stable");
    const uint32_t cm = invoke("MSVCR90.dll!__p__commode", {});
    require(cm != fm && rd32(cm) == 0, "commode separate guest storage");
    for (uint32_t value : {0u, 1u, 0x12345678u, 0xffffffffu}) {
        const uint32_t encoded = invoke("MSVCR90.dll!_encode_pointer", {value});
        require(encoded != value && invoke("MSVCR90.dll!_decode_pointer", {encoded}) == value, "opaque pointer round trip including sentinels");
    }
    cpu.fpu_cw = 0x37f;
    require(invoke("MSVCR90.dll!_controlfp_s", {0x25000, 0x10000, 0x30000}) == 0 && cpu.fpu_cw == 0x27f && rd32(0x25000) == 0x9001f, "53-bit precision and abstract result");
    invoke("MSVCR90.dll!_controlfp_s", {0x25000, 0, 0});
    require(cpu.fpu_cw == 0x27f && rd32(0x25000) == 0x9001f, "FP query preserves control state");
    invoke("MSVCR90.dll!_lock", {8});
    invoke("MSVCR90.dll!_lock", {8});
    invoke("MSVCR90.dll!_unlock", {8});
    invoke("MSVCR90.dll!_unlock", {8});
    require(section_calls.size() == 5 && section_calls[0].first == 0x50000 && section_calls[1].second == section_calls[4].second, "recursive CRT lock routes through same scheduler object");
    wr32(0x26000, 0); wr32(0x26004, 0);
    require(invoke("MSVCR90.dll!__dllonexit", {0x100000, 0x26000, 0x26004}) == 0x100000, "on-exit append result");
    invoke("MSVCR90.dll!__dllonexit", {0x100010, 0x26000, 0x26004});
    require(rd32(0x26004) == rd32(0x26000) + 8 && rd32(rd32(0x26000)) == 0x100000 && rd32(rd32(0x26000) + 4) == 0x100010, "on-exit realloc preserves order");
    callbacks.clear();
    require(invoke("MSVCR90.dll!_onexit", {0x100000}) == 0x100000, "global exit callback registered");
    invoke("MSVCR90.dll!_onexit", {0x100020});
    invoke("MSVCR90.dll!_cexit", {});
    require(callbacks == std::vector<uint32_t>({0x100020, 0x100000}), "exit callbacks execute LIFO");
    invoke("MSVCR90.dll!_cexit", {});
    require(callbacks.size() == 2, "exit callbacks not replayed");
    memcpy(g_mem + 0x23000, "abcdef", 6);
    g_dirty_count = 1;
    g_dirty[0] = {0x24000, 16, 0xffffffff, 0};
    g_watch_base = 0x24001; g_watch_len = 2;
    require(invoke("MSVCR90.dll!memcpy", {0x24000, 0x23000, 6}) == 0x24000 && memcmp(g_mem + 0x24000, "abcdef", 6) == 0, "memcpy copies and returns destination");
    require(g_dirty[0].lo == 0x24000 && g_dirty[0].hi == 0x24006 && watches == 2, "bulk writes preserve dirty span/watchpoints");
    g_dirty_count = g_watch_len = 0;
    invoke("MSVCR90.dll!memmove", {0x24001, 0x24000, 5});
    require(memcmp(g_mem + 0x24000, "aabcde", 6) == 0, "memmove overlapping range");
    invoke("MSVCR90.dll!memset", {0x24000, 0x1ff, 6});
    require(g_mem[0x24000] == 255 && g_mem[0x24005] == 255, "memset byte truncation");
    uint32_t p = invoke("MSVCR90.dll!calloc", {7, 3});
    require(p && heap_size(p) == 21 && rd32(p) == 0, "calloc zeroed size");
    wr32(p, 0x12345678);
    p = invoke("MSVCR90.dll!realloc", {p, 40});
    require(p && heap_size(p) == 40 && rd32(p) == 0x12345678, "realloc preserves contents");
    require(invoke("MSVCR90.dll!_msize", {p}) == 40, "requested block size");
    uint32_t aligned = invoke("MSVCR90.dll!_aligned_malloc", {33, 4096});
    require(aligned && !(aligned & 4095) && heap_size(aligned) == 33, "aligned allocation uses guest allocator");
    invoke("MSVCR90.dll!_aligned_free", {aligned});
    require(!heap_owns(aligned), "aligned block released");
    invoke("MSVCR90.dll!free", {0});
    require(invoke("MSVCR90.dll!calloc", {0xffffffff, 2}) == 0, "calloc detects overflow");
    uint32_t e = invoke("MSVCR90.dll!_errno", {});
    require(rd32(e) == 12, "allocation overflow sets ENOMEM");
    cpu.fs_base = 0x40000;
    require(invoke("MSVCR90.dll!_errno", {}) != e, "errno is guest-thread specific");
    invoke("MSVCR90.dll!_errno", {}); // Provision errno before simulating exhaustion.
    fail_allocation = true;
    require(invoke("MSVCR90.dll!malloc", {32}) == 0, "allocation failure returns null");
    const uint32_t saved_begin = rd32(0x26000), saved_end = rd32(0x26004);
    require(invoke("MSVCR90.dll!__dllonexit", {0x100020, 0x26000, 0x26004}) == 0 && rd32(0x26000) == saved_begin && rd32(0x26004) == saved_end, "on-exit allocation failure leaves table intact");
    puts("startup adapters: synthetic memory, ABI, initializer and allocation checks passed");
    free(g_mem);
}
