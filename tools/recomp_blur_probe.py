#!/usr/bin/env python3
"""Preserve a bounded diagnostic run of the local translated Blur host.

This does not accept generated-code patches as recovered behavior. A successful
host exit or a presented buffer alone never passes a gameplay/translation gate.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re

try:
    from .recomp_crt_compare import run_bounded
except ImportError:
    from recomp_crt_compare import run_bounded

ROOT = Path(__file__).resolve().parents[1]
BLUR_SHA = "dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f"
CRT_SHA = "ed0170d3de86da33e02bfa1605eec8ff6010583481b1c530843867c1939d2185"


def digest(path):
    with Path(path).open('rb') as stream:
        h = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def observations(log):
    """Extract observations without promoting diagnostic counters to parity."""
    targets = sorted(set(re.findall(r'(?:call to unknown target |crt.guard.unknown_call target=)([0-9a-f]{8})', log)))
    frames = re.search(r'presented frames:\s+(\d+)', log)
    guest_exit = re.search(r'guest exit code:\s+(\d+)', log)
    fault = re.search(r'SIGSEGV in guest thread (\d+): EIP=([0-9a-f]{8})', log)
    return dict(
        presented_frames=int(frames.group(1)) if frames else None,
        guest_exit=int(guest_exit.group(1)) if guest_exit else None,
        uniform_frames='every presented frame was uniform' in log,
        host_signals=[int(s) for s in re.findall(r'\[host\] SIGNAL (\d+):', log)],
        last_guest_fault_context=(dict(thread=int(fault.group(1)), last_eip='0x' + fault.group(2),
                                       notice='Synchronized guest context; not necessarily the faulting instruction')
                                  if fault else None),
        missing_dispatch=[dict(va='0x' + t, rva=hex(int(t, 16) - 0x400000),
                               executable_sha256=BLUR_SHA,
                               evidence='host.log unknown-target diagnostic',
                               confidence='high for observed target; function behavior unverified')
                          for t in targets if 0x400000 <= int(t, 16) < 0x125b000],
        guards=[line for line in log.splitlines() if 'crt.guard.' in line],
        unimplemented_imports=sorted(set(re.findall(r'unimplemented import ([^:\n]+)', log))),
        parity_established=False, gameplay_established=False)


def probe(out, binary, seconds=5, frames=5, extra_code=None, strict=True, assets=None):
    out, binary = Path(out).absolute(), Path(binary).resolve()
    local = (ROOT / 'local').resolve()
    # Reject escapes and symlinks, including an existing output. Never reuse
    # profiles/logs or let an output path touch the installed reference game.
    if out.resolve() != out or not out.is_relative_to(local) or out == local:
        raise ValueError('A fresh, nonsymlink directory under local/ is required')
    if out.exists():
        raise ValueError('Probe directory already exists; preserve it and use a fresh path')
    if not 0 < seconds <= 60 or not 0 < frames <= 3600:
        raise ValueError('Startup probe bound is 60 seconds and 3600 frames')
    exe = ROOT / 'local/recomp-blur/original/Blur.exe'
    crt = ROOT / 'local/recomp-crt-msvc90-translation/original/msvcr90.dll'
    for path, expected in ((exe, BLUR_SHA), (crt, CRT_SHA)):
        if path.is_symlink() or digest(path) != expected:
            raise ValueError('Pinned input changed: ' + str(path))
    inputs = [exe, crt, binary, Path(__file__).resolve()]
    modules = ([extra_code] if isinstance(extra_code, (str, Path)) else extra_code) or []
    modules = [Path(p).resolve(strict=True) for p in modules]
    if any(':' in str(p) for p in modules):
        raise ValueError('Extra-code paths cannot contain the module-list separator')
    inputs.extend(modules)
    if assets:
        assets = Path(assets).resolve(strict=True)
        if not assets.is_dir():
            raise ValueError('Asset root must be a directory')
    before = {str(p.relative_to(ROOT)): digest(p) for p in inputs}
    out.mkdir(parents=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith('RECOMP_')}
    env.update(RECOMP_EXE=str(exe), RECOMP_NO_MODS='1',
               RECOMP_PROFILE_DIR=str(out / 'profile'), RECOMP_FRAMES=str(out / 'frames'),
               RECOMP_MAX_SECONDS=str(seconds), RECOMP_MAX_FRAMES=str(frames),
               RECOMP_LOG='1', RECOMP_IMPORT_STATS='1', RECOMP_DISCOVERY=str(out / 'discovery.txt'))
    if strict:
        env['RECOMP_STRICT_EXECUTION'] = '1'
    if modules:
        env['RECOMP_EXTRA_CODE'] = ':'.join(map(str, modules))
    if assets:
        env['RECOMP_ASSET_ROOT'] = str(assets)
        env['RECOMP_TRACE_FILES'] = '1'
    step = run_bounded([binary], out / 'host.log', seconds + 5, env)
    log = (out / 'host.log').read_text(errors='replace')
    after = {str(p.relative_to(ROOT)): digest(p) for p in inputs}
    result = dict(inputs=before, inputs_unchanged=before == after,
                  readonly_asset_root=str(assets) if assets else None,
                  strict_execution_requested=strict, step=step, observations=observations(log),
                  notice='Existing experimental host; source/build provenance and instruction semantics need separate validation. No port milestone inferred.')
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    if before != after:
        raise ValueError('Inputs changed during probe; run is invalid')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=ROOT / 'local/recomp-blur-msvcr90/build/recomp/pop_headless')
    parser.add_argument('--seconds', type=int, default=5)
    parser.add_argument('--frames', type=int, default=5)
    parser.add_argument('--extra-code', type=Path, action='append', help='load a module; repeat for multiple modules')
    parser.add_argument('--assets', type=Path, help='read-only installation fallback; requires the asset-root runtime patch')
    parser.add_argument('--diagnostic', action='store_true', help='Observe past missing-runtime guards; never acceptance')
    args = parser.parse_args()
    print(json.dumps(probe(args.out, args.binary, args.seconds, args.frames, args.extra_code,
                           strict=not args.diagnostic, assets=args.assets), indent=2))


if __name__ == '__main__':
    main()
