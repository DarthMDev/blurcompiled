#!/usr/bin/env python3
"""Read and verify bounded PC 2KAP v2 raw, zlib and chunked resources in memory.

Writes only JSON statistics, never decoded assets. See docs/PAK_PAYLOADS.md.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

try:
    from .inspect_pak import inspect
except ImportError:
    from inspect_pak import inspect

MAX_STORED = 16 * 1024 * 1024
MAX_DECODED = 32 * 1024 * 1024
MAX_TOTAL = 256 * 1024 * 1024
CHECKSUM_BLOCK = 0x8000


def content_checksum(data):
    """Sum unfinalized CRC32 values for independent 32 KiB decoded blocks."""
    return sum(zlib.crc32(data[i:i + CHECKSUM_BLOCK]) ^ 0xffffffff
               for i in range(0, len(data), CHECKSUM_BLOCK)) & 0xffffffff


def decode_zlib(stored, logical_size):
    if not 0 < len(stored) <= MAX_STORED or not 0 < logical_size <= MAX_DECODED:
        raise ValueError('Resource exceeds supported size bounds or is empty')
    decoder = zlib.decompressobj()
    try:
        plain = decoder.decompress(stored, logical_size + 1)
    except zlib.error as exc:
        raise ValueError('Invalid zlib stream') from exc
    if len(plain) != logical_size:
        raise ValueError('Decoded size differs from index logical size')
    if not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError('Zlib stream incomplete, over limit, or has trailing input')
    return plain


def decode_chunks(stored, logical_size):
    """Observed selector-4 profile: 64 KiB blocks, raw DEFLATE or -1/raw."""
    if not 0 < len(stored) <= MAX_STORED or not 0 < logical_size <= MAX_DECODED:
        raise ValueError('Resource exceeds supported size bounds or is empty')
    if len(stored) < 4 or struct.unpack_from('<I', stored)[0] != 0x10000:
        raise ValueError('Unsupported chunk header')
    result = bytearray()
    offset = 4
    while len(result) < logical_size:
        if len(stored) - offset < 4:
            raise ValueError('Missing chunk length')
        size = struct.unpack_from('<I', stored, offset)[0]
        offset += 4
        raw = size == 0xffffffff
        size = 0x10000 if raw else size
        if size == 0 or size > 0x10000 or size > len(stored) - offset:
            raise ValueError('Chunk range outside stored resource')
        block = stored[offset:offset + size]
        expected = min(0x10000, logical_size - len(result))
        if raw:
            plain = block
        else:
            decoder = zlib.decompressobj(-15)
            try:
                plain = decoder.decompress(block, expected + 1)
            except zlib.error as exc:
                raise ValueError('Invalid raw-DEFLATE chunk') from exc
            if not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
                raise ValueError('Chunk stream incomplete or has trailing input')
        if len(plain) != expected:
            raise ValueError('Decoded chunk size differs from expected size')
        result.extend(plain)
        offset += size
    if offset != len(stored):
        raise ValueError('Trailing chunk bytes')
    return bytes(result)


def read_resource(stream, archive, entry_index):
    """Use an index produced by inspect() for this stable, seekable input."""
    header = bytes.fromhex(archive['header_hex'])
    selector = header[16]
    if archive['version'] != 2 or selector not in (1, 2, 4):
        raise ValueError('Only observed v2 storage selectors 1, 2 and 4 are supported')
    if not 0 <= entry_index < len(archive['entries']):
        raise ValueError('Entry index out of range')
    entry = archive['entries'][entry_index]
    offset = entry['payload_offset']
    if offset is None:
        raise ValueError('Entry has no payload sector')
    name_crc = zlib.crc32(entry['name'].encode('ascii')) ^ 0xffffffff
    if name_crc != entry['word_00']:
        raise ValueError('Name CRC differs from record')
    stored_size, logical_size = entry['word_24'], entry['logical_size']
    if not 0 < stored_size <= MAX_STORED or not 0 < logical_size <= MAX_DECODED:
        raise ValueError('Resource exceeds supported size bounds or is empty')
    stream.seek(0, 2)
    actual_size = stream.tell()
    metadata_end = archive['index_offset'] + 8 + archive['index_size']
    # A subsequent payload start is an independent upper bound, not an inferred length.
    next_start = min((e['payload_offset'] for e in archive['entries']
                      if e['payload_offset'] is not None and e['payload_offset'] > offset),
                     default=actual_size)
    if (actual_size != archive['file_size'] or offset < metadata_end or
            offset > next_start or stored_size > next_start - offset):
        raise ValueError('Stored range crosses archive or next resource boundary')
    stream.seek(offset)
    stored = stream.read(stored_size)
    if len(stored) != stored_size:
        raise ValueError('Short payload read')
    if selector == 1:
        if stored_size != logical_size:
            raise ValueError('Raw resource stored and logical sizes differ')
        plain = stored
    elif selector == 2:
        plain = decode_zlib(stored, logical_size)
    else:
        plain = decode_chunks(stored, logical_size)
    checksum = content_checksum(plain)
    if checksum != entry['word_20']:
        raise ValueError('Decoded content checksum differs from record')
    return plain, dict(entry_index=entry_index, name=entry['name'], payload_offset=offset,
                       stored_size=stored_size, decoded_size=len(plain), storage_selector=selector,
                       name_crc_u32=name_crc, content_checksum_u32=checksum,
                       stored_sha256=hashlib.sha256(stored).hexdigest(),
                       decoded_sha256=hashlib.sha256(plain).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--entry-index', type=int)
    group.add_argument('--all', action='store_true', help='Verify all present records within aggregate bounds')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--reference-root', type=Path, default=Path('/Volumes/T7-Touch/Games/Blur'))
    args = parser.parse_args()
    source, output, root = args.archive.resolve(), args.out.resolve(), args.reference_root.resolve()
    if output == source or output == root or root in output.parents:
        parser.error('Output must be outside the reference installation and differ from input')
    try:
        summaries = []
        with source.open('rb') as stream:
            archive = inspect(stream)
            indexes = ([i for i, e in enumerate(archive['entries']) if e['payload_offset'] is not None]
                       if args.all else [args.entry_index])
            total_stored = total_decoded = 0
            for i in indexes:
                if not 0 <= i < len(archive['entries']):
                    raise ValueError('Entry index out of range')
                e = archive['entries'][i]
                total_stored += e.get('word_24', 0)
                total_decoded += e['logical_size']
                if max(total_stored, total_decoded) > MAX_TOTAL:
                    raise ValueError('Aggregate verification byte limit exceeded')
                plain, summary = read_resource(stream, archive, i)
                summaries.append(summary)
                del plain
            stream.seek(0)
            digest = hashlib.sha256()
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(block)
        report = dict(schema_version=1, archive_sha256=digest.hexdigest(),
                      storage_profile='PC 2KAP v2 selectors 1/raw, 2/zlib, 4/chunked',
                      verified_count=len(summaries), total_stored=total_stored,
                      total_decoded=total_decoded, resources=summaries,
                      limitations='In-memory resource verification only; extended metadata authenticity, empty resources, v1 and other storage profiles unsupported.')
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + '\n')
    except (OSError, ValueError) as exc:
        parser.exit(1, f'Resource verification failed: {exc}\n')
    print(f'{source.name}: verified {len(summaries)} resources / {total_decoded:,} decoded bytes -> {output}')


if __name__ == '__main__':
    main()
