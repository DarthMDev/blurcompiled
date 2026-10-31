#!/usr/bin/env python3
"""Bounded, read-only inspector for observed PC Blur 2KAP v1/v2 indexes.

No payload decoding or extraction. Field evidence and limits: docs/PAK_FORMAT.md.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

NAME_KEY = bytes.fromhex('56586f34306a334024255c256078')
MAX_NAMES = 16 * 1024 * 1024
MAX_INDEX = 16 * 1024 * 1024
MAX_ENTRIES = 100000
MAX_STRINGS = 200000
MAX_NAME_LENGTH = 4096
ABSENT_SECTOR = (1 << 64) - 1


def inspect(stream):
    """Inspect a seekable binary stream without reading asset payloads."""
    stream.seek(0, 2)
    file_size = stream.tell()

    def read(offset, size):
        if offset < 0 or size < 0 or offset > file_size or size > file_size - offset:
            raise ValueError(f'File range out of bounds: {offset:#x}+{size:#x}')
        stream.seek(offset)
        data = stream.read(size)
        if len(data) != size:
            raise ValueError('Short read (file may have changed)')
        return data

    header = read(0, 60)
    if header[:4] != b'2KAP':
        raise ValueError('Only observed little-endian PC 2KAP is supported')
    version, alignment = struct.unpack_from('<II', header, 4)
    if version not in (1, 2):
        raise ValueError('Unsupported archive version')
    # Conservative supported profile, not a claim that other alignments are invalid.
    if alignment != 2048:
        raise ValueError('Only observed 2048-byte alignment is supported')
    names_size = struct.unpack_from('<I', header, 56)[0]
    names_size_offset = 56
    if names_size == 0:
        names_size_offset = alignment
        names_size = struct.unpack('<I', read(names_size_offset, 4))[0]
    if not 0 < names_size <= MAX_NAMES:
        raise ValueError('Name table exceeds supported bounds or is empty')
    names_offset = names_size_offset + 4
    encoded = read(names_offset, names_size)
    names = bytes(b ^ NAME_KEY[i % len(NAME_KEY)] for i, b in enumerate(encoded))
    # Build valid string starts once, keeping per-entry work bounded for hostile inputs.
    strings = {}
    start = 0
    for end, value in enumerate(names):
        if end - start > MAX_NAME_LENGTH:
            raise ValueError('Name exceeds supported length')
        if value == 0:
            if len(strings) >= MAX_STRINGS:
                raise ValueError('Too many strings in name table')
            strings[start] = names[start:end]
            start = end + 1
    if start != len(names):
        raise ValueError('Unterminated name table')
    index_offset = (names_offset + names_size + alignment - 1) // alignment * alignment
    count, index_size = struct.unpack('<II', read(index_offset, 8))
    if count == 0 or count > MAX_ENTRIES or index_size > MAX_INDEX:
        raise ValueError('Index exceeds supported bounds or is empty')
    if index_size % count:
        raise ValueError('Index size is not a whole number of records')
    stride = index_size // count
    supported = (32,) if version == 1 else (40, 64)
    if stride not in supported:
        raise ValueError(f'Unsupported v{version} record stride: {stride}')
    selector = header[17 if version == 2 else 37]
    expected_stride = (40, 64)[selector] if selector in (0, 1) else None
    if version == 1:
        expected_stride = 32 if selector == 0 else None
    if stride != expected_stride:
        raise ValueError('Header record selector disagrees with index stride')
    raw_index = read(index_offset + 8, index_size)
    metadata_end = index_offset + 8 + index_size
    entries = []
    for i in range(count):
        offset = i * stride
        word_00, name_offset, word_08, size, raw_time, sector = struct.unpack_from('<IIIIQQ', raw_index, offset)
        if name_offset not in strings:
            raise ValueError(f'Entry {i}: name offset is not a terminated string start')
        try:
            name = strings[name_offset].decode('ascii')
        except UnicodeDecodeError as exc:
            raise ValueError(f'Entry {i}: non-ASCII name outside observed profile') from exc
        payload_offset = None if sector == ABSENT_SECTOR else sector * alignment
        if payload_offset is not None and not metadata_end <= payload_offset < file_size:
            raise ValueError(f'Entry {i}: payload start outside archive data region')
        # Logical size is not stored size; do not apply it as a compressed-file bound.
        entry = dict(name=name, name_offset=name_offset, logical_size=size,
                     word_00=word_00, word_08=word_08, raw_time_u64=raw_time,
                     sector_u64=sector, payload_offset=payload_offset)
        if version == 2:
            entry['word_20'], entry['word_24'] = struct.unpack_from('<II', raw_index, offset + 32)
        if stride == 64:
            entry['extra_24_hex'] = raw_index[offset + 40:offset + 64].hex()
        entries.append(entry)
    return dict(schema_version=1, file_size=file_size, version=version, alignment=alignment,
                header_hex=header.hex(), names_size_offset=names_size_offset,
                names_offset=names_offset, names_size=names_size, index_offset=index_offset,
                entry_count=count, index_size=index_size, record_stride=stride,
                record_selector=selector,
                encoded_names_sha256=hashlib.sha256(encoded).hexdigest(),
                index_sha256=hashlib.sha256(raw_index).hexdigest(), entries=entries,
                limitations='Index inspection only; unknown words retained raw; no payload decoding or extraction.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--reference-root', type=Path, default=Path('/Volumes/T7-Touch/Games/Blur'))
    args = parser.parse_args()
    source, output, root = args.archive.resolve(), args.out.resolve(), args.reference_root.resolve()
    if output == source or output == root or root in output.parents:
        parser.error('Output must be outside the reference installation and differ from the archive')
    try:
        with source.open('rb') as stream:
            report = inspect(stream)
        # Hash the complete input separately with bounded memory, for evidence identity.
        digest = hashlib.sha256()
        with source.open('rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(block)
        report['archive_sha256'] = digest.hexdigest()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + '\n')
    except (OSError, ValueError) as exc:
        parser.exit(1, f'Inspection failed: {exc}\n')
    print(f"{source.name}: v{report['version']}, {report['entry_count']} entries, "
          f"{report['record_stride']}-byte records -> {output}")


if __name__ == '__main__':
    main()
