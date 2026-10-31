"""Read explicitly bounded flat or byte-remapped tables from a pinned image.

The caller must independently establish the guard and instruction CFG. These
checks validate storage and targets; they do not infer a bound from code bytes.
"""


def finite_targets(read8, read32, base, count, start, end, byte_base=None):
    if not 1 <= count <= 256 or not 0 < start < end or base <= 0:
        raise ValueError('Invalid explicit finite table')
    if byte_base is not None and byte_base <= 0:
        raise ValueError('Invalid byte table base')
    targets = []
    for case in range(count):
        index = case if byte_base is None else read8(byte_base + case)
        if not isinstance(index, int) or not 0 <= index <= 255:
            raise ValueError('Unreadable byte table index')
        target = read32(base + 4 * index)
        if not isinstance(target, int) or not start <= target < end:
            raise ValueError('Unreadable or escaping finite target')
        targets.append(target)
    return targets
