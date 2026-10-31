#!/usr/bin/env python3
"""Read-only Blur inventory and bounded PE32 import inspection; stdlib only."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct


class PE:
    def __init__(self, data):
        self.data = data
        if data[:2] != b'MZ':
            raise ValueError('Not an MZ image')
        pe = self.u32(0x3c)
        if self.read(pe, 4) != b'PE\0\0':
            raise ValueError('Missing PE signature')
        self.machine = self.u16(pe + 4)
        count = self.u16(pe + 6)
        opt_size = self.u16(pe + 20)
        opt = pe + 24
        if self.u16(opt) != 0x10b or opt_size < 112:
            raise ValueError('Only PE32 with import directory is supported')
        self.read(opt, opt_size)
        self.entry_rva = self.u32(opt + 16)
        self.image_base = self.u32(opt + 28)
        self.headers_size = self.u32(opt + 60)
        self.import_rva = self.u32(opt + 104) if self.u32(opt + 92) > 1 else 0
        self.import_size = self.u32(opt + 108) if self.import_rva else 0
        self.sections = []
        for i in range(count):
            off = opt + opt_size + i * 40
            name, virtual_size, rva, raw_size, raw_offset = struct.unpack('<8sIIII', self.read(off, 24))
            self.read(off, 40)
            self.read(raw_offset, raw_size)
            self.sections.append(dict(name=name.rstrip(b'\0').decode('ascii', 'replace'),
                                      rva=rva, virtual_size=virtual_size,
                                      raw_size=raw_size, raw_offset=raw_offset))

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValueError(f'File range out of bounds: {offset:#x}+{size:#x}')
        return self.data[offset:offset + size]

    def u16(self, off):
        return struct.unpack('<H', self.read(off, 2))[0]

    def u32(self, off):
        return struct.unpack('<I', self.read(off, 4))[0]

    def offset(self, rva):
        if 0 <= rva < min(self.headers_size, len(self.data)):
            return rva
        for s in self.sections:
            delta = rva - s['rva']
            if 0 <= delta < s['raw_size']:
                return s['raw_offset'] + delta
        raise ValueError(f'RVA not backed by file bytes: {rva:#x}')

    def string(self, rva):
        off = self.offset(rva)
        end = self.data.find(b'\0', off, min(off + 4096, len(self.data)))
        if end == -1:
            raise ValueError('Unterminated PE string')
        return self.data[off:end].decode('ascii', 'replace')

    def imports(self):
        result = []
        if not self.import_rva:
            return result
        for i in range(min(self.import_size // 20, 4096)):
            off = self.offset(self.import_rva + i * 20)
            original, stamp, chain, name, thunk = struct.unpack('<5I', self.read(off, 20))
            if not any((original, stamp, chain, name, thunk)):
                return result
            functions = []
            for j in range(65536):
                value = self.u32(self.offset((original or thunk) + 4 * j))
                if not value:
                    break
                functions.append(f'ordinal:{value & 0xffff}' if value & 0x80000000 else self.string(value + 2))
            else:
                raise ValueError('Unterminated import thunk table')
            result.append(dict(dll=self.string(name), functions=functions))
        raise ValueError('Unterminated import descriptor table')

    def report(self):
        return dict(machine=f'{self.machine:#06x}', architecture='x86' if self.machine == 0x14c else 'unknown',
                    image_base=f'{self.image_base:#x}', entry_rva=f'{self.entry_rva:#x}',
                    sections=self.sections, imports=self.imports(),
                    limitations='Static imports only; delay imports and runtime loads are not enumerated.')


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def inventory(root, hash_all=False):
    if not (root / 'Blur.exe').is_file():
        raise ValueError('Installation must contain Blur.exe')
    files = []
    binaries = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink() or not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        if any(part.startswith('.') for part in path.relative_to(root).parts):
            continue
        record = dict(path=rel, size=path.stat().st_size)
        suffix = path.suffix.lower()
        if hash_all or suffix in {'.exe', '.dll', '.asi', '.lua', '.luac', '.cfg'}:
            record['sha256'] = sha256(path)
        if suffix in {'.pak', '.level', '.levelstream', '.levelrender', '.levelphysics', '.sig', '.bik'}:
            with path.open('rb') as stream:
                record['header_32_hex'] = stream.read(32).hex()
        if suffix in {'.exe', '.dll', '.asi'}:
            try:
                binaries[rel] = PE(path.read_bytes()).report()
            except ValueError as exc:
                binaries[rel] = dict(error=str(exc))
        files.append(record)
    return dict(schema_version=1, file_count=len(files), total_bytes=sum(f['size'] for f in files),
                extensions=dict(sorted(Counter(Path(f['path']).suffix.lower() for f in files).items())),
                hash_policy='all files' if hash_all else 'executables, libraries, Lua, and config only',
                files=files, binaries=binaries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--hash-all', action='store_true', help='Also read/hash all large game assets')
    args = parser.parse_args()
    root = args.root.resolve()
    output = args.out.resolve()
    if output == root or root in output.parents:
        parser.error('Output must be outside the reference installation')
    try:
        report = inventory(root, args.hash_all)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + '\n')
    except (OSError, ValueError) as exc:
        parser.exit(1, f'Inspection failed: {exc}\n')
    print(f"Inventoried {report['file_count']} files / {report['total_bytes']:,} bytes -> {output}")


if __name__ == '__main__':
    main()

