// New, bounded Windows/VC90 startup adapters, not recovered game source.
// Called with the kit's scheduler baton. All pointers remain guest addresses;
// host addresses are used only while accessing validated arena storage.
#include "runtime/imports.h"
#include "runtime/memory.h"
#include "runtime/kernel32_internal.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>

extern "C" uint32_t g_last_valid_G = 0;

namespace {
[[noreturn]] void unsupported(const char *reason) {
    fprintf(stderr, "blur-startup: unsupported %s\n", reason);
    abort();
}
void range(uint32_t p, uint32_t n) {
    if (n && (p < 0x10000 || !gm_valid(p, n))) unsupported("guest memory range");
}
uint32_t errno_address(X86 *c) {
    // A separate location for each guest TEB. No host errno leaks into the ABI.
    static std::map<uint32_t, uint32_t> slots;
    uint32_t &p = slots[c->fs_base];
    if (!p || !heap_owns(p)) p = heap_alloc(4, true);
    if (!p) unsupported("errno allocation failure");
    return p;
}
void error(X86 *c, uint32_t value) { wr32(errno_address(c), value); }
void crt_errno(X86 *c) { set_eax(c, errno_address(c)); }

// Process-scoped CRT storage lives in guest memory, not in imported IAT code.
uint32_t process_word(uint32_t &slot, uint32_t initial) {
    if (!slot || !heap_owns(slot)) {
        slot = heap_alloc(4, true);
        if (!slot) unsupported("CRT global allocation");
        wr32(slot, initial);
    }
    return slot;
}
uint32_t app_type_slot, fmode_slot, commode_slot, matherr_slot;
void app_type(X86 *c) {
    if (arg(c, 0) != 1 && arg(c, 0) != 2) unsupported("CRT application type");
    wr32(process_word(app_type_slot, 0), arg(c, 0));
}
void fmode(X86 *c) { set_eax(c, process_word(fmode_slot, 0x4000)); }
void commode(X86 *c) { set_eax(c, process_word(commode_slot, 0)); }
void matherr(X86 *c) { wr32(process_word(matherr_slot, 0), arg(c, 0)); }

// New opaque pointer representation for this runtime; not the Windows cipher.
// One nonzero, process-specific cookie makes encode/decode reversible for every
// uint32_t value, including null and the CRT's all-ones table sentinel.
uint32_t pointer_cookie() {
    static const uint32_t cookie = [] {
        std::random_device entropy;
        uint32_t value = entropy();
        return value ? value : 0x6f2d9a41u;
    }();
    return cookie;
}
void pointer_code(X86 *c) { set_eax(c, arg(c, 0) ^ pointer_cookie()); }

// CRT numbered locks delegate to guest critical sections so a blocked guest
// yields the scheduler baton; host std::mutex locking here would deadlock it.
std::map<uint32_t, uint32_t> crt_locks;
uint32_t lock_address(X86 *c, uint32_t id) {
    if (id > 63) unsupported("CRT lock number");
    uint32_t &slot = crt_locks[id];
    if (!slot || !heap_owns(slot)) {
        slot = heap_alloc(24, true);
        if (!slot) unsupported("CRT lock allocation");
        const uint32_t init = imports_resolve("KERNEL32.dll", "InitializeCriticalSection");
        if (!init) unsupported("critical-section initialization service");
        guest_call(c, init, slot);
    }
    return slot;
}
void lock(X86 *c) {
    const uint32_t p = lock_address(c, arg(c, 0));
    const uint32_t fn = imports_resolve("KERNEL32.dll", "EnterCriticalSection");
    if (!fn) unsupported("guest critical-section entry service");
    guest_call(c, fn, p);
}
void unlock(X86 *c) {
    const uint32_t id = arg(c, 0);
    auto it = crt_locks.find(id);
    if (it == crt_locks.end() || !heap_owns(it->second)) unsupported("uninitialized CRT lock release");
    const uint32_t fn = imports_resolve("KERNEL32.dll", "LeaveCriticalSection");
    if (!fn) unsupported("guest critical-section release service");
    guest_call(c, fn, it->second);
}

// __dllonexit owns a caller-supplied [begin,end) array of raw guest callbacks.
// Resize transactionally: allocation failure leaves both pointers/data intact.
void dllonexit_append(X86 *c, uint32_t fn, uint32_t pb, uint32_t pe) {
    range(pb, 4); range(pe, 4);
    if (pb == pe) unsupported("aliased on-exit table pointers");
    const uint32_t begin = rd32(pb), end = rd32(pe);
    if ((!begin && end) || begin > end || ((end - begin) & 3)) unsupported("on-exit table bounds");
    const uint32_t bytes = end - begin;
    if (bytes / 4 >= 65536) unsupported("on-exit callback limit");
    if (begin && (!heap_owns(begin) || heap_size(begin) < bytes)) unsupported("on-exit table ownership");
    const uint32_t p = begin ? heap_realloc(begin, bytes + 4) : heap_alloc(4, false);
    if (!p) { error(c, 12); set_eax(c, 0); return; }
    wr32(p + bytes, fn);
    wr32(pb, p); wr32(pe, p + bytes + 4);
    set_eax(c, fn);
}
void dllonexit(X86 *c) { dllonexit_append(c, arg(c, 0), arg(c, 1), arg(c, 2)); }
uint32_t exit_begin_slot, exit_end_slot;
void onexit(X86 *c) {
    const uint32_t fn = arg(c, 0);
    const uint32_t begin = process_word(exit_begin_slot, 0), end = process_word(exit_end_slot, 0);
    const uint32_t cs = lock_address(c, 8);
    guest_call(c, imports_resolve("KERNEL32.dll", "EnterCriticalSection"), cs);
    dllonexit_append(c, fn, begin, end);
    const uint32_t result = c->r[R_EAX];
    guest_call(c, imports_resolve("KERNEL32.dll", "LeaveCriticalSection"), cs);
    set_eax(c, result);
}
// Execute registered callbacks in reverse order, popping before calling so
// callbacks may register new work without replaying the current callback.
void cexit(X86 *c) {
    const uint32_t begin_slot = process_word(exit_begin_slot, 0), end_slot = process_word(exit_end_slot, 0);
    const uint32_t cs = lock_address(c, 8);
    const uint32_t enter = imports_resolve("KERNEL32.dll", "EnterCriticalSection");
    const uint32_t leave = imports_resolve("KERNEL32.dll", "LeaveCriticalSection");
    for (unsigned budget = 0; budget < 65536; ++budget) {
        guest_call(c, enter, cs);
        const uint32_t begin = rd32(begin_slot), end = rd32(end_slot);
        if (begin == end) { guest_call(c, leave, cs); return; }
        if (begin > end || (end - begin) & 3) unsupported("exit callback range");
        const uint32_t fn = rd32(end - 4);
        wr32(end_slot, end - 4);
        guest_call(c, leave, cs);
        if (fn) guest_call(c, fn);
    }
    unsupported("exit callback budget");
}

// This bounded adapter handles the observed x86 precision-only startup request.
// SSE/exception/rounding changes are refused rather than applied to host FP state.
void controlfp(X86 *c) {
    const uint32_t out = arg(c, 0), value = arg(c, 1), mask = arg(c, 2);
    if (mask != 0 && mask != 0x30000) unsupported("_controlfp_s beyond precision-only startup");
    if (mask) {
        const uint32_t pc = value & 0x30000;
        if (pc == 0x30000) unsupported("_controlfp_s invalid precision");
        const uint16_t bits = pc == 0x20000 ? 0 : pc == 0x10000 ? 0x200 : 0x300;
        x87_set_cw(c, uint16_t((c->fpu_cw & ~0x300u) | bits));
    }
    uint32_t status = 0;
    const uint32_t em[] = {0x10, 0x80000, 8, 4, 2, 1};
    for (unsigned i = 0; i < 6; ++i) if (c->fpu_cw & (1u << i)) status |= em[i];
    status |= ((c->fpu_cw >> 10) & 3u) << 8;
    const uint32_t pc = (c->fpu_cw >> 8) & 3u;
    if (pc == 1) unsupported("reserved x87 precision");
    status |= pc == 0 ? 0x20000 : pc == 2 ? 0x10000 : 0;
    if (c->fpu_cw & 0x1000) status |= 0x40000;
    if (out) { range(out, 4); wr32(out, status); }
    set_eax(c, 0);
}

void compare_exchange(X86 *c) {
    uint32_t p = arg(c, 0), expected = arg(c, 2);
    range(p, 4);
    if (p & 3) unsupported("unaligned InterlockedCompareExchange");
    // The native pointer addresses validated guest storage, never a guest value.
    if (__atomic_compare_exchange_n(reinterpret_cast<uint32_t *>(gm_ptr(p)), &expected,
                                    arg(c, 1), false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        recomp_watch(p, 4, arg(c, 1));
    set_eax(c, expected);
}
void open_mutex(X86 *c) { open_mutex_named(c, gm_str(arg(c, 2))); }

// Walk [first,last), skip null entries, and stop on the first error for _e.
// guest_call preserves the shim's stack while invoking translated callbacks.
void initializers(X86 *c, bool errors) {
    const uint32_t first = arg(c, 0), last = arg(c, 1);
    if (first > last || (first & 3) || (last & 3)) unsupported("initializer table bounds");
    range(first, last - first);
    if ((last - first) / 4 > 65536) unsupported("initializer table limit");
    uint32_t result = 0;
    uint32_t calls = 0;
    fprintf(stderr, "blur-startup: %s table %08x..%08x\n", errors ? "_initterm_e" : "_initterm", first, last);
    for (uint32_t p = first; p < last; p += 4) {
        const uint32_t fn = rd32(p);
        if (!fn) continue;
        if (calls == 0) fprintf(stderr, "blur-startup: first initializer %08x\n", fn);
        ++calls;
        result = guest_call(c, fn);
        if (errors && result) break;
    }
    fprintf(stderr, "blur-startup: %s completed %u callbacks, last result %08x\n",
            errors ? "_initterm_e" : "_initterm", calls, result);
    if (errors) set_eax(c, result);
}
void initterm(X86 *c) { initializers(c, false); }
void initterm_e(X86 *c) { initializers(c, true); }

// Preserve the kit's surface-dirty spans and optional byte watchpoints even
// when a bulk host memory operation performs the write.
void written(uint32_t p, uint32_t n) {
    if (!n) return;
    recomp_dirty(p, n);
    if (g_watch_len) {
        const uint64_t first = std::max(uint64_t(p), uint64_t(g_watch_base));
        const uint64_t last = std::min(uint64_t(p) + n, uint64_t(g_watch_base) + g_watch_len);
        for (uint64_t a = first; a < last; ++a)
            recomp_watch_hit(uint32_t(a), 1, rd8(uint32_t(a)));
    }
}

void copy(X86 *c) {
    const uint32_t dst = arg(c, 0), src = arg(c, 1), n = arg(c, 2);
    range(dst, n); range(src, n);
    // memcpy overlap is undefined: refuse it rather than silently becoming memmove.
    if (n && uint64_t(dst) < uint64_t(src) + n && uint64_t(src) < uint64_t(dst) + n)
        unsupported("overlapping memcpy");
    if (n) memcpy(gm_ptr(dst), gm_ptr(src), n);
    written(dst, n);
    set_eax(c, dst);
}
void move(X86 *c) {
    const uint32_t dst = arg(c, 0), src = arg(c, 1), n = arg(c, 2);
    range(dst, n); range(src, n);
    if (n) memmove(gm_ptr(dst), gm_ptr(src), n);
    written(dst, n);
    set_eax(c, dst);
}
void fill(X86 *c) {
    const uint32_t dst = arg(c, 0), n = arg(c, 2);
    range(dst, n);
    if (n) memset(gm_ptr(dst), arg(c, 1) & 255, n);
    written(dst, n);
    set_eax(c, dst);
}
uint32_t allocate(X86 *c, uint32_t size, bool zero, uint32_t align = 16) {
    // Provision per-thread error storage before a request can exhaust the heap.
    errno_address(c);
    uint32_t p = 0;
    if (size <= HEAP_LIMIT - HEAP_BASE && align <= GUEST_SIZE)
        p = heap_alloc(std::max(size, 1u), zero, std::max(align, 16u));
    if (!p) error(c, 12); // VC errno ENOMEM
    return p;
}
void malloc_(X86 *c) { set_eax(c, allocate(c, arg(c, 0), false)); }
void calloc_(X86 *c) {
    const uint64_t n = uint64_t(arg(c, 0)) * arg(c, 1);
    if (n > UINT32_MAX) { error(c, 12); set_eax(c, 0); return; }
    set_eax(c, allocate(c, uint32_t(n), true));
}
void free_(X86 *c) {
    if (arg(c, 0) && !heap_free(arg(c, 0))) unsupported("invalid free");
}
void realloc_(X86 *c) {
    errno_address(c);
    const uint32_t p = arg(c, 0), n = arg(c, 1);
    if (!p) { set_eax(c, allocate(c, n, false)); return; }
    if (!n) { free_(c); set_eax(c, 0); return; }
    if (!heap_owns(p)) unsupported("invalid realloc");
    uint32_t result = n <= HEAP_LIMIT - HEAP_BASE ? heap_realloc(p, n) : 0;
    if (!result) error(c, 12);
    set_eax(c, result);
}
void aligned_malloc(X86 *c) {
    uint32_t n = arg(c, 0), align = arg(c, 1);
    if (!n || !align || (align & (align - 1)))
        unsupported("_aligned_malloc invalid-parameter handler (not implemented)");
    set_eax(c, allocate(c, n, false, align));
}
void msize(X86 *c) {
    if (!heap_owns(arg(c, 0))) unsupported("_msize invalid-parameter handler");
    set_eax(c, heap_size(arg(c, 0)));
}
const ImportShim shims[] = {
#ifndef BLUR_USE_AUX_MSVCR90
    {"MSVCR90.dll", "__set_app_type", ARGC_CDECL, app_type},
    {"MSVCR90.dll", "__p__fmode", ARGC_CDECL, fmode},
    {"MSVCR90.dll", "__p__commode", ARGC_CDECL, commode},
    {"MSVCR90.dll", "__setusermatherr", ARGC_CDECL, matherr},
    {"MSVCR90.dll", "_encode_pointer", ARGC_CDECL, pointer_code},
    {"MSVCR90.dll", "_decode_pointer", ARGC_CDECL, pointer_code},
    {"MSVCR90.dll", "_lock", ARGC_CDECL, lock},
    {"MSVCR90.dll", "_unlock", ARGC_CDECL, unlock},
    {"MSVCR90.dll", "__dllonexit", ARGC_CDECL, dllonexit},
    {"MSVCR90.dll", "_onexit", ARGC_CDECL, onexit},
    {"MSVCR90.dll", "_cexit", ARGC_CDECL, cexit},
    {"MSVCR90.dll", "_controlfp_s", ARGC_CDECL, controlfp},
#endif
    {"KERNEL32.dll", "InterlockedCompareExchange", 3, compare_exchange},
    {"KERNEL32.dll", "OpenMutexA", 3, open_mutex},
#ifndef BLUR_USE_AUX_MSVCR90
    {"MSVCR90.dll", "_initterm", ARGC_CDECL, initterm},
    {"MSVCR90.dll", "_initterm_e", ARGC_CDECL, initterm_e},
    {"MSVCR90.dll", "memcpy", ARGC_CDECL, copy},
    {"MSVCR90.dll", "memmove", ARGC_CDECL, move},
    {"MSVCR90.dll", "memset", ARGC_CDECL, fill},
    {"MSVCR90.dll", "malloc", ARGC_CDECL, malloc_},
    {"MSVCR90.dll", "calloc", ARGC_CDECL, calloc_},
    {"MSVCR90.dll", "_calloc_crt", ARGC_CDECL, calloc_},
    {"MSVCR90.dll", "realloc", ARGC_CDECL, realloc_},
    {"MSVCR90.dll", "free", ARGC_CDECL, free_},
    {"MSVCR90.dll", "_aligned_malloc", ARGC_CDECL, aligned_malloc},
    {"MSVCR90.dll", "_aligned_free", ARGC_CDECL, free_},
    {"MSVCR90.dll", "_msize", ARGC_CDECL, msize},
    {"MSVCR90.dll", "_errno", ARGC_CDECL, crt_errno},
#endif
};
struct Register {
    Register() {
        imports_register(shims, sizeof(shims) / sizeof(shims[0]));
#ifndef BLUR_USE_AUX_MSVCR90
        // Obsolete Pentium FDIV workaround flag: this guest CPU has no FDIV bug.
        imports_register_data("MSVCR90.dll", "_adjust_fdiv", 4);
#endif
    }
} registration;
}

#ifdef BLUR_USE_AUX_MSVCR90
namespace blur_init_thunks {

// Helper to push a dword to guest stack
static inline void push32(X86 *c, uint32_t val) {
    c->r[4] -= 4;
    wr32(c->r[4], val);
}

// 0x00e08710
// push 0; push 0x46b400; push 0xe78558; mov ecx, 0x112bfc8; call 0x4f6de0; push 0xe43a20; call 0xd44031; pop ecx; ret
static void init_00e08710(X86 *c) {
    push32(c, 0);
    push32(c, 0x0046b400);
    push32(c, 0x00e78558);
    c->r[1] = 0x0112bfc8;
    push32(c, 0x00e08726);
    recomp_call(c, 0x004f6de0);
    push32(c, 0x00e43a20);
    push32(c, 0x00e08730);
    recomp_call(c, 0x00d44031);
    c->r[1] = rd32(c->r[4]); c->r[4] += 4;
    c->eip = rd32(c->r[4]); c->r[4] += 4;
    recomp_return(c);
}

// 0x00e08b00
// push 0; push 0x46b790; push 0xe78720; mov ecx, 0x112c280; call 0x4f6de0; push 0xe43b10; call 0xd44031; pop ecx; ret
static void init_00e08b00(X86 *c) {
    push32(c, 0);
    push32(c, 0x0046b790);
    push32(c, 0x00e78720);
    c->r[1] = 0x0112c280;
    push32(c, 0x00e08b16);
    recomp_call(c, 0x004f6de0);
    push32(c, 0x00e43b10);
    push32(c, 0x00e08b20);
    recomp_call(c, 0x00d44031);
    c->r[1] = rd32(c->r[4]); c->r[4] += 4;
    c->eip = rd32(c->r[4]); c->r[4] += 4;
    recomp_return(c);
}

// 0x00e09ca0
// fld [0xe79610]; push ecx; fstp [esp]; push 0xe795f0; mov ecx, 0x112d294; call 0x4f6ba0; push 0xe44010; call 0xd44031; pop ecx; ret
static void init_00e09ca0(X86 *c) {
    push32(c, rd32(0x00e79610));
    push32(c, 0x00e795f0);
    c->r[1] = 0x0112d294;
    push32(c, 0x00e09cb9);
    recomp_call(c, 0x004f6ba0);
    push32(c, 0x00e44010);
    push32(c, 0x00e09cc3);
    recomp_call(c, 0x00d44031);
    c->r[1] = rd32(c->r[4]); c->r[4] += 4;
    c->eip = rd32(c->r[4]); c->r[4] += 4;
    recomp_return(c);
}

// 0x00e09e10
// fld [0xe63c94]; push ecx; fstp [esp]; push 0xe796f8; mov ecx, 0x112d42c; call 0x4f6ba0; push 0xe44080; call 0xd44031; pop ecx; ret
static void init_00e09e10(X86 *c) {
    push32(c, rd32(0x00e63c94));
    push32(c, 0x00e796f8);
    c->r[1] = 0x0112d42c;
    push32(c, 0x00e09e29);
    recomp_call(c, 0x004f6ba0);
    push32(c, 0x00e44080);
    push32(c, 0x00e09e33);
    recomp_call(c, 0x00d44031);
    c->r[1] = rd32(c->r[4]); c->r[4] += 4;
    c->eip = rd32(c->r[4]); c->r[4] += 4;
    recomp_return(c);
}

// 0x00e0e980
// push 1; push 0xe8220c; mov ecx, 0x1134d50; call 0x4f6a90; push 0xe454c0; call 0xd44031; pop ecx; ret
static void init_00e0e980(X86 *c) {
    push32(c, 1);
    push32(c, 0x00e8220c);
    c->r[1] = 0x01134d50;
    push32(c, 0x00e0e991);
    recomp_call(c, 0x004f6a90);
    push32(c, 0x00e454c0);
    push32(c, 0x00e0e99b);
    recomp_call(c, 0x00d44031);
    c->r[1] = rd32(c->r[4]); c->r[4] += 4;
    c->eip = rd32(c->r[4]); c->r[4] += 4;
    recomp_return(c);
}

// 0x00e18120
static void init_00e18120(X86 *c) {
    c->r[4] -= 0x14;
    uint32_t obj1 = c->r[4];
    uint32_t obj2 = c->r[4] + 8;
    push32(c, rd32(0x00e64898));
    c->r[1] = obj1;
    push32(c, 0x00e18136);
    recomp_call(c, 0x004f4bb0);
    c->r[4] -= 8;
    wr32(c->r[4] + 4, rd32(0x00e647a4));
    wr32(c->r[4], rd32(0x00e7a5f0));
    c->r[1] = obj2;
    push32(c, 0x00e18155);
    recomp_call(c, 0x004f4b20);
    push32(c, obj1);
    push32(c, obj2);
    push32(c, rd32(0x00e63bf8));
    push32(c, 0x00e91424);
    c->r[1] = 0x01148a2c;
    push32(c, 0x00e18177);
    recomp_call(c, 0x004f6be0);
    push32(c, 0x00e47990);
    push32(c, 0x00e18181);
    recomp_call(c, 0x00d44031);
    c->r[4] += 0x18;
    c->eip = rd32(c->r[4]); c->r[4] += 4;
    recomp_return(c);
}

// 0x00e187c0
// movss xmm0, [0x10285c4]; movss [0x114afbc], xmm0; ret
static void init_00e187c0(X86 *c) {
    wr32(0x0114afbc, rd32(0x010285c4));
    c->eip = rd32(c->r[4]); c->r[4] += 4;
    recomp_return(c);
}

// 0x00e20400
// movss xmm0, [0x10285c4]; movss [0x1155d80], xmm0; ret
static void init_00e20400(X86 *c) {
    wr32(0x01155d80, rd32(0x010285c4));
    c->eip = rd32(c->r[4]); c->r[4] += 4;
    recomp_return(c);
}

// 0x00e20c90
static void init_00e20c90(X86 *c) {
    c->r[4] -= 0x14;
    uint32_t obj1 = c->r[4];
    uint32_t obj2 = c->r[4] + 8;
    push32(c, rd32(0x00e638a0));
    c->r[1] = obj1;
    push32(c, 0x00e20ca6);
    recomp_call(c, 0x004f4bb0);
    uint32_t x0 = rd32(0x00e83314) ^ rd32(0x00e63c40);
    c->r[4] -= 8;
    wr32(c->r[4] + 4, rd32(0x00e83314));
    wr32(c->r[4], x0);
    c->r[1] = obj2;
    push32(c, 0x00e20cd0);
    recomp_call(c, 0x004f4b20);
    push32(c, obj1);
    push32(c, obj2);
    push32(c, 0); // fldz
    push32(c, 0x00e9c3a0);
    c->r[1] = 0x0115849c;
    push32(c, 0x00e20cee);
    recomp_call(c, 0x004f6be0);
    push32(c, 0x00e4a790);
    push32(c, 0x00e20cf8);
    recomp_call(c, 0x00d44031);
    c->r[4] += 0x18;
    c->eip = rd32(c->r[4]); c->r[4] += 4;
    recomp_return(c);
}

// 0x00e26850
static void init_00e26850(X86 *c) {
    c->r[4] -= 0x14;
    uint32_t obj1 = c->r[4];
    uint32_t obj2 = c->r[4] + 8;
    push32(c, rd32(0x00e638a0));
    c->r[1] = obj1;
    push32(c, 0x00e26866);
    recomp_call(c, 0x004f4bb0);
    c->r[4] -= 8;
    wr32(c->r[4] + 4, 0x3f800000u); // fld1 (1.0f)
    wr32(c->r[4], 0);                // fldz (0.0f)
    c->r[1] = obj2;
    push32(c, 0x00e2687d);
    recomp_call(c, 0x004f4b20);
    push32(c, obj1);
    push32(c, obj2);
    push32(c, 0); // fldz
    push32(c, 0x00ea0014);
    c->r[1] = 0x0115e078;
    push32(c, 0x00e2689b);
    recomp_call(c, 0x004f6be0);
    push32(c, 0x00e4bbc0);
    push32(c, 0x00e268a5);
    recomp_call(c, 0xd44031);
    c->r[4] += 0x18;
    c->eip = rd32(c->r[4]); c->r[4] += 4;
    recomp_return(c);
}

// 0x00e28100
// movss xmm0, [0x10285c4]; movss [0x1163b80], xmm0; ret
static void init_00e28100(X86 *c) {
    wr32(0x01163b80, rd32(0x010285c4));
    c->eip = rd32(c->r[4]); c->r[4] += 4;
    recomp_return(c);
}

// 0x00e30800
// push 0x856270; push 0x856260; push 0x65593014; mov ecx, 0x11b0f84; call 0x523040; push 0xe4e310; call 0xd44031; pop ecx; ret
static void init_00e30800(X86 *c) {
    push32(c, 0x00856270);
    push32(c, 0x00856260);
    push32(c, 0x65593014);
    c->r[1] = 0x011b0f84;
    push32(c, 0x00e30819);
    recomp_call(c, 0x00523040);
    push32(c, 0x00e4e310);
    push32(c, 0x00e30823);
    recomp_call(c, 0x00d44031);
    c->r[1] = rd32(c->r[4]); c->r[4] += 4;
    c->eip = rd32(c->r[4]); c->r[4] += 4;
    recomp_return(c);
}

// 0x00e40000
// movss xmm0, [0x10285c0]; movss [0x124bd90], xmm0; ret
static void init_00e40000(X86 *c) {
    wr32(0x0124bd90, rd32(0x010285c0));
    c->eip = rd32(c->r[4]); c->r[4] += 4;
    recomp_return(c);
}

// 0x00e40020
// movss xmm0, [0x10285c4]; movss [0x124bd8c], xmm0; ret
static void init_00e40020(X86 *c) {
    wr32(0x0124bd8c, rd32(0x010285c4));
    c->eip = rd32(c->r[4]); c->r[4] += 4;
    recomp_return(c);
}

// 0x00e400ea
// mov ecx, 0x124d448; call 0xd43846; push 0xe525d7; call 0xd44031; pop ecx; ret
static void init_00e400ea(X86 *c) {
    c->r[1] = 0x0124d448;
    push32(c, 0x00e400f4);
    recomp_call(c, 0x00d43846);
    push32(c, 0x00e525d7);
    push32(c, 0x00e400fe);
    recomp_call(c, 0x00d44031);
    c->r[1] = rd32(c->r[4]); c->r[4] += 4;
    c->eip = rd32(c->r[4]); c->r[4] += 4;
    recomp_return(c);
}

static const uint32_t kBlurInitAddrs[15] = {
    0x00e08710, 0x00e08b00, 0x00e09ca0, 0x00e09e10, 0x00e0e980,
    0x00e18120, 0x00e187c0, 0x00e20400, 0x00e20c90, 0x00e26850,
    0x00e28100, 0x00e30800, 0x00e40000, 0x00e40020, 0x00e400ea
};

static void (*const kBlurInitPtrs[15])(X86 *c) = {
    init_00e08710, init_00e08b00, init_00e09ca0, init_00e09e10, init_00e0e980,
    init_00e18120, init_00e187c0, init_00e20400, init_00e20c90, init_00e26850,
    init_00e28100, init_00e30800, init_00e40000, init_00e40020, init_00e400ea
};

static uint8_t kBlurInitHooked[15] = {0};
static RecompHookFn kBlurInitHookPtrs[15] = {nullptr};

static const char *const kBlurInitNames[15] = {
    "blur_init_00e08710", "blur_init_00e08b00", "blur_init_00e09ca0", "blur_init_00e09e10", "blur_init_00e0e980",
    "blur_init_00e18120", "blur_init_00e187c0", "blur_init_00e20400", "blur_init_00e20c90", "blur_init_00e26850",
    "blur_init_00e28100", "blur_init_00e30800", "blur_init_00e40000", "blur_init_00e40020", "blur_init_00e400ea"
};

static const RecompModule kBlurInitModule = {
    "blur_extra_initializers",
    0x00e08710,
    0x00e400eb,
    kBlurInitAddrs,
    15,
    kBlurInitPtrs,
    kBlurInitHookPtrs,
    kBlurInitHooked,
    nullptr,
    0,
    kBlurInitNames
};

struct BlurInitRegister {
    BlurInitRegister() {
        recomp_module_register(&kBlurInitModule);
    }
} s_blur_init_reg;

} // namespace blur_init_thunks
#endif
