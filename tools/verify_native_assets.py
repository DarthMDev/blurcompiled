#!/usr/bin/env python3
"""Compare native decoded resource bytes with existing ignored Python SHA-256 reports."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

try:
    from .inspect_install import sha256
except ImportError:
    from inspect_install import sha256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--probe', type=Path, default=Path('build/blur-asset-probe'))
    parser.add_argument('--reports', type=Path, default=Path('reports/pak'))
    parser.add_argument('--out', type=Path, default=Path('reports/native-assets.json'))
    args = parser.parse_args()
    root, output = args.root.resolve(), args.out.resolve()
    if output == root or root in output.parents:
        parser.error('Output must be outside the reference installation')
    groups = [('gamedata', 'gamedata-resources.json'), ('gamedata_boot', 'gamedata_boot-resources.json'),
              ('boot', 'boot-resources.json'), ('lut_rampak', 'lut_rampak-resources.json'),
              ('root', 'root-resource-0.json'), ('root_streaming', 'root_streaming-resource-0.json')]
    results = []
    try:
        for name, filename in groups:
            expected = json.loads((args.reports / filename).read_text())
            archive = root / 'cache' / (name + '.pak')
            identity = sha256(archive)
            if identity != expected['archive_sha256']:
                raise ValueError(f'{name}: archive hash differs from reference report')
            for e in expected['resources']:
                run = subprocess.run([str(args.probe.resolve()), '--resource', str(archive), str(e['entry_index'])],
                                     capture_output=True, check=True, timeout=30)
                if len(run.stdout) != e['decoded_size'] or hashlib.sha256(run.stdout).hexdigest() != e['decoded_sha256']:
                    raise ValueError(f"{name}: entry {e['entry_index']} differs from Python reference")
            results.append(dict(archive=name, archive_sha256=identity, matched_count=len(expected['resources']),
                                decoded_bytes=expected['total_decoded']))
            print(f"{name}: {len(expected['resources'])} native resource hashes match", flush=True)
        report = dict(schema_version=1, native_probe_sha256=sha256(args.probe.resolve()),
                      matched_count=sum(r['matched_count'] for r in results),
                      decoded_bytes=sum(r['decoded_bytes'] for r in results), archives=results,
                      limitations='Native/Python parity against local archive bytes; no original runtime comparison or gameplay.')
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + '\n')
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        parser.exit(1, f'Native comparison failed: {exc}\n')
    print(f"Matched {report['matched_count']} resource hashes -> {output}")


if __name__ == '__main__':
    main()
