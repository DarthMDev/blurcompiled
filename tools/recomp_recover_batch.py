#!/usr/bin/env python3
"""Validate independent Ghidra batch exports against pinned PE bytes, then emit.

Only baseline recovered entries already named by the static trap inventory are
eligible. Incomplete, conflicting or open bodies remain review items.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from recomp_blur_probe import ROOT, BLUR_SHA, digest
from recomp_trap_batch import fresh_local, write_json, TESTED
from recomp_observed_slice import baseline_entries


def check_exported_boundaries(addresses, insns, stop):
    if not insns or addresses != [i.address for i in insns] or insns[-1].address + insns[-1].size != stop:
        raise ValueError('Ghidra reachable body and baseline PE extent disagree')


def main():
    import capstone
    import pefile
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--exports', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    image = ROOT / 'local/recomp-blur/original/Blur.exe'
    if digest(image) != BLUR_SHA:
        raise ValueError('Executable identity changed')
    inv = json.loads(args.inventory.read_text())
    if inv['executable_sha256'] != BLUR_SHA:
        raise ValueError('Inventory identity changed')
    pe = pefile.PE(str(image))
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    out = fresh_local(args.out)
    baseline = ROOT / 'local/startup-native-r2/build/recomp/gen/funcs.h'
    dispatch = baseline_entries(baseline.read_text())
    result = dict(executable_sha256=BLUR_SHA, inventory=str(args.inventory.relative_to(ROOT)),
                  inventory_sha256=digest(args.inventory), accepted=[], review=[], complete=False,
                  parity_established=False)
    for row in inv['owners']:
        if row['listing_sha256'] or row['first'] != row['va'] or not all(s['mnemonic'] in TESTED for s in row['sites']):
            continue
        va = row['va']
        observed = args.exports / f'observed-{va:08x}'
        listing = observed / f'{va:08x}.asm'
        if not listing.is_file():
            continue
        log_path = out / f'{va:08x}.log'
        try:
            evidence = dict(line.split('=', 1) for line in (observed / 'evidence.txt').read_text().splitlines() if '=' in line)
            if evidence['sha256'] != BLUR_SHA or int(evidence['va'], 16) != va:
                raise ValueError('Export identity changed')
            if digest(ROOT / row['chunk']) != row['chunk_sha256']:
                raise ValueError('Baseline ownership changed')
            end_ins = next(decoder.disasm(pe.get_data(row['last'] - 0x400000, 15), row['last']))
            stop = end_ins.address + end_ins.size
            insns = list(decoder.disasm(pe.get_data(va - 0x400000, stop - va), va))
            addresses = [int(line.split()[0], 16) for line in listing.read_text().splitlines()]
            # Fail closed on any missing bytes: these are review items, not
            # permission to insert padding, skip an instruction, or invent CFGs.
            check_exported_boundaries(addresses, insns, stop)
            write_json(out / f'{va:08x}-byte-check.json', dict(
                executable_sha256=BLUR_SHA, va=hex(va), rva=hex(va - 0x400000),
                listing_sha256=digest(listing), ownership_header=row['ownership_header'],
                instructions=[dict(va=hex(i.address), rva=hex(i.address - 0x400000), bytes=i.bytes.hex(),
                                   instruction=i.mnemonic + ' ' + i.op_str) for i in insns],
                evidence='Independent Ghidra export and pinned PE Capstone boundaries agree exactly',
                confidence='High byte and baseline entry identity; runtime reach and behavior unverified'))
            module = out / f'module-{va:08x}'
            command = [sys.executable, str(ROOT / 'tools/recomp_observed_slice.py'),
                       '--observed', str(observed), '--out', str(module), '--replace-existing',
                       '--baseline-funcs', str(ROOT / 'local/startup-native-r2/build/recomp/gen/funcs.h')]
            # Independent export can restore a complete owner that the baseline
            # split into several suffix bodies. Preserve every existing dispatch
            # entry at a verified instruction boundary, not only header aliases.
            entries = sorted((dispatch & set(addresses)) - {va})
            for entry in entries:
                command += ['--entry', hex(entry)]
            with log_path.open('x') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
                report = json.loads((module / 'translation.json').read_text())
                if report['unmodelled'] or report['unresolved_branches'] or report['tables']:
                    raise ValueError('Retained traps or table review required')
                subprocess.run([sys.executable, str(ROOT / 'local/recomp-kit-static-binding/tools/lazy_static.py'),
                    '--game-dir', str(ROOT / 'local/startup-native-r2'), '--discovered', str(module / 'discovered.txt'),
                    '--generated', str(module / 'generated'), '--out', str(module / 'code.dylib')],
                    stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
            result['accepted'].append(dict(va=va, rva=va - 0x400000, sites=len(row['sites']),
                path=str((module / 'code.dylib').relative_to(ROOT)), sha256=digest(module / 'code.dylib')))
        except (ValueError, subprocess.SubprocessError) as error:
            if not log_path.exists():
                log_path.write_text(str(error) + '\n')
            result['review'].append(dict(va=va, rva=va - 0x400000, reason=str(error), log=str(log_path.relative_to(ROOT))))
        write_json(out / 'batch.json', result)
        print(json.dumps(dict(va=hex(va), accepted=len(result['accepted']), review=len(result['review']))), flush=True)
    result['complete'] = True
    write_json(out / 'batch.json', result)


if __name__ == '__main__':
    main()
