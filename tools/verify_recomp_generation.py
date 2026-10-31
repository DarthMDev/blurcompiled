#!/usr/bin/env python3
"""Check raw dispatch definitions in the kit's generated C, without modifying it.

This is a structural check of the known generator format, not a C parser or a
claim that the translated routines implement the original program correctly.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

RAW_TABLE = re.compile(r'void\s*\(\*const\s+\w*raw_ptrs\[\]\)\(X86\s*\*\)\s*=\s*\{(.*?)\};', re.S)
DEFINITION = re.compile(r'\bvoid\s+fn_([0-9a-f]{8})\s*\(X86\s*\*\s*c\)\s*\{')


def verify(directory):
    """Require exactly one raw entry table and one body for each of its entries."""
    directory = Path(directory)
    table = (directory / 'table.c').read_text()
    matches = RAW_TABLE.findall(table)
    if len(matches) != 1:
        raise ValueError('Expected exactly one generated raw dispatch table')
    entries = re.findall(r'\bfn_([0-9a-f]{8})\b', matches[0])
    if not entries:
        raise ValueError('Generated raw dispatch table has no entries')
    paths = sorted(directory.glob('chunk_*.c')) + [directory / 'table.c']
    counts = Counter(address for path in paths for address in DEFINITION.findall(path.read_text()))
    missing = sorted(set(entries) - counts.keys())
    duplicates = sorted(address for address, count in counts.items() if count != 1)
    repeated_entries = sorted(address for address, count in Counter(entries).items() if count != 1)
    return dict(structural_check_passed=not (missing or duplicates or repeated_entries),
                entries=len(entries), definitions=len(counts), missing_definitions=missing,
                duplicate_definitions=duplicates, repeated_entries=repeated_entries,
                limitation='Dispatch coverage only; no behavioral, ABI or gameplay acceptance')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        result = verify(args.directory)
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + '\n')
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['structural_check_passed'] else 1)


if __name__ == '__main__':
    main()
