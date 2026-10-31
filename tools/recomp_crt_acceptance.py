"""Accept only the exact, reviewed metadata exceptions for one CRT translation."""
import json
from pathlib import Path
import re

try:
    from . import recomp_crt_compare as c
except ImportError:
    import recomp_crt_compare as c


def review(report, inventory, image, root):
    if report['identity']['candidate_sha256'] != inventory['candidate_sha256']:
        raise ValueError('Inventory candidate identity mismatch')
    import hashlib
    if hashlib.sha256(image.data).hexdigest() != inventory['candidate_sha256']:
        raise ValueError('Candidate bytes changed')
    for group in ('recovery_audit', 'export_root_audit', 'withdrawn_root_audit'):
        for row in inventory[group]:
            if row['candidate_sha256'] != inventory['candidate_sha256']:
                raise ValueError('Inventory row identity mismatch')
            if image.rva_read(row['rva'], len(bytes.fromhex(row['bytes']))).hex() != row['bytes']:
                raise ValueError('Inventory bytes changed')
            for ref in row['references']:
                path = root / ref['listing']
                if c.prior.digest(path) != ref['listing_sha256']:
                    raise ValueError('Inventory listing changed')
                if any(line not in path.read_text().splitlines() for line in ref['instructions']):
                    raise ValueError('Inventory reference absent')
    stats = report['statistics']
    if report['step']['exit_code'] or report['step']['timed_out']:
        raise ValueError('Translation did not complete')
    required = ('failures', 'unmodelled', 'table_gaps', 'table_sites_undecoded',
                'table_gaps_allowed', 'unmodelled_allowed')
    if any(k not in stats or stats[k] for k in required):
        raise ValueError('Failed, permissive or incomplete strict translation')
    expected_roots = {image.image_base + e['rva'] for e in inventory['export_root_audit']}
    if set(report['root_gaps']) != expected_roots:
        raise ValueError('Unexplained export dispatch gap')
    if stats['recover_errors'] != inventory['exact_recovery_diagnostics']:
        raise ValueError('Recovery diagnostic details changed')
    observed = {}
    for line in stats['recover_errors']:
        match = re.fullmatch(r'([0-9a-f]+): recovery abandoned at ([0-9a-f]+): unknown operand width (\d+)', line)
        if not match:
            raise ValueError('Unreviewed recovery diagnostic')
        key = int(match[1], 16) - image.image_base
        observed[key] = observed.get(key, 0) + 1
    expected = {e['rva']: e['occurrences'] for e in inventory['recovery_audit']}
    if observed != expected:
        raise ValueError('Recovery diagnostic inventory changed')
    withdrawn = {int(a, 16) - image.image_base for a, why, t in stats['blocks_withdrawn'] if why == 'data'}
    if withdrawn != {e['rva'] for e in inventory['withdrawn_root_audit']} or len(withdrawn) != len(stats['blocks_withdrawn']):
        raise ValueError('Unreviewed withdrawn candidate')
    if stats['functions_total'] - stats['functions_ok'] != len(withdrawn):
        raise ValueError('Unexplained non-emitted body')
    if image.image_base + image.entry_rva in expected_roots:
        raise ValueError('Actual entry cannot be classified as data')
    return dict(translation_gate=True, reviewed_recovery_events=sum(observed.values()),
                reviewed_data_exports=len(expected_roots), reviewed_withdrawn_guesses=len(withdrawn),
                notice='Static translation acceptance with exact data inventory; initialization, ABI and behavior untested.')


def main():
    p = c.prior
    root = p.ROOT
    report_path = root / 'reports/recomp-crt-msvc90-noreturn-round1.json'
    inv_path = root / 'recomp/crt-probe/msvc90-data-inventory.json'
    source = root / 'local/recomp-crt-msvc90-translation/original/msvcr90.dll'
    result = review(json.loads(report_path.read_text()), json.loads(inv_path.read_text()), p.CRTImage(source.read_bytes()), root)
    result['inputs'] = p.fingerprint([report_path, inv_path, source, Path(__file__).resolve()])
    p.write_report(root / 'reports/recomp-crt-msvc90-accepted-final.json', result)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
