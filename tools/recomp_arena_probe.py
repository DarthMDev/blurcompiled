"""Synthetic POSIX arena feasibility measurement, with no PE/DLL execution."""
import argparse
import json
import mmap
import platform
import resource
import sys
import time
from pathlib import Path

try:
    from . import recomp_crt_compare as c
except ImportError:
    import recomp_crt_compare as c


def measure(size):
    page = mmap.PAGESIZE
    if size not in (0x10100000, 0x785c3000):
        raise ValueError('Only the two compared arena sizes are allowed')
    def peak_bytes():
        return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss * (1 if sys.platform == 'darwin' else 1024)
    start = time.monotonic()
    before = peak_bytes()
    arena = mmap.mmap(-1, size, flags=mmap.MAP_PRIVATE | mmap.MAP_ANON,
                      prot=mmap.PROT_READ | mmap.PROT_WRITE)
    after_reserve = peak_bytes()
    # Touch a synthetic 16 MiB heap slice, candidate-sized high mapping, and
    # boundary pages. No source binary is read and no code is executed.
    ranges = [(0x02000000, 0x03000000), (size - 0xa3000, size), (0, page)]
    touched = 0
    for lo, hi in ranges:
        for offset in range(lo, hi, page):
            if arena[offset] != 0:
                raise ValueError('Fresh anonymous page was not zero')
            arena[offset] = 0xa5
            if arena[offset] != 0xa5:
                raise ValueError('Arena round trip failed')
            touched += page
    peak = peak_bytes()
    arena.close()
    return dict(arena_size=size, page_size=page, touched_bytes=touched,
                peak_rss_before_bytes=before, peak_rss_after_reserve_bytes=after_reserve,
                peak_rss_after_touch_bytes=peak, seconds=round(time.monotonic()-start, 4),
                unmapped=True, no_dll_executed=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--child', type=lambda v: int(v, 0))
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    if args.child is not None:
        print(json.dumps(measure(args.child)))
        return
    if args.out is None:
        parser.error('--out is required')
    out = c.prior.safe_output(args.out)
    if out.exists():
        parser.error('Report exists')
    cases = []
    for size in (0x10100000, 0x785c3000):
        for run in range(3):
            log = out.with_name(out.stem + '-%x-%d.log' % (size, run))
            step = c.run_bounded([sys.executable, Path(__file__).resolve(), '--child', hex(size)], log, 10)
            row = dict(step=step)
            if step['exit_code'] == 0:
                row['measurement'] = json.loads(log.read_text())
            cases.append(row)
    c.prior.write_report(out, dict(cases=cases, all_passed=all(x['step']['exit_code'] == 0 for x in cases),
                                   system=platform.platform(), architecture=platform.machine(),
                                   source_sha256=c.prior.digest(Path(__file__).resolve()),
                                   limits='Synthetic mmap matching pinned POSIX reserve flags. Peak RSS, not current RSS or committed memory. Does not establish loader/CRT initialization, full-arena pressure, mobile feasibility or concurrency capacity.'))


if __name__ == '__main__':
    main()
