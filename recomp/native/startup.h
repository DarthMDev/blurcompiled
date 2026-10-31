/* Native overrides and extensions for Blur Native. */
#pragma once
#include <math.h>
#include <stdio.h>
#include "runtime/x86.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void recomp_ret(X86 *c) {
    c->eip = rd32(c->r[4]);
    c->r[4] += 4u;
    recomp_return(c);
}

static inline void native_libm_sse2_acos(X86 *c)  { xmm_set_f64(c, 0, acos(xmm_f64(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_acosf(X86 *c) { xmm_set_f32(c, 0, acosf(xmm_f32(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_asin(X86 *c)  { xmm_set_f64(c, 0, asin(xmm_f64(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_asinf(X86 *c) { xmm_set_f32(c, 0, asinf(xmm_f32(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_atan(X86 *c)  { xmm_set_f64(c, 0, atan(xmm_f64(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_atan2(X86 *c) { xmm_set_f64(c, 0, atan2(xmm_f64(c, 0), xmm_f64(c, 1))); recomp_ret(c); }
static inline void native_libm_sse2_atanf(X86 *c) { xmm_set_f32(c, 0, atanf(xmm_f32(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_cos(X86 *c)   { xmm_set_f64(c, 0, cos(xmm_f64(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_cosf(X86 *c)  { xmm_set_f32(c, 0, cosf(xmm_f32(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_exp(X86 *c)   { xmm_set_f64(c, 0, exp(xmm_f64(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_expf(X86 *c)  { xmm_set_f32(c, 0, expf(xmm_f32(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_log(X86 *c)   { xmm_set_f64(c, 0, log(xmm_f64(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_log10(X86 *c) { xmm_set_f64(c, 0, log10(xmm_f64(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_log10f(X86 *c){ xmm_set_f32(c, 0, log10f(xmm_f32(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_logf(X86 *c)  { xmm_set_f32(c, 0, logf(xmm_f32(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_pow(X86 *c)   { xmm_set_f64(c, 0, pow(xmm_f64(c, 0), xmm_f64(c, 1))); recomp_ret(c); }
static inline void native_libm_sse2_sin(X86 *c)   { xmm_set_f64(c, 0, sin(xmm_f64(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_sinf(X86 *c)  { xmm_set_f32(c, 0, sinf(xmm_f32(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_tan(X86 *c)   { xmm_set_f64(c, 0, tan(xmm_f64(c, 0))); recomp_ret(c); }
static inline void native_libm_sse2_tanf(X86 *c)  { xmm_set_f32(c, 0, tanf(xmm_f32(c, 0))); recomp_ret(c); }

static inline void native_blur_init_00e09a10(X86 *c) {
    xmm_set_f64(c, 0, rdf64(0x00e79398u));
    xmm_set_f64(c, 0, cos(xmm_f64(c, 0)));
    xmm_set_f32(c, 0, (float)xmm_f64(c, 0));
    wrf32(0x0112cfc8u, xmm_f32(c, 0));
    recomp_ret(c);
}

extern void sched_checkpoint(void);
extern void fn_00ae9390(X86 *c);
extern void fn_00ae5d20(X86 *c);

extern uint32_t g_last_valid_G;

static inline uint32_t check_and_fix_lua_G(uint32_t L) {
    if (L < 0x10000u || L >= GUEST_SIZE) return 0;
    uint32_t G = rd32(L + 0x10);
    if (G >= 0x10000u && G < GUEST_SIZE && rd32(G + 0x0c) == 0x00af4690u) {
        g_last_valid_G = G;
        return G;
    }
    // Attempt recovery of global_State
    if (L + 0x78 + 0x0c < GUEST_SIZE && rd32(L + 0x78 + 0x0c) == 0x00af4690u) {
        G = L + 0x78;
        wr32(L + 0x10, G);
        g_last_valid_G = G;
        return G;
    }
    if (G >= 0x10010u && G < GUEST_SIZE && rd32((G - 0x10) + 0x0c) == 0x00af4690u) {
        G = G - 0x10;
        wr32(L + 0x10, G);
        g_last_valid_G = G;
        return G;
    }
    if (g_last_valid_G >= 0x10000u && g_last_valid_G < GUEST_SIZE && rd32(g_last_valid_G + 0x0c) == 0x00af4690u) {
        wr32(L + 0x10, g_last_valid_G);
        return g_last_valid_G;
    }
    if (rd32(0x05824158u + 0x0c) == 0x00af4690u) {
        wr32(L + 0x10, 0x05824158u);
        g_last_valid_G = 0x05824158u;
        return 0x05824158u;
    }
    if (rd32(0x05822d18u + 0x0c) == 0x00af4690u) {
        wr32(L + 0x10, 0x05822d18u);
        g_last_valid_G = 0x05822d18u;
        return 0x05822d18u;
    }
    if (L >= 0x05800000u && L < 0x06000000u) {
        G = L + 0x78;
        wr32(L + 0x10, G);
        wr32(G + 0x0c, 0x00af4690u);
        g_last_valid_G = G;
        return G;
    }
    return 0;
}

static inline void native_luaC_singlestep(X86 *c) {
    static uint32_t s_gc_steps = 0;
    s_gc_steps++;
    uint32_t L = c->r[0];
    uint32_t G = check_and_fix_lua_G(L);
    if (G >= 0x10000u) {
        uint8_t gcstate = rd8(G + 0x15);
        if (gcstate > 4) {
            static uint32_t s_invalid_warn = 0;
            if (s_invalid_warn++ < 5) {
                fprintf(stderr, "[lua_gc] INVALID gcstate=%u at G=%08x (L=%08x), resetting to GCSpause (0)!\n",
                        (unsigned)gcstate, G, L);
            }
            wr8(G + 0x15, 0);
            wr32(G + 0x4c, 0);
            c->r[7] = 0;
        }
    }
    fn_00ae9390(c);
    sched_checkpoint();
    if (s_gc_steps <= 10 || (s_gc_steps % 10000 == 0)) {
        uint8_t gcstate = (G >= 0x10000u) ? rd8(G + 0x15) : 255;
        uint32_t totalbytes = (G >= 0x10000u) ? rd32(G + 0x44) : 0;
        uint32_t threshold = (G >= 0x10000u) ? rd32(G + 0x40) : 0;
        fprintf(stderr, "[lua_gc] step=%u L=%08x lim=%d step_ret=%u gcstate=%u total=%u thresh=%u\n",
                s_gc_steps, L, (int)c->r[7], c->r[0], (unsigned)gcstate, totalbytes, threshold);
    }
}

static inline void native_luaM_realloc(X86 *c) {
    uint32_t L = rd32(c->r[4] + 4);
    check_and_fix_lua_G(L);
    fn_00ae5d20(c);
}

extern void fn_00847000(X86 *c);
extern void fn_008472d0(X86 *c);

static inline void native_mutex_lock(X86 *c) {
    uint32_t ctx = c->r[1];
    static uint32_t s_lock_cnt = 0;
    uint32_t lock_obj = (ctx >= 0x10000u) ? rd32(ctx + 0x0c) : 0;
    uint32_t owner = (lock_obj >= 0x10000u) ? rd32(lock_obj) : 0;
    uint32_t count = (lock_obj >= 0x10000u) ? rd32(lock_obj + 0x08) : 0;
    uint32_t tid = (c->fs_base >= 0x10000u) ? rd32(c->fs_base + 0x24) : 0;
    if (s_lock_cnt < 15 || (s_lock_cnt % 20000 == 0)) {
        fprintf(stderr, "[mutex_lock #%u] tid=%u (fs=%08x) ctx=%08x obj=%08x owner=%u count=%u\n",
                s_lock_cnt, tid, c->fs_base, ctx, lock_obj, owner, count);
    }
    s_lock_cnt++;
    fn_00847000(c);
}

static inline void native_mutex_unlock(X86 *c) {
    uint32_t ctx = c->r[1];
    static uint32_t s_unlock_cnt = 0;
    uint32_t lock_obj = (ctx >= 0x10000u) ? rd32(ctx + 0x14) : 0;
    uint32_t owner = (lock_obj >= 0x10000u) ? rd32(lock_obj) : 0;
    uint32_t count = (lock_obj >= 0x10000u) ? rd32(lock_obj + 0x08) : 0;
    uint32_t tid = (c->fs_base >= 0x10000u) ? rd32(c->fs_base + 0x24) : 0;
    if (s_unlock_cnt < 15 || (s_unlock_cnt % 20000 == 0)) {
        fprintf(stderr, "[mutex_unlock #%u] tid=%u (fs=%08x) ctx=%08x obj=%08x owner=%u count=%u\n",
                s_unlock_cnt, tid, c->fs_base, ctx, lock_obj, owner, count);
    }
    s_unlock_cnt++;
    fn_008472d0(c);
}

static inline void native_memcpy(X86 *c) {
    uint32_t dest = rd32(c->r[4] + 4);
    uint32_t src  = rd32(c->r[4] + 8);
    uint32_t count = rd32(c->r[4] + 12);
    if (count > 0 && dest && src) {
        memmove(g_mem + dest, g_mem + src, count);
    }
    c->r[0] = dest;
    recomp_ret(c);
}

static inline void native_memset(X86 *c) {
    uint32_t dest = rd32(c->r[4] + 4);
    uint32_t val  = rd32(c->r[4] + 8);
    uint32_t count = rd32(c->r[4] + 12);
    if (count > 0 && dest) {
        memset(g_mem + dest, (uint8_t)val, count);
    }
    c->r[0] = dest;
    recomp_ret(c);
}

extern void fn_00ae5d20(X86 *c);

static inline void native_luaS_resize(X86 *c) {
    uint32_t saved_ebx = c->r[3];
    uint32_t saved_ebp = c->r[5];
    uint32_t saved_esi = c->r[6];
    uint32_t saved_edi = c->r[7];

    uint32_t L = rd32(c->r[4] + 4);
    uint32_t newsize = rd32(c->r[4] + 8);
    uint32_t G = check_and_fix_lua_G(L);
    if (G < 0x10000u) {
        c->r[3] = saved_ebx; c->r[5] = saved_ebp; c->r[6] = saved_esi; c->r[7] = saved_edi;
        recomp_ret(c);
        return;
    }

    // In Lua 5.1, cannot resize while sweeping strings
    uint8_t gcstate = rd8(G + 0x15);
    if (gcstate == 2) { // GCSsweepstring
        c->r[3] = saved_ebx; c->r[5] = saved_ebp; c->r[6] = saved_esi; c->r[7] = saved_edi;
        recomp_ret(c);
        return;
    }

    // Ensure newsize is a clean power of 2 >= 32 and <= 65536
    if (newsize < 32) newsize = 32;
    if (newsize > 65536) newsize = 65536;
    if ((newsize & (newsize - 1)) != 0) {
        uint32_t p = 32;
        while (p < newsize) p <<= 1;
        newsize = p;
    }

    uint32_t old_hash = rd32(G);
    uint32_t old_size = rd32(G + 8);

    // Allocate newhash: luaM_realloc_(L, NULL, 0, newsize * 4)
    c->r[4] -= 4; wr32(c->r[4], newsize * 4u);
    c->r[4] -= 4; wr32(c->r[4], 0);
    c->r[4] -= 4; wr32(c->r[4], 0);
    c->r[4] -= 4; wr32(c->r[4], L);
    c->r[4] -= 4; wr32(c->r[4], 0xae5e37u);
    fn_00ae5d20(c);
    c->r[4] += 16;
    uint32_t newhash = c->r[0];
    if (!newhash) {
        c->r[3] = saved_ebx; c->r[5] = saved_ebp; c->r[6] = saved_esi; c->r[7] = saved_edi;
        recomp_ret(c);
        return;
    }

    memset(g_mem + newhash, 0, newsize * 4u);
    uint32_t mask = newsize - 1;

    // Rehash old table if valid
    if (old_hash >= 0x10000u && old_size > 0 && old_size <= 1000000) {
        for (uint32_t i = 0; i < old_size; i++) {
            uint32_t p = rd32(old_hash + i * 4);
            while (p >= 0x10000u && p < GUEST_SIZE) {
                uint32_t next = rd32(p); // p->next
                uint32_t h = rd32(p + 8); // ts->tsv.hash
                uint32_t h1 = h & mask;
                wr32(p, rd32(newhash + h1 * 4));
                wr32(newhash + h1 * 4, p);
                p = next;
            }
        }
        // Free old_hash: luaM_realloc_(L, old_hash, old_size * 4, 0)
        c->r[4] -= 4; wr32(c->r[4], 0);
        c->r[4] -= 4; wr32(c->r[4], old_size * 4u);
        c->r[4] -= 4; wr32(c->r[4], old_hash);
        c->r[4] -= 4; wr32(c->r[4], L);
        c->r[4] -= 4; wr32(c->r[4], 0xae5eafu);
        fn_00ae5d20(c);
        c->r[4] += 16;
    }

    wr32(G, newhash);
    wr32(G + 8, newsize);
    c->r[3] = saved_ebx; c->r[5] = saved_ebp; c->r[6] = saved_esi; c->r[7] = saved_edi;
    recomp_ret(c);
}

static inline void native_00ae5ec0(X86 *c) {
    uint32_t L = rd32(c->r[4] + 4);
    uint32_t str_ptr = rd32(c->r[4] + 8);
    uint32_t h = rd32(c->r[4] + 12);
    uint32_t l = c->r[7];
    uint32_t G = check_and_fix_lua_G(L);

    uint32_t saved_ebx = c->r[3];
    uint32_t saved_ebp = c->r[5];
    uint32_t saved_esi = c->r[6];
    uint32_t saved_edi = c->r[7];

    static uint32_t s_str_cnt = 0;
    s_str_cnt++;

    // Allocate string: luaM_realloc_(L, NULL, 0, l + 0x11)
    check_and_fix_lua_G(L);
    uint32_t nsize = l + 0x11u;
    c->r[4] -= 4; wr32(c->r[4], nsize);
    c->r[4] -= 4; wr32(c->r[4], 0);
    c->r[4] -= 4; wr32(c->r[4], 0);
    c->r[4] -= 4; wr32(c->r[4], L);
    c->r[4] -= 4; wr32(c->r[4], 0xae5eeau);
    fn_00ae5d20(c);
    c->r[4] += 16;

    uint32_t ts = c->r[0];
    if (!ts) {
        c->r[3] = saved_ebx;
        c->r[5] = saved_ebp;
        c->r[6] = saved_esi;
        c->r[7] = saved_edi;
        c->r[0] = 0;
        recomp_ret(c);
        return;
    }

    wr32(ts + 0x0c, l);
    wr32(ts + 0x08, h);
    uint8_t currentwhite = (G >= 0x10000u) ? rd8(G + 0x14) : 0;
    wr8(ts + 0x05, currentwhite & 3);
    wr8(ts + 0x04, 4); // LUA_TSTRING
    wr8(ts + 0x06, 0); // reserved

    if (l > 0 && str_ptr) {
        memmove(g_mem + ts + 0x10, g_mem + str_ptr, l);
    }
    wr8(ts + 0x10 + l, 0); // null terminate

    // Insert into string hash table
    if (G >= 0x10000u) {
        uint32_t strt_hash = rd32(G);
        uint32_t strt_size = rd32(G + 0x8);
        if (strt_size < 32 || strt_size > 65536 || (strt_size & (strt_size - 1)) != 0 || strt_hash < 0x10000u) {
            uint32_t target_size = (strt_size >= 32 && strt_size <= 65536) ? strt_size : 64;
            uint32_t p = 32;
            while (p < target_size) p <<= 1;
            target_size = (p <= 65536) ? p : 65536;
            c->r[4] -= 4; wr32(c->r[4], target_size);
            c->r[4] -= 4; wr32(c->r[4], L);
            c->r[4] -= 4; wr32(c->r[4], 0xae5f4eu);
            native_luaS_resize(c);
            c->r[4] += 8;
            strt_hash = rd32(G);
            strt_size = rd32(G + 0x8);
        }

        uint32_t mask = (strt_size >= 32) ? (strt_size - 1) : 31;
        uint32_t idx = h & mask;
        uint32_t old_head = (strt_hash >= 0x10000u) ? rd32(strt_hash + idx * 4) : 0;
        wr32(ts, old_head);
        if (strt_hash >= 0x10000u) {
            wr32(strt_hash + idx * 4, ts);
        }

        uint32_t nuse = rd32(G + 0x4) + 1;
        wr32(G + 0x4, nuse);

        if (nuse > strt_size && strt_size <= 32768) {
            c->r[4] -= 4; wr32(c->r[4], strt_size * 2);
            c->r[4] -= 4; wr32(c->r[4], L);
            c->r[4] -= 4; wr32(c->r[4], 0xae5f4eu);
            native_luaS_resize(c);
            c->r[4] += 8;
        }
    }

    c->r[3] = saved_ebx;
    c->r[5] = saved_ebp;
    c->r[6] = saved_esi;
    c->r[7] = saved_edi;
    c->r[0] = ts;
    recomp_ret(c);
}

static inline void native_luaS_newlstr(X86 *c) {
    uint32_t saved_ebx = c->r[3];
    uint32_t saved_ebp = c->r[5];
    uint32_t saved_esi = c->r[6];
    uint32_t saved_edi = c->r[7];

    uint32_t L = rd32(c->r[4] + 4);
    uint32_t str_ptr = rd32(c->r[4] + 8);
    uint32_t l = rd32(c->r[4] + 12);
    uint32_t G = check_and_fix_lua_G(L);

    // Compute hash
    uint32_t h = l;
    uint32_t step = (l >> 5) + 1;
    for (uint32_t l1 = l; l1 >= step; l1 -= step) {
        uint8_t ch = rd8(str_ptr + l1 - 1);
        h = h ^ ((h << 5) + (h >> 2) + ch);
    }

    // Search existing interned strings
    if (G >= 0x10000u) {
        uint32_t strt_hash = rd32(G);
        uint32_t strt_size = rd32(G + 8);
        if (strt_size >= 32 && (strt_size & (strt_size - 1)) == 0 && strt_hash >= 0x10000u && strt_hash < GUEST_SIZE) {
            uint32_t idx = h & (strt_size - 1);
            uint32_t o = rd32(strt_hash + idx * 4);
            while (o >= 0x10000u && o < GUEST_SIZE) {
                uint32_t ts_len = rd32(o + 0x0c);
                if (ts_len == l && (l == 0 || memcmp(g_mem + str_ptr, g_mem + o + 0x10, l) == 0)) {
                    // String already interned!
                    uint8_t currentwhite = rd8(G + 0x14);
                    wr8(o + 5, currentwhite & 3);
                    c->r[0] = o;
                    c->r[3] = saved_ebx;
                    c->r[5] = saved_ebp;
                    c->r[6] = saved_esi;
                    c->r[7] = saved_edi;
                    recomp_ret(c);
                    return;
                }
                o = rd32(o); // o = o->next
            }
        }
    }

    // String not found: allocate new string via native_00ae5ec0
    c->r[7] = l; // l passed in EDI to 00ae5ec0
    c->r[4] -= 4; wr32(c->r[4], h);
    c->r[4] -= 4; wr32(c->r[4], str_ptr);
    c->r[4] -= 4; wr32(c->r[4], L);
    c->r[4] -= 4; wr32(c->r[4], 0xae6017u);
    native_00ae5ec0(c);
    c->r[4] += 12;
    uint32_t ts = c->r[0];

    c->r[0] = ts;
    c->r[3] = saved_ebx;
    c->r[5] = saved_ebp;
    c->r[6] = saved_esi;
    c->r[7] = saved_edi;
    recomp_ret(c);
}

#ifdef __cplusplus
}
#endif

#define FN_00847000 native_mutex_lock
#define FN_008472d0 native_mutex_unlock

// Legacy speculative Lua repairs changed guest layouts/GC state and can free a
// live state after upstream register corruption. New builds use the translated
// bodies. Keep the old experiment explicitly opt-in for historical comparison.
#ifdef BLUR_ENABLE_UNVERIFIED_LUA_OVERRIDES
#define FN_00ae5d20 native_luaM_realloc
#define FN_00ae9390 native_luaC_singlestep
#define FN_00ae5e00 native_luaS_resize
#define FN_00ae5ec0 native_00ae5ec0
#define FN_00ae5f60 native_luaS_newlstr
#endif

#define FN_00d43f86 native_memcpy
#define FN_00d447a2 native_memcpy
#define FN_7855aac0 native_memcpy
#define FN_7855afb0 native_memcpy

#define FN_00d4405a native_memset
#define FN_7855b320 native_memset

#define FN_00e09a10 native_blur_init_00e09a10
#define FN_7859c279 native_libm_sse2_acos
#define FN_7859c721 native_libm_sse2_acosf
#define FN_7859cb03 native_libm_sse2_asin
#define FN_7859cf51 native_libm_sse2_asinf
#define FN_7859d313 native_libm_sse2_atan
#define FN_7859d52a native_libm_sse2_atan2
#define FN_7859da45 native_libm_sse2_atanf
#define FN_7859dbea native_libm_sse2_cos
#define FN_7859dd9a native_libm_sse2_cosf
#define FN_7859def6 native_libm_sse2_exp
#define FN_7859e111 native_libm_sse2_expf
#define FN_7859e26d native_libm_sse2_log
#define FN_7859e498 native_libm_sse2_log10
#define FN_7859e6db native_libm_sse2_log10f
#define FN_7859e859 native_libm_sse2_logf
#define FN_7859e9d7 native_libm_sse2_pow
#define FN_7859fb0c native_libm_sse2_sin
#define FN_7859fcc9 native_libm_sse2_sinf
#define FN_7859fe1d native_libm_sse2_tan
#define FN_785a0077 native_libm_sse2_tanf
