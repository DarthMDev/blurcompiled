#!/usr/bin/env python3
"""Translate one closed Ghidra-exported entry without editing baseline listings.

This runs strict instruction emission, not whole-image discovery. Open branches,
listing gaps, arbitrary indirect jumps and SEH require a separate full pass.
Explicit local tables must have finite decoder bounds and wholly internal targets.
Explicit audited tail targets allow unconditional jumps to existing entries.
Unsupported SSE forms retain runtime abort boundaries, recorded in translation.json.
No generated C is patched.
"""
import argparse
import importlib.util
import json
import re
from pathlib import Path
import sys
from types import SimpleNamespace

from recomp_blur_probe import ROOT, BLUR_SHA, digest


def check_closed_cfg(insns, fallthrough, branch_target, conditional_branches, tail_targets=(), local_jumps=None):
    addresses = {ins.addr for ins in insns}
    local_jumps = local_jumps or {}
    for i, ins in enumerate(insns):
        if ins.mnem in conditional_branches or ins.mnem == 'JMP':
            target = branch_target(ins)
            local_table = (ins.mnem == 'JMP' and target is None and ins.addr in local_jumps
                           and local_jumps[ins.addr] and set(local_jumps[ins.addr]).issubset(addresses))
            if target not in addresses and not (ins.mnem == 'JMP' and target in tail_targets) and not local_table:
                raise ValueError('Open or indirect branch in bounded function')
        if ins.mnem not in ('RET', 'JMP') and fallthrough[i] not in addresses:
            raise ValueError('Listing has a fallthrough gap')


def checked_entries(insns, va, entries):
    entries = sorted(set(entries) - {va})
    if not set(entries).issubset({ins.addr for ins in insns}):
        raise ValueError('Alternate entry is not a listed instruction')
    return entries


def check_extent(first, end, va, max_bytes):
    if max_bytes not in (4096, 8192, 16384):
        raise ValueError('Bound must be 4096, 8192 or 16384 bytes')
    if first != va or not end or not 0 < end - va <= max_bytes:
        raise ValueError('Invalid bounded function extent')


def emitted_runtime_traps(body, insns, base):
    """Deliberate SSE traps do not enter Translator.unmodelled; inventory them."""
    addresses = sorted({int(value, 16) for value in re.findall(
        r'\brecomp_unmodelled\(c,\s*(0x[0-9a-fA-F]+)u?\)', '\n'.join(body))})
    listing = {ins.addr: ins.raw for ins in insns}
    return [dict(va=hex(addr), rva=hex(addr - base),
                 instruction=listing.get(addr, 'not in supplied listing')) for addr in addresses]


def baseline_entries(text):
    return {int(a, 16) for a in re.findall(r'^void fn_([0-9a-f]{8})\(X86 \*c\);$', text, re.M)}


def checked_local_tables(sites, decoded, inferred, insns, allow):
    """Accept only finite decoded tables whose every target is in this body."""
    if sites and not allow:
        raise ValueError('Jump tables require explicit bounded-table validation')
    addresses = {ins.addr for ins in insns}
    result = {}
    for key in sites:
        targets = decoded.get(key)
        if key in inferred or not targets or not set(targets).issubset(addresses):
            raise ValueError('Table is unbounded, undecoded, or leaves the complete function')
        result[key[1]] = list(targets)
    return result


def emit_bounded(translator, listing, va, out, replace_existing=False, entries=(), max_bytes=4096, tail_targets=(), baseline_funcs=None, local_tables=False):
    """Emit a closed, explicitly bounded function using the kit's tested emitter API.

    This deliberately does not run or claim whole-image discovery acceptance.
    External CALLs retain runtime dispatch. Incomplete branches are refused.
    """
    translator.configure(translator.game_config.load(ROOT / 'recomp'))
    image = translator.Image(translator.BINARY)
    insns = translator.parse_listing(str(listing))
    end = image.insn_end(insns[-1].addr, insns[-1].mnem)
    check_extent(insns[0].addr, end, va, max_bytes)
    fn = translator.Function(va, 'observed_%08x' % va, end - va, insns)
    fn.measure(image)
    known = {int(line.split('\t')[0], 16) for line in Path(translator.FUNCS_TSV).read_text().splitlines()[1:]}
    if baseline_funcs:
        known.update(baseline_entries(Path(baseline_funcs).read_text()))
    known.add(va)
    tr = translator.Translator(image, known, SimpleNamespace(eager_flags=True))
    if not set(tail_targets).issubset(known):
        raise ValueError('Tail target must be an existing listed or baseline dispatch entry')
    tr.prepare(fn, strict=True)
    local_jumps = checked_local_tables(tr.table_sites, tr.jumptables,
                                      tr.table_sites_inferred, insns, local_tables)
    check_closed_cfg(insns, fn.fallthrough, tr.branch_target, translator.JCC, tail_targets, local_jumps)
    if fn.seh_sites:
        raise ValueError('SEH needs full discovery')
    entries = checked_entries(insns, va, entries)
    addresses = [va] + entries
    body = translator.dispatch_outside(tr.translate(fn, entries), set(addresses))
    if tr.unmodelled or tr.unlisted_targets:
        raise ValueError('Unmodelled instructions or unresolved targets')
    runtime_traps = emitted_runtime_traps(body, insns, image.base)
    tables = [dict(va=hex(site), rva=hex(site - image.base),
                   base=hex(tr.table_sites[(fn.addr, site)]), targets=[hex(a) for a in targets])
              for site, targets in sorted(local_jumps.items())]
    generated = out / 'generated'
    generated.mkdir()
    (generated / 'funcs.h').write_text('#include "x86.h"\n#include "intrinsics.h"\n#include "seh.h"\n' + ''.join('void fn_%08x(X86 *c);\n' % a for a in addresses))
    (generated / 'chunk_000.c').write_text('#include "funcs.h"\n' + '\n'.join(body) + '\n')
    returns = sorted({fn.fallthrough[i] for i, ins in enumerate(insns) if ins.mnem == 'CALL'})
    table = f'''#include "funcs.h"
static const uint32_t addresses[] = {{{', '.join('0x%08xu' % a for a in addresses)}}};
static void (*const functions[])(X86 *) = {{{', '.join('fn_%08x' % a for a in addresses)}}};
static void hook(X86 *c, uint32_t i) {{ functions[i](c); }}
static RecompHookFn hooks[{len(addresses)}];
static uint8_t hooked[{len(addresses)}];
static const uint32_t returns[] = {{{', '.join('0x%08xu' % a for a in returns) or '0'}}};
static const char *const names[] = {{{', '.join('"observed_%08x"' % a for a in addresses)}}};
static const RecompModule module = {{"blur_observed_{va:08x}", 0x{va:08x}u, 0x{end:08x}u,
 addresses, {len(addresses)}, functions, hooks, hooked, returns, {len(returns)}, names}};
__attribute__((constructor)) static void register_observed(void) {{
    for (unsigned i = 0; i < {len(addresses)}; ++i) hooks[i] = hook;
    recomp_module_register(&module);
}}
'''
    if replace_existing:
        table += '''
#include <stdio.h>
#include <stdlib.h>
extern const uint32_t recomp_func_addrs[], recomp_func_count;
extern RecompHookFn recomp_hook_ptrs[];
extern uint8_t recomp_hooked[];
static void replacement(X86 *c, uint32_t index) {
    for (unsigned j = 0; j < sizeof(addresses)/sizeof(addresses[0]); ++j)
        if (addresses[j] == recomp_func_addrs[index]) { functions[j](c); return; }
    abort();
}
__attribute__((constructor)) static void replace_existing(void) {
    for (unsigned j = 0; j < sizeof(addresses)/sizeof(addresses[0]); ++j) {
        uint32_t i = 0;
        while (i < recomp_func_count && recomp_func_addrs[i] != addresses[j]) ++i;
        if (i == recomp_func_count || recomp_hooked[i]) abort();
        recomp_hook_ptrs[i] = replacement;
        recomp_hooked[i] = 1;
        fprintf(stderr, "observed.replace %08x\\n", recomp_func_addrs[i]);
    }
}
'''
    (generated / 'table.c').write_text(table)
    report = dict(scope='bounded single function; not whole-image acceptance',
                  executable_sha256=BLUR_SHA, va=hex(va), rva=hex(va - image.base),
                  byte_size=end - va, max_bytes=max_bytes, tail_targets=[hex(a) for a in tail_targets], alternate_entries=[hex(a) for a in entries], instructions=len(insns), eager_flags=True, tables=tables, unmodelled=runtime_traps, unresolved_branches=[],
                  evidence='hash-verified Ghidra listing and kit instruction emitter',
                  confidence=('structural translation with retained runtime aborts; behavioral parity unverified'
                              if runtime_traps else 'structural translation only; behavioral parity unverified'),
                  replaces_existing_entry=replace_existing,
                  generated={p.name: digest(p) for p in generated.iterdir()})
    (out / 'translation.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--observed', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--replace-existing', action='store_true', help='replace existing primary and supplied alternate entries; refuse another hook')
    parser.add_argument('--entry', action='append', type=lambda value: int(value, 0), default=[], help='audited alternate entry within the same closed listing')
    parser.add_argument('--max-bytes', type=int, choices=(4096, 8192, 16384), default=4096, help='explicit complete-function bound; default 4096')
    parser.add_argument('--tail-target', action='append', type=lambda value: int(value, 0), default=[], help='audited unconditional tail jump to an existing function entry')
    parser.add_argument('--baseline-funcs', type=Path, help='existing verified generated funcs.h for recovered dispatch entries')
    parser.add_argument('--local-tables', action='store_true', help='require finite decoded tables with all targets inside this complete function')
    args = parser.parse_args()
    observed = args.observed.resolve(strict=True)
    evidence = dict(line.split('=', 1) for line in (observed / 'evidence.txt').read_text().splitlines() if '=' in line)
    if evidence['sha256'] != BLUR_SHA:
        raise ValueError('Observed entry belongs to another executable')
    va = int(evidence['va'], 16)
    if not 0x400000 <= va < 0x125b000:
        raise ValueError('Observed entry outside the pinned image')
    listing = observed / ('%08x.asm' % va)
    kit = ROOT / 'local/recomp-kit-static-binding'
    spec = importlib.util.spec_from_file_location('observed_translate', kit / 'tools/recomp/translate.py')
    translator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(translator)
    out = args.out.absolute()
    if out.resolve() != out or not out.is_relative_to((ROOT / 'local').resolve()) or out.exists():
        raise ValueError('Fresh output under local/ required')
    out.mkdir(parents=True)
    if digest(ROOT / 'local/recomp-blur/original/Blur.exe') != BLUR_SHA:
        raise ValueError('Pinned executable changed')
    discovered = out / 'discovered.txt'
    discovered.write_text('%08x call 00000000 1\n' % va)
    identity = {str(p.relative_to(ROOT)): digest(p) for p in
                (Path(__file__).resolve(), listing, observed / 'evidence.txt',
                 kit / 'tools/recomp/translate.py', ROOT / 'recomp/game.toml')}
    if args.baseline_funcs:
        identity[str(args.baseline_funcs.resolve().relative_to(ROOT))] = digest(args.baseline_funcs)
    (out / 'inputs.json').write_text(json.dumps(identity, indent=2) + '\n')
    return emit_bounded(translator, listing, va, out, args.replace_existing, args.entry, args.max_bytes, args.tail_target, args.baseline_funcs, args.local_tables)


if __name__ == '__main__':
    sys.exit(main())
