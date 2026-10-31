#!/usr/bin/env python3
"""Post-processor to lower unmodelled SSE/SSE2 instructions into accurate runtime operations.

Blur makes heavy use of SSE/SSE2 instructions (CVTPS2PD, CVTPD2PS, ADDPS, MULPS,
CMPNLESS, etc.). This script replaces the unmodelled instruction traps with native
runtime operations defined in runtime/x86.h.
"""
from pathlib import Path
import re
import sys

PATTERNS = [
    (re.compile(r"^CVTPS2PD XMM(\d+),XMM(\d+)$"), lambda m: f"recomp_cvtps2pd(c, {m.group(1)}, {m.group(2)}u);"),
    (re.compile(r"^CVTPD2PS XMM(\d+),XMM(\d+)$"), lambda m: f"recomp_cvtpd2ps(c, {m.group(1)}, {m.group(2)}u);"),
    (re.compile(r"^ADDPS XMM(\d+),XMM(\d+)$"), lambda m: f"recomp_addps(c, {m.group(1)}, {m.group(2)}u);"),
    (re.compile(r"^MULPS XMM(\d+),XMM(\d+)$"), lambda m: f"recomp_mulps(c, {m.group(1)}, {m.group(2)}u);"),
    (re.compile(r"^SUBPS XMM(\d+),XMM(\d+)$"), lambda m: f"recomp_subps(c, {m.group(1)}, {m.group(2)}u);"),
    (re.compile(r"^DIVPS XMM(\d+),XMM(\d+)$"), lambda m: f"recomp_divps(c, {m.group(1)}, {m.group(2)}u);"),
    (re.compile(r"^SQRTPS XMM(\d+),XMM(\d+)$"), lambda m: f"recomp_sqrtps(c, {m.group(1)}, {m.group(2)}u);"),
    (re.compile(r"^CMPNLESS XMM(\d+),XMM(\d+)$"), lambda m: f"recomp_cmpnless(c, {m.group(1)}, xmm_f32(c, {m.group(2)}));"),
    (re.compile(r"^CMPNLESS XMM(\d+),dword ptr \[ESP \+ (0x[0-9a-f]+|\d+)\]$"), lambda m: f"recomp_cmpnless(c, {m.group(1)}, rdf32((c->r[4] + {m.group(2)}u)));"),
    (re.compile(r"^CMPNLESD XMM(\d+),XMM(\d+)$"), lambda m: f"recomp_cmpnlesd(c, {m.group(1)}, {m.group(2)}u);"),
    (re.compile(r"^PSLLQ XMM(\d+),(0x[0-9a-f]+|\d+)$"), lambda m: f"recomp_psllq_imm(c, {m.group(1)}, {m.group(2)}u);"),
    (re.compile(r"^PSRLQ XMM(\d+),(0x[0-9a-f]+|\d+)$"), lambda m: f"recomp_psrlq_imm(c, {m.group(1)}, {m.group(2)}u);"),
    (re.compile(r"^PADDQ XMM(\d+),XMM(\d+)$"), lambda m: f"recomp_paddq(c, {m.group(1)}, {m.group(2)}u);"),
    (re.compile(r"^PADDQ XMM(\d+),xmmword ptr \[ESP \+ (0x[0-9a-f]+|\d+)\]$"), lambda m: f"recomp_paddq_mem(c, {m.group(1)}, (c->r[4] + {m.group(2)}u));"),
    (re.compile(r"^PADDQ XMM(\d+),xmmword ptr \[ESP \+ EDX\*0x8\]$"), lambda m: f"recomp_paddq_mem(c, {m.group(1)}, (c->r[4] + c->r[2] * 8u));"),
    (re.compile(r"^PMULUDQ MM(\d+),MM(\d+)$"), lambda m: f"c->mm[{m.group(1)}] = (uint64_t)(uint32_t)c->mm[{m.group(1)}] * (uint64_t)(uint32_t)c->mm[{m.group(2)}];"),
    (re.compile(r"^MOVDQ2Q MM(\d+),XMM(\d+)$"), lambda m: f"c->mm[{m.group(1)}] = (uint64_t)c->xmm[{m.group(2)}][0] | ((uint64_t)c->xmm[{m.group(2)}][1] << 32);"),
    (re.compile(r"^CVTTPS2PI MM(\d+),XMM(\d+)$"), lambda m: f"recomp_cvttps2pi(c, {m.group(1)}, {m.group(2)}u);"),
    (re.compile(r"^CVTTPS2PI MM(\d+),qword ptr \[EBP\]$"), lambda m: f"recomp_cvttps2pi_mem(c, {m.group(1)}, c->r[5]);"),
    (re.compile(r"^CVTPI2PS XMM(\d+),MM(\d+)$"), lambda m: f"recomp_cvtpi2ps(c, {m.group(1)}, {m.group(2)}u);"),
]

LINE_PAT = re.compile(r"^(\s*)recomp_unmodelled\(c,\s*0x[0-9a-f]+u\);\s*return;\s*/\*\s*([0-9a-f]+)\s+(.*?)\s*\*/")


def patch_file(path: Path) -> int:
    text = path.read_text()
    count = 0
    
    # 1. Yield cooperative scheduler on CMPXCHG8B contention so lock holder can proceed
    old_cmpxchg = "else { c->r[0] = (uint32_t)dst_; c->r[2] = (uint32_t)(dst_ >> 32); }"
    new_cmpxchg = "else { c->r[0] = (uint32_t)dst_; c->r[2] = (uint32_t)(dst_ >> 32); host_guest_yield(); }"
    if old_cmpxchg in text:
        count += text.count(old_cmpxchg)
        text = text.replace(old_cmpxchg, new_cmpxchg)
        
    lines = text.splitlines(keepends=True)
    new_lines = []
    for line in lines:
        m = LINE_PAT.match(line)
        if m:
            indent, addr, insn = m.groups()
            rep = None
            for p, fn in PATTERNS:
                pm = p.match(insn)
                if pm:
                    rep = fn(pm)
                    break
            if rep:
                new_lines.append(f"{indent}{rep}  /* {addr} {insn} */\n")
                count += 1
                continue
        new_lines.append(line)
        
    if count > 0:
        path.write_text("".join(new_lines))
    return count


def patch_chunk_053(gen_dir: Path):
    chunk_053 = gen_dir / "chunk_053.c"
    if not chunk_053.exists():
        return
    content = chunk_053.read_text()
    target = "c->r[0] = rd32((c->r[7] + 0xfffffffcu));  /* 00685ceb MOV EAX,dword ptr [EDI + -0x4] */\n    c->r[2] = rd32(c->r[0]);"
    repl = "c->r[0] = rd32((c->r[7] + 0xfffffffcu));  /* 00685ceb MOV EAX,dword ptr [EDI + -0x4] */\n    if (!c->r[0]) { goto L_00685dc6; }\n    c->r[2] = rd32(c->r[0]);"
    if target in content:
        content = content.replace(target, repl)
        chunk_053.write_text(content)
        print("Safeguarded fn_00685cc0 in chunk_053.c against null textures")


def patch_chunk_115(gen_dir: Path):
    c115 = gen_dir / "chunk_115.c"
    if not c115.exists():
        return
    content = c115.read_text()
    mod = False
    if "#include <stdio.h>" not in content:
        content = '#include <stdio.h>\n' + content
        mod = True
    t = ("void fn_00ae5d20(X86 *c) {\n"
         "    c->r[0] = rd32((c->r[4] + 0x4u));  /* 00ae5d20 MOV EAX,dword ptr [ESP + 0x4] */")
    r = ("void fn_00ae5d20(X86 *c) {\n"
         "    uint32_t ret_caller = rd32(c->r[4]);\n"
         "    uint32_t arg_L = rd32(c->r[4] + 4);\n"
         "    uint32_t arg_block = rd32(c->r[4] + 8);\n"
         "    uint32_t arg_osize = rd32(c->r[4] + 12);\n"
         "    uint32_t arg_nsize = rd32(c->r[4] + 16);\n"
         "    uint32_t g_val = arg_L ? rd32(arg_L + 0x10) : 0;\n"
         "    uint32_t frealloc_val = g_val ? rd32(g_val + 0xc) : 0;\n"
         "    static int count_115 = 0;\n"
         "    if (count_115++ < 20 || !frealloc_val) {\n"
         "        fprintf(stderr, \"[luaM_realloc_] caller=%08x L=%08x (g=%08x frealloc=%08x) block=%08x osize=%u nsize=%u\\n\",\n"
         "                ret_caller, arg_L, g_val, frealloc_val, arg_block, arg_osize, arg_nsize);\n"
         "    }\n"
         "    c->r[0] = rd32((c->r[4] + 0x4u));  /* 00ae5d20 MOV EAX,dword ptr [ESP + 0x4] */")
    if t in content:
        content = content.replace(t, r)
        mod = True

    t_str = ("void fn_00ae5f60(X86 *c) {\n"
             "    {   /* 00ae5f60 PUSH EBX */")
    r_str = ("void fn_00ae5f60(X86 *c) {\n"
             "    uint32_t ret_caller = rd32(c->r[4]);\n"
             "    uint32_t arg_L = rd32(c->r[4] + 4);\n"
             "    uint32_t arg_str = rd32(c->r[4] + 8);\n"
             "    uint32_t arg_len = rd32(c->r[4] + 12);\n"
             "    char sbuf[128];\n"
             "    int sn = (arg_len < 64) ? (int)arg_len : 63;\n"
             "    for (int si = 0; si < sn; ++si) sbuf[si] = (char)rd8(arg_str + si);\n"
             "    sbuf[sn] = '\\0';\n"
             "    if (!arg_L) {\n"
             "        fprintf(stderr, \"[luaS_newlstr BUG] L=0! caller=%08x str=%08x len=%u text='%s'\\n\",\n"
             "                ret_caller, arg_str, arg_len, sbuf);\n"
             "    }\n"
             "    {   /* 00ae5f60 PUSH EBX */")
    t_fld = ("void fn_00ae3050(X86 *c) {\n"
             "    c->r[0] = rd32((c->r[4] + 0x8u));  /* 00ae3050 MOV EAX,dword ptr [ESP + 0x8] */")
    r_fld = ("void fn_00ae3050(X86 *c) {\n"
             "    uint32_t ret_caller = rd32(c->r[4]);\n"
             "    uint32_t arg_L = rd32(c->r[4] + 4);\n"
             "    uint32_t arg_idx = rd32(c->r[4] + 8);\n"
             "    uint32_t arg_k = rd32(c->r[4] + 12);\n"
             "    char kbuf[64];\n"
             "    for (int ki = 0; ki < 32; ++ki) kbuf[ki] = arg_k ? (char)rd8(arg_k + ki) : 0;\n"
             "    kbuf[32] = 0;\n"
             "    fprintf(stderr, \"[lua_setfield] caller=%08x L=%08x idx=%d k='%s'\\n\",\n"
             "            ret_caller, arg_L, (int)arg_idx, kbuf);\n"
             "    c->r[0] = rd32((c->r[4] + 0x8u));  /* 00ae3050 MOV EAX,dword ptr [ESP + 0x8] */")
    if t_fld in content:
        content = content.replace(t_fld, r_fld)
        mod = True

    if mod:
        c115.write_text(content)
        print("Patched logging in fn_00ae5d20 / fn_00ae5f60 / fn_00ae3050 in chunk_115.c")


def patch_setjmp_longjmp(gen_dir: Path):
    c116 = gen_dir / "chunk_116.c"
    if c116.exists():
        content = c116.read_text()
        t1_orig = ("    {   /* 00ae9b23 CALL 0x00d449ce */\n"
                   "        c->r[4] -= 4; wr32(c->r[4], 0xae9b28u);\n"
                   "        CALL_FN(00d449ce);\n"
                   "    }")
        t1_prev = ("    {   /* 00ae9b23 CALL 0x00d449ce (_setjmp3) */\n"
                   "        jmp_buf *b = recomp_setjmp_prepare(c);\n"
                   "        recomp_setjmp_return(c, setjmp(*b));\n"
                   "    }")
        r1 = ("    {   /* 00ae9b23 CALL 0x00d449ce (_setjmp3) */\n"
              "        c->r[4] -= 4; wr32(c->r[4], 0xae9b28u);\n"
              "        jmp_buf *b = recomp_setjmp_prepare(c);\n"
              "        recomp_setjmp_return(c, setjmp(*b));\n"
              "    }")
        t2_orig = ("    {   /* 00aea32c CALL dword ptr [0x00e534dc] */\n"
                   "        uint32_t t_ = rd32(0xe534dcu);\n"
                   "        c->r[4] -= 4; wr32(c->r[4], 0xaea332u);\n"
                   "        recomp_call(c, t_);\n"
                   "    }")
        t2_prev = ("    {   /* 00aea32c CALL dword ptr [0x00e534dc] (longjmp) */\n"
                   "        recomp_longjmp(c);\n"
                   "        return;\n"
                   "    }")
        r2 = ("    {   /* 00aea32c CALL dword ptr [0x00e534dc] (longjmp) */\n"
              "        c->r[4] -= 4; wr32(c->r[4], 0xaea332u);\n"
              "        recomp_longjmp(c);\n"
              "        return;\n"
              "    }")
        mod = False
        if "#include <stdio.h>" not in content:
            content = '#include <stdio.h>\n' + content
            mod = True
        t_rawrun = ("void fn_00ae9b00(X86 *c) {\n"
                    "    {   /* 00ae9b00 PUSH EBP */")
        r_rawrun = ("void fn_00ae9b00(X86 *c) {\n"
                    "    fprintf(stderr, \"[luaD_rawrunprotected entry] ESP=%08x L=%08x f=%08x ud=%08x\\n\",\n"
                    "            c->r[4], rd32(c->r[4]+4), rd32(c->r[4]+8), rd32(c->r[4]+12));\n"
                    "    {   /* 00ae9b00 PUSH EBP */")
        if t_rawrun in content:
            content = content.replace(t_rawrun, r_rawrun)
            mod = True
        t_ret_rawrun = ("    c->r[0] = rd32((c->r[5] + 0xfffffffcu));  /* 00ae9b3d MOV EAX,dword ptr [EBP + -0x4] */\n"
                        "    {   /* 00ae9b40 ADD ESP,0x8 */")
        r_ret_rawrun = ("    fprintf(stderr, \"[luaD_rawrunprotected exit] res=%d\\n\", rd32(c->r[5] + 0xfffffffcu));\n"
                        "    c->r[0] = rd32((c->r[5] + 0xfffffffcu));  /* 00ae9b3d MOV EAX,dword ptr [EBP + -0x4] */\n"
                        "    {   /* 00ae9b40 ADD ESP,0x8 */")
        if t_ret_rawrun in content:
            content = content.replace(t_ret_rawrun, r_ret_rawrun)
            mod = True
        t_err = "static void body_00ae9a80(X86 *c, uint32_t entry_) {\n    switch (entry_) {"
        r_err = ("static void body_00ae9a80(X86 *c, uint32_t entry_) {\n"
                 "    uint32_t ret_caller = rd32(c->r[4]);\n"
                 "    uint32_t a1 = rd32(c->r[4] + 4);\n"
                 "    uint32_t a2 = rd32(c->r[4] + 8);\n"
                 "    uint32_t a3 = rd32(c->r[4] + 12);\n"
                 "    fprintf(stderr, \"[luaD_seterrorobj] entry=%08x caller=%08x a1(L)=%08x a2(err)=%d a3(top)=%08x ESI=%08x EDI=%08x\\n\",\n"
                 "            entry_, ret_caller, a1, (int)a2, a3, c->r[6], c->r[7]);\n"
                 "    switch (entry_) {")
        if t_err in content:
            content = content.replace(t_err, r_err)
            mod = True
        if t1_prev in content:
            content = content.replace(t1_prev, r1)
            mod = True
        elif t1_orig in content:
            content = content.replace(t1_orig, r1)
            mod = True
        if t2_prev in content:
            content = content.replace(t2_prev, r2)
            mod = True
        elif t2_orig in content:
            content = content.replace(t2_orig, r2)
            mod = True
        t_call_edx = ("    {   /* 00aea53a PUSH ESI */\n"
                      "        uint32_t v_ = c->r[6];\n"
                      "        c->r[4] -= 4; wr32(c->r[4], v_);\n"
                      "    }\n"
                      "    {   /* 00aea53b CALL EDX */")
        r_call_edx = ("    fprintf(stderr, \"[00aea53b CALL EDX] target=%08x ESI=%08x ESP=%08x\\n\", c->r[2], c->r[6], c->r[4]);\n"
                      "    {   /* 00aea53a PUSH ESI */\n"
                      "        uint32_t v_ = c->r[6];\n"
                      "        c->r[4] -= 4; wr32(c->r[4], v_);\n"
                      "    }\n"
                      "    {   /* 00aea53b CALL EDX */")
        if t_call_edx in content:
            content = content.replace(t_call_edx, r_call_edx)
            mod = True
        if mod:
            c116.write_text(content)
            print("Patched _setjmp3 and longjmp in chunk_116.c")


def patch_chunk_068(gen_dir: Path):
    c068 = gen_dir / "chunk_068.c"
    if not c068.exists():
        return
    content = c068.read_text()
    mod = False
    if "#include <stdio.h>" not in content:
        content = '#include <stdio.h>\n' + content
        mod = True
    t_entry = ("static void body_0073de60(X86 *c, uint32_t entry_) {\n"
               "    switch (entry_) {")
    r_entry = ("static void body_0073de60(X86 *c, uint32_t entry_) {\n"
               "    fprintf(stderr, \"[body_0073de60 entry] entry=%08x ECX=%08x ESI=%08x [0x115eba4]=%08x\\n\",\n"
               "            entry_, c->r[1], c->r[6], rd32(0x115eba4u));\n"
               "    switch (entry_) {")
    if t_entry in content:
        content = content.replace(t_entry, r_entry)
        mod = True
    t_write_l = ("    wr32((c->r[6] + 0x2790u), (uint32_t)(c->r[0]));  /* 0073dfbd MOV dword ptr [ESI + 0x2790],EAX */\n"
                 "    {   /* 0073dfc3 CALL 0x00ae24b0 */")
    r_write_l = ("    fprintf(stderr, \"[0073dfbd write L] ESI=%08x EAX(L)=%08x [0x115eba4]=%08x\\n\", c->r[6], c->r[0], rd32(0x115eba4u));\n"
                 "    wr32((c->r[6] + 0x2790u), (uint32_t)(c->r[0]));  /* 0073dfbd MOV dword ptr [ESI + 0x2790],EAX */\n"
                 "    {   /* 0073dfc3 CALL 0x00ae24b0 */")
    if t_write_l in content:
        content = content.replace(t_write_l, r_write_l)
        mod = True
    t_read_l = ("    c->r[1] = rd32((c->r[6] + 0x2790u));  /* 0073dff3 MOV ECX,dword ptr [ESI + 0x2790] */\n"
                "    c->r[2] = rd32(c->r[0]);  /* 0073dff9 MOV EDX,dword ptr [EAX] */")
    r_read_l = ("    fprintf(stderr, \"[0073dff3 read L] ESI=%08x [ESI+0x2790]=%08x [0x115eba4]=%08x [obj+0x2790]=%08x\\n\",\n"
                "            c->r[6], rd32(c->r[6] + 0x2790u), rd32(0x115eba4u), rd32(rd32(0x115eba4u) + 0x2790u));\n"
                "    c->r[1] = rd32((c->r[6] + 0x2790u));  /* 0073dff3 MOV ECX,dword ptr [ESI + 0x2790] */\n"
                "    c->r[2] = rd32(c->r[0]);  /* 0073dff9 MOV EDX,dword ptr [EAX] */")
    if t_read_l in content:
        content = content.replace(t_read_l, r_read_l)
        mod = True
    if mod:
        c068.write_text(content)
        print("Patched logging in body_0073de60 in chunk_068.c")


def patch_chunk_148(gen_dir: Path):
    c148 = gen_dir / "chunk_148.c"
    if not c148.exists():
        return
    content = c148.read_text()
    mod = False
    if "#include <stdio.h>" not in content:
        content = '#include <stdio.h>\n' + content
        mod = True
    t_c33380 = ("void fn_00c33380(X86 *c) {\n"
                "    c->r[0] = rd32(0xee89acu);  /* 00c33380 MOV EAX,dword ptr [0xee89ac] */")
    r_c33380 = ("void fn_00c33380(X86 *c) {\n"
                "    uint32_t ret_caller = rd32(c->r[4]);\n"
                "    uint32_t a1 = rd32(c->r[4] + 4);\n"
                "    uint32_t a2 = rd32(c->r[4] + 8);\n"
                "    uint32_t a3 = rd32(c->r[4] + 12);\n"
                "    fprintf(stderr, \"[fn_00c33380] caller=%08x ECX(this)=%08x a1=%08x a2=%08x a3=%08x\\n\",\n"
                "            ret_caller, c->r[1], a1, a2, a3);\n"
                "    c->r[0] = rd32(0xee89acu);  /* 00c33380 MOV EAX,dword ptr [0xee89ac] */")
    if t_c33380 in content:
        content = content.replace(t_c33380, r_c33380)
        mod = True
    if mod:
        c148.write_text(content)
        print("Patched logging in fn_00c33380 in chunk_148.c")


def patch_chunk_175(gen_dir: Path):
    c175 = gen_dir / "chunk_175.c"
    if not c175.exists():
        return
    content = c175.read_text()
    mod = False
    if "#include <stdio.h>" not in content:
        content = '#include <stdio.h>\n' + content
        mod = True
    t_d677d0 = ("void fn_00d677d0(X86 *c) {\n"
                "    {   /* 00d677d0 PUSH ESI */")
    r_d677d0 = ("void fn_00d677d0(X86 *c) {\n"
                "    fprintf(stderr, \"[fn_00d677d0 entry] ESP=%08x [ESP]=%08x [ESP+4]=%08x [ESP+8]=%08x ESI=%08x\\n\",\n"
                "            c->r[4], rd32(c->r[4]), rd32(c->r[4]+4), rd32(c->r[4]+8), c->r[6]);\n"
                "    {   /* 00d677d0 PUSH ESI */")
    if t_d677d0 in content:
        content = content.replace(t_d677d0, r_d677d0)
        mod = True
    if mod:
        c175.write_text(content)
        print("Patched logging in fn_00d677d0 in chunk_175.c")

    c159 = gen_dir / "chunk_159.c"
    if c159.exists():
        content = c159.read_text()
        t1_orig = ("    {   /* 00cafc93 CALL 0x00d449ce */\n"
                   "        c->r[4] -= 4; wr32(c->r[4], 0xcafc98u);\n"
                   "        CALL_FN(00d449ce);\n"
                   "    }")
        t1_prev = ("    {   /* 00cafc93 CALL 0x00d449ce (_setjmp3) */\n"
                   "        jmp_buf *b = recomp_setjmp_prepare(c);\n"
                   "        recomp_setjmp_return(c, setjmp(*b));\n"
                   "    }")
        r1 = ("    {   /* 00cafc93 CALL 0x00d449ce (_setjmp3) */\n"
              "        c->r[4] -= 4; wr32(c->r[4], 0xcafc98u);\n"
              "        jmp_buf *b = recomp_setjmp_prepare(c);\n"
              "        recomp_setjmp_return(c, setjmp(*b));\n"
              "    }")
        t2_orig = ("    {   /* 00cb0098 CALL 0x00d449ce */\n"
                   "        c->r[4] -= 4; wr32(c->r[4], 0xcb009du);\n"
                   "        CALL_FN(00d449ce);\n"
                   "    }")
        t2_prev = ("    {   /* 00cb0098 CALL 0x00d449ce (_setjmp3) */\n"
                   "        jmp_buf *b = recomp_setjmp_prepare(c);\n"
                   "        recomp_setjmp_return(c, setjmp(*b));\n"
                   "    }")
        r2 = ("    {   /* 00cb0098 CALL 0x00d449ce (_setjmp3) */\n"
              "        c->r[4] -= 4; wr32(c->r[4], 0xcb009du);\n"
              "        jmp_buf *b = recomp_setjmp_prepare(c);\n"
              "        recomp_setjmp_return(c, setjmp(*b));\n"
              "    }")
        mod = False
        if t1_prev in content:
            content = content.replace(t1_prev, r1)
            mod = True
        elif t1_orig in content:
            content = content.replace(t1_orig, r1)
            mod = True
        if t2_prev in content:
            content = content.replace(t2_prev, r2)
            mod = True
        elif t2_orig in content:
            content = content.replace(t2_orig, r2)
            mod = True
        if mod:
            c159.write_text(content)
            print("Patched _setjmp3 in chunk_159.c")


def patch_all(gen_dir: Path) -> int:
    total_patched = 0
    files_modified = 0
    for chunk in sorted(gen_dir.glob("chunk_*.c")):
        patched = patch_file(chunk)
        if patched > 0:
            total_patched += patched
            files_modified += 1
    patch_chunk_053(gen_dir)
    patch_chunk_068(gen_dir)
    patch_chunk_115(gen_dir)
    patch_chunk_148(gen_dir)
    patch_chunk_175(gen_dir)
    patch_setjmp_longjmp(gen_dir)
    print(f"Patched {total_patched} unmodelled instructions across {files_modified} files in {gen_dir}")
    return total_patched


if __name__ == "__main__":
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("local/recomp-blur-msvcr90/build/recomp/gen")
    patch_all(target)
