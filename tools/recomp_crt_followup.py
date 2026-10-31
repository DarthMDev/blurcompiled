"""Bounded strict retry on a separately patched kit; never builds/runs a DLL."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
from types import SimpleNamespace

try:
    from . import recomp_crt_compare as c
except ImportError:
    import recomp_crt_compare as c

p = c.prior
ROOT = p.ROOT
KIT = ROOT / 'local/recomp-kit-msvc90-noreturn'
WORK = ROOT / 'local/recomp-crt-msvc90-translation'
PATCH = ROOT / 'recomp/patches/msvc90-exit-iat.patch'


def identities(patch=PATCH):
    manifest = c.verified(SimpleNamespace(work=WORK))
    commit = subprocess.check_output(['git', '-C', str(KIT), 'rev-parse', 'HEAD'], text=True).strip()
    diff = subprocess.check_output(['git', '-C', str(KIT), 'diff', '--binary'])
    if commit != p.PIN or diff != patch.read_bytes():
        raise ValueError('Separate kit pin/patch mismatch')
    untracked = subprocess.check_output(['git', '-C', str(KIT), 'ls-files', '--others', '--exclude-standard'], text=True)
    if untracked.strip():
        raise ValueError('Unexpected untracked kit source')
    export = json.loads((ROOT / 'reports/recomp-crt-msvc90-export.json').read_text())
    if not export['success'] or export['candidate_sha256'] != manifest['candidate']['sha256']:
        raise ValueError('Pinned candidate export required')
    c.verify_files(export['listing_inputs'])
    return dict(base_commit=commit, patch_sha256=p.digest(patch),
                candidate_sha256=manifest['candidate']['sha256'],
                parent_manifest_sha256=p.digest(WORK / 'manifest.json'),
                export_report_sha256=p.digest(ROOT / 'reports/recomp-crt-msvc90-export.json'),
                sources=p.fingerprint([Path(__file__).resolve(), ROOT / 'tools/recomp_crt_compare.py',
                                       ROOT / 'tools/recomp_crt_experiment.py', patch]),
                patched_sources=p.fingerprint([KIT / name for name in subprocess.check_output(
                    ['git', '-C', str(KIT), 'diff', '--name-only'], text=True).splitlines()]))


def translate(args):
    patch = args.patch.absolute() if args.patch else PATCH
    before = identities(patch)
    work = p.safe_output(ROOT / ('local/recomp-crt-msvc90-noreturn/round%d' % args.round))
    if work.exists():
        raise ValueError('Attempt exists; never overwrite')
    work.mkdir()
    report = p.safe_output(args.out)
    if report.exists():
        raise ValueError('Report exists')
    stats_path = work / 'statistics.json'
    step = c.run_bounded([sys.executable, KIT / 'tools/recomp/translate.py', '--game', WORK,
                          '--module', 'msvcr90', '--out', work / 'generated', '--report', stats_path],
                         work / 'translate.log', 120)
    stats = json.loads(stats_path.read_text()) if stats_path.is_file() else None
    image = p.CRTImage((WORK / 'original/msvcr90.dll').read_bytes())
    roots = {image.image_base + image.entry_rva} | {
        image.image_base + e['rva'] for e in image.exports() if e['kind'] == 'code'}
    table = work / 'generated/table.c'
    entries = set()
    if table.is_file():
        match = re.search(r'func_addrs\[\] = \{(.*?)\};', table.read_text(), re.S)
        if match:
            entries = {int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]+)', match.group(1))}
    gaps = sorted(roots - entries)
    log = (ROOT / step['log']).read_text()
    after = identities(patch)
    if after != before:
        raise ValueError('Inputs changed during strict attempt')
    result = dict(identity=before, round=args.round, step=step, strict=True, statistics=stats,
                  translation_gate=c.strict_gate(step['exit_code'], stats, gaps),
                  code_roots=len(roots), dispatch_entries=len(entries), root_gaps=gaps,
                  diagnostics=[line for line in log.splitlines() if any(s in line for s in
                               ('failed:', 'dispatches to', 'jump tables:', 'recovery errors',
                                'could not be modelled', 'TranslateError', 'FAILED'))],
                  artifacts=p.fingerprint([f for f in work.rglob('*') if f.is_file()]),
                  host_built=False, dll_executed=False)
    p.write_report(report, result)
    print(json.dumps({k: v for k, v in result.items() if k not in ('identity', 'statistics', 'artifacts', 'root_gaps', 'diagnostics')}, indent=2))
    return 0 if result['translation_gate'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--patch', type=Path)
    parser.add_argument('--round', type=int, choices=(0, 1), required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        return translate(args)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        parser.exit(1, 'Follow-up refused/failed: %s\n' % exc)


if __name__ == '__main__':
    sys.exit(main())
