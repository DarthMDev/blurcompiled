#!/usr/bin/env python3
"""Structural gates for independently decoded bounded table-owner listings.

No instruction recovery, instruction semantics or runtime bypass lives here.
Callers supply independently decoded (address, byte length) pairs and the
original baseline boundaries that a complete replacement must preserve.
"""


def check_instruction_spans(spans, start, end, baseline=(), entries=(), tables=(), max_bytes=8192):
    """Reject overlaps, missing baseline entries and escaping table targets.

    Gaps are permitted only at this byte-identity layer. The existing bounded
    emitter must separately prove actual fallthrough and branch closure.
    """
    if max_bytes not in (4096, 8192, 16384):
        raise ValueError('Invalid explicit recovery budget')
    if not 0 < end - start <= max_bytes or not spans or spans[0][0] != start:
        raise ValueError('Invalid bounded owner extent')
    previous_end = start
    addresses = set()
    for address, size in spans:
        if not 1 <= size <= 15 or address < previous_end or address + size > end:
            raise ValueError('Overlapping, unsorted, or out-of-bounds instruction')
        addresses.add(address)
        previous_end = address + size
    if not set(baseline).issubset(addresses):
        raise ValueError('Replacement drops baseline instruction boundaries')
    if not set(entries).issubset(addresses):
        raise ValueError('Replacement drops an existing dispatch entry')
    for site, targets in tables:
        if site not in addresses or not targets or not set(targets).issubset(addresses):
            raise ValueError('Table site or target is not a recovered instruction boundary')
    return addresses
