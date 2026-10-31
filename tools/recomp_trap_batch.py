#!/usr/bin/env python3
"""Inventory emitted traps and batch closed owners using existing tested lowerings.

Static structural acceptance only. Never edits baseline listings/generated code,
adds instruction semantics, or infers runtime reach from static discovery.
"""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from recomp_blur_probe import ROOT, BLUR_SHA, digest

HEADER = re.compile(r'/\* (?:FUN_|recovered_)([0-9a-f]{8})\s+\d+ insns\s+([0-9a-f]{8})\.\.([0-9a-f]{8})([^\n]*)\*/')
TRAP = re.compile(r'\brecomp_unmodelled\(c,\s*(0x[0-9a-fA-F]+)u?\)')
TESTED = {'cvtps2pd', 'cvtpd2ps', 'cmpnless'}


def scan_chunk(text):
    """Use explicit owner headers, not the nearest address in a C comment."""
    headers = list(HEADER.finditer(text))
    result = []
    for index, header in enumerate(headers):
        body = text[header.end():headers[index + 1].start() if index + 1 < len(headers) else len(text)]
        addresses = sorted({int(a, 16) for a in TRAP.findall(body)})
        if not addresses:
            continue
        owner, start, end = (int(header.group(n), 16) for n in (1, 2, 3))
        if any(not start <= a <= end for a in addresses):
            raise ValueError('Trap outside declared owner extent')
        entries = header.group(4).split('entries:', 1)
        alternates = [int(a, 16) for a in re.findall(r'\b[0-9a-f]{8}\b', entries[1])] if len(entries) == 2 else []
        # Verify every declared alternate is implemented by its baseline owner.
        for entry in alternates:
            wrapper = rf'void fn_{entry:08x}\(X86 \*c\) \{{ body_{owner:08x}\(c, 0x[0-9a-f]+u\); \}}'
            if not re.search(wrapper, body):
                raise ValueError('Alternate entry lacks baseline ownership wrapper')
        result.append(dict(va=owner, first=start, last=end, entries=alternates, sites=addresses,
                           ownership_header=header.group(0)))
    # Refuse orphan traps rather than silently assigning them to another owner.
    if {a for row in result for a in row['sites']} != {int(a, 16) for a in TRAP.findall(text)}:
        raise ValueError('Unowned trap sites')
    return result


def covers(owner, sites, report):
    if report.get('executable_sha256') != BLUR_SHA or not report.get('replaces_existing_entry'):
        return False
    entries = {int(report['va'], 16)} | {int(a, 16) for a in report.get('alternate_entries', [])}
    start = int(report['va'], 16)
    retained = {int(a['va'], 16) for a in report.get('unmodelled', [])}
    return owner in entries and all(start <= a < start + report['byte_size'] and a not in retained for a in sites)


def fresh_local(path):
    path = path.absolute()
    if path.resolve() != path or not path.is_relative_to(ROOT / 'local') or path.exists():
        raise ValueError('Fresh nonsymlink output under local/ required')
    path.mkdir(parents=True)
    return path


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n')


def nonconflicting(rows, reserved):
    """Refuse both sides of a batch overlap, and every active-entry collision."""
    counts = Counter(a for row in rows for a in row['entries'])
    return ([r for r in rows if all(counts[a] == 1 and a not in reserved for a in r['entries'])],
            [r for r in rows if any(counts[a] != 1 or a in reserved for a in r['entries'])])


def validate(args):
    batch = json.loads(args.batch.read_text())
    if not batch.get('complete') or batch.get('executable_sha256') != BLUR_SHA:
        raise ValueError('Complete hash-pinned batch required')
    inventory_path = ROOT / batch['inventory']
    if digest(inventory_path) != batch['inventory_sha256']:
        raise ValueError('Inventory changed')
    inv = json.loads(inventory_path.read_text())
    probe_path = ROOT / inv['active_probe']
    if digest(probe_path) != inv['active_probe_sha256']:
        raise ValueError('Active probe changed')
    probe = json.loads(probe_path.read_text())
    reserved = set(args.reserved_entry)
    reserved_manifests = {}
    for manifest in args.reserve_load:
        loaded = json.loads(manifest.read_text())
        if loaded['executable_sha256'] != BLUR_SHA:
            raise ValueError('Reserved load manifest identity changed')
        for module in loaded['modules']:
            if digest(ROOT / module['path']) != module['sha256']:
                raise ValueError('Reserved module changed')
            reserved.update(module['entries'])
        reserved_manifests[str(manifest.relative_to(ROOT))] = digest(manifest)
    for name, sha in probe['inputs'].items():
        path = ROOT / name
        if digest(path) != sha:
            raise ValueError('Active probe input changed: ' + name)
        report = path.parent / 'translation.json'
        if path.name == 'code.dylib' and report.is_file():
            r = json.loads(report.read_text())
            reserved.update([int(r['va'], 16)] + [int(a, 16) for a in r.get('alternate_entries', [])])
    rows = []
    for row in batch['accepted']:
        path = ROOT / row['path']
        if digest(path) != row['sha256']:
            raise ValueError('Batch module changed')
        r = json.loads((path.parent / 'translation.json').read_text())
        if r['executable_sha256'] != BLUR_SHA or r['unmodelled'] or r['unresolved_branches']:
            raise ValueError('Unaccepted module in batch')
        for name, sha in r['generated'].items():
            if digest(path.parent / 'generated' / name) != sha:
                raise ValueError('Generated module source changed')
        entries = [int(r['va'], 16)] + [int(a, 16) for a in r['alternate_entries']]
        rows.append({**row, 'entries': entries,
                     'symbols': [dict(va=hex(a), rva=hex(a - 0x400000), executable_sha256=BLUR_SHA,
                                      evidence=str((path.parent / 'translation.json').relative_to(ROOT)),
                                      confidence='Static entry/ownership evidence; runtime reach and behavioral parity unverified')
                                 for a in entries]})
    accepted, conflicts = nonconflicting(rows, reserved)
    out = fresh_local(args.out)
    write_json(out / 'load.json', dict(executable_sha256=BLUR_SHA, batch=str(args.batch.relative_to(ROOT)),
        batch_sha256=digest(args.batch), active_probe=inv['active_probe'], modules=accepted,
        reserved_manifests=reserved_manifests, reserved_entries=sorted(reserved),
        dispatch_conflicts=conflicts, review=batch['review'],
        notice='Conflict-free structural modules only; runtime integration and behavioral parity unverified'))
    print(json.dumps(dict(loadable=len(accepted), conflicts=len(conflicts), review=len(batch['review']))))


def inventory(args):
    import capstone
    import pefile
    exe = ROOT / 'local/recomp-blur/original/Blur.exe'
    if digest(exe) != BLUR_SHA:
        raise ValueError('Executable identity changed')
    probe = json.loads(args.active_probe.read_text())
    if not probe.get('inputs_unchanged'):
        raise ValueError('Active probe did not establish unchanged inputs')
    active = []
    for name, sha in probe['inputs'].items():
        path = ROOT / name
        if path.name != 'code.dylib' or not (path.parent / 'translation.json').is_file():
            continue
        if digest(path) != sha:
            raise ValueError('Active module changed: ' + name)
        report = json.loads((path.parent / 'translation.json').read_text())
        if 'byte_size' not in report:
            # Early manifests predate byte_size. Use their hash-verified module
            # descriptor rather than assuming an extent from another owner.
            table = path.parent / 'generated/table.c'
            if digest(table) != report['generated']['table.c']:
                raise ValueError('Active module descriptor changed')
            bounds = re.search(r'RecompModule module = \{"[^"]+", (0x[0-9a-f]+)u, (0x[0-9a-f]+)u,', table.read_text())
            if not bounds or int(bounds.group(1), 16) != int(report['va'], 16):
                raise ValueError('Active module lacks a verified extent')
            report['byte_size'] = int(bounds.group(2), 16) - int(bounds.group(1), 16)
        active.append((name, report))
    pe = pefile.PE(str(exe))
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    rows = []
    for name in sorted(os.listdir(args.generated)):
        if not re.fullmatch(r'chunk_[0-9]+\.c', name):
            continue
        path = args.generated / name
        chunk_sha = digest(path)
        for row in scan_chunk(path.read_text()):
            row['chunk'] = str(path.relative_to(ROOT))
            row['chunk_sha256'] = chunk_sha
            row['rva'] = row['va'] - 0x400000
            row['covered_by'] = [name for name, report in active if covers(row['va'], row['sites'], report)]
            row['sites'] = [dict(va=a, rva=a - 0x400000,
                                mnemonic=(ins := next(decoder.disasm(pe.get_data(a - 0x400000, 15), a))).mnemonic,
                                operands=ins.op_str, bytes=ins.bytes.hex()) for a in row['sites']]
            listing = args.listings / f"{row['va']:08x}.asm"
            row['listing'] = str(listing.relative_to(ROOT))
            row['listing_sha256'] = digest(listing) if listing.is_file() else None
            row['status'] = ('covered' if row['covered_by'] else
                             'candidate' if row['listing_sha256'] and all(s['mnemonic'] in TESTED for s in row['sites'])
                             else 'review')
            if row['status'] == 'candidate' and row['first'] != row['va']:
                row['status'] = 'review'
                row['review_reason'] = 'Baseline owner entry differs from first instruction'
            rows.append(row)
    multiplicity = Counter(s['va'] for r in rows for s in r['sites'])
    for row in rows:
        if row['status'] == 'candidate' and any(multiplicity[s['va']] > 1 for s in row['sites']):
            row['status'] = 'review'
            row['review_reason'] = 'Shared trap bytes in overlapping baseline owners'
    summary = dict(sites=len(multiplicity), owners=len(rows),
                   owners_by_status=dict(Counter(r['status'] for r in rows)),
                   instructions=dict(Counter({s['va']: s['mnemonic'] for r in rows for s in r['sites']}.values())))
    out = fresh_local(args.out)
    write_json(out / 'inventory.json', dict(executable_sha256=BLUR_SHA, tested_lowerings=sorted(TESTED),
               tool_sha256=digest(Path(__file__)),
               active_probe=str(args.active_probe.relative_to(ROOT)), active_probe_sha256=digest(args.active_probe),
               summary=summary, owners=rows,
               evidence='Baseline ownership headers, pinned executable byte decode, active module manifests',
               confidence='Static identity and structural eligibility; execution and behavioral parity unverified'))
    print(json.dumps(summary), flush=True)


def build(args):
    if args.limit < 0:
        raise ValueError('Batch limit must be nonnegative')
    data = json.loads(args.inventory.read_text())
    if data['executable_sha256'] != BLUR_SHA or digest(ROOT / 'local/recomp-blur/original/Blur.exe') != BLUR_SHA:
        raise ValueError('Executable identity changed')
    out = fresh_local(args.out)
    accepted, review = [], []
    candidates = [r for r in data['owners'] if r['status'] == 'candidate']
    if args.limit:
        candidates = candidates[:args.limit]
    for row in candidates:
        va = row['va']
        module = out / f'module-{va:08x}'
        observed = out / f'observed-{va:08x}'
        observed.mkdir()
        listing, chunk = ROOT / row['listing'], ROOT / row['chunk']
        if digest(listing) != row['listing_sha256'] or digest(chunk) != row['chunk_sha256']:
            raise ValueError('Inventory input changed')
        shutil.copyfile(listing, observed / listing.name)
        (observed / 'evidence.txt').write_text(f"sha256={BLUR_SHA}\nva={va:08x}\nrva={row['rva']:08x}\n"
            f"evidence={row['chunk']} {row['ownership_header']}; pinned listing; static trap inventory\n"
            "confidence=high static byte/owner identity; runtime reach and behavior unverified\n")
        command = [sys.executable, str(ROOT / 'tools/recomp_observed_slice.py'), '--observed', str(observed),
                   '--out', str(module), '--replace-existing', '--max-bytes', '8192']
        for entry in row['entries']:
            command += ['--entry', hex(entry)]
        # Jump tables, SEH and open CFGs deliberately remain on the review list.
        try:
            with (observed / 'build.log').open('x') as log:
                subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
                report = json.loads((module / 'translation.json').read_text())
                if report['unmodelled'] or report['unresolved_branches'] or report['tables']:
                    raise ValueError('Retained traps or structural review requirement')
                subprocess.run([sys.executable, str(ROOT / 'local/recomp-kit-static-binding/tools/lazy_static.py'),
                    '--game-dir', str(ROOT / 'local/startup-native-r2'), '--discovered', str(module / 'discovered.txt'),
                    '--generated', str(module / 'generated'), '--out', str(module / 'code.dylib')],
                    cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
            accepted.append(dict(va=va, rva=row['rva'], sites=len(row['sites']),
                                 path=str((module / 'code.dylib').relative_to(ROOT)), sha256=digest(module / 'code.dylib')))
        except (subprocess.SubprocessError, ValueError) as error:
            review.append(dict(va=va, rva=row['rva'], reason=str(error), log=str((observed / 'build.log').relative_to(ROOT))))
        write_json(out / 'batch.json', dict(inventory=str(args.inventory.relative_to(ROOT)),
                   inventory_sha256=digest(args.inventory), executable_sha256=BLUR_SHA,
                   accepted=accepted, review=review, complete=False, parity_established=False))
        print(json.dumps(dict(processed=len(accepted) + len(review), total=len(candidates), accepted=len(accepted), review=len(review))), flush=True)
    result = json.loads((out / 'batch.json').read_text()) if candidates else dict(
        inventory=str(args.inventory.relative_to(ROOT)), inventory_sha256=digest(args.inventory),
        executable_sha256=BLUR_SHA, accepted=[], review=[], parity_established=False)
    result['complete'] = True
    write_json(out / 'batch.json', result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    inv = sub.add_parser('inventory')
    inv.add_argument('--generated', type=Path, default=ROOT / 'local/startup-native-r2/build/recomp/gen')
    inv.add_argument('--listings', type=Path, default=ROOT / 'local/recomp-blur/repaired/Blur.exe/functions')
    inv.add_argument('--active-probe', type=Path, required=True)
    inv.add_argument('--out', type=Path, required=True)
    batch = sub.add_parser('build')
    batch.add_argument('--inventory', type=Path, required=True)
    batch.add_argument('--out', type=Path, required=True)
    batch.add_argument('--limit', type=int, default=0)
    val = sub.add_parser('validate')
    val.add_argument('--batch', type=Path, required=True)
    val.add_argument('--out', type=Path, required=True)
    val.add_argument('--reserved-entry', type=lambda s: int(s, 0), action='append', default=[])
    val.add_argument('--reserve-load', type=Path, action='append', default=[])
    args = parser.parse_args()
    dict(inventory=inventory, build=build, validate=validate)[args.command](args)


if __name__ == '__main__':
    main()
