# PC Blur archive findings — 2026-10-06

This is a read-only research inspector, not an asset loader integrated into the
native host. It resolves archive names and index records. General extraction,
resource decoding and game execution remain unimplemented.

Follow-up on 2026-10-07: a separate bounded Python resource reader now verifies
v2 raw, zlib and chunked payloads. See [PAK_PAYLOADS.md](PAK_PAYLOADS.md) for
the later checksum findings, supported profiles and validation scope. The layout
and raw-field descriptions below record the earlier index research stage.

## Evidence baseline

All executable addresses below refer only to `Blur.exe` SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`,
x86 PE32, preferred base `0x400000`. Proposed names are research labels, not
original source symbols. Disassembly and Ghidra's inferred pseudocode stay in
ignored `reports/`; the project database stays in ignored `local/ghidra/`.

## Executable evidence and proposed research labels

Ghidra 12.1.4 default PE/x86 analysis completed in 281 seconds without reaching
its 600-second limit. Its database is `local/ghidra/blur-dba0264b.gpr`.
Exported instructions, xrefs and inferred pseudocode are in
`reports/ghidra-archive.txt`, `ghidra-archive-callers.txt`,
`ghidra-archive-layout.txt` and `ghidra-archive-strings.txt`.
Automatic analysis reported missing Windows libraries/PDB and unreadable-byte
warnings in an unrelated function at VA `0xcc74ae`. This is a partial manually
reviewed call graph, not proof of complete or correct whole-program analysis.

Every row below uses the executable SHA-256 stated above. **High static** means
instruction/xref behavior was inspected; **medium interpretation** means the
role is inferred from those instructions and archive bytes. Neither means the
original executable's function has been run and compared against native code.

| RVA | Preferred VA | Proposed label | Evidence | Confidence |
| --- | --- | --- | --- | --- |
| `0x8b3390` | `0xcb3390` | archive_open_candidate | Call at `0xcb33de` to metadata load; initializes stream through `0xcb6cb0` | Medium interpretation |
| `0x8b7010` | `0xcb7010` | load_archive_metadata | Reads initial 2048 bytes; calls header, names and index readers; rejects version >2 | High static |
| `0x8b8ad0` | `0xcb8ad0` | read_archive_header | Called at `0xcb705b`; reads first three words, calls magic check at `0xcb8bb7`, then extended metadata reader | High static |
| `0x8b87f0` | `0xcb87f0` | validate_magic_and_endian | Compares `0x50414b32` at `0xcb87ff`; bswaps magic/version/alignment for reversed signature | High static |
| `0x8b8f60` | `0xcb8f60` | read_extended_metadata | Called at `0xcb8c52`; skips 20 bytes for v1, reads four flag bytes, then words/64-bit value and v2 words | High static |
| `0x8b88f0` | `0xcb88f0` | read_names_from_stream | Called at `0xcb7193`; reads length, bytes, XORs them at `0xcb8987`, consumes alignment padding | High static |
| `0x8b84e0` | `0xcb84e0` | xor_name_bytes | Loads key at VA `0xefab60`; preincrements key index modulo 14, XORs each byte | High static |
| `0x8b8e80` | `0xcb8e80` | read_index_from_stream | Called at `0xcb71aa`; reads exactly two words, count and serialized size; v2 requires count × selected native stride | High static |
| `0x8b8560` | `0xcb8560` | select_native_record_stride | Selector 0 returns `0x28`, selector 1 returns `0x40`; called at `0xcb8efb` | High static |
| `0x8b8c70` | `0xcb8c70` | deserialize_record_array | Calls base reader at `0xcb8cad`, extended reader at `0xcb8d02`; destination steps 40/64 bytes | High static |
| `0x8b9180` | `0xcb9180` | deserialize_base_record | Reads four u32 then two u64; adds two u32 for version ≥2; serialized sizes 32/40 | High static |
| `0x8b9390` | `0xcb9390` | deserialize_extended_record | Calls base reader at `0xcb939e`, then reads one word +20 bytes; size 64 in v2 | High static |
| `0x8b3300` | `0xcb3300` | lookup_present_record_candidate | Tests record sector at +`0x18`/+`0x1c` for all-ones; multiplies it by stored alignment | High static / medium role |
| `0x8b85d0` | `0xcb85d0` | alignment_padding | Returns 0 when size divisible by alignment, otherwise alignment − remainder | High static |

The base/extended record readers use ECX as destination and pop two stack
arguments (`thiscall` shape); XOR uses two stack arguments with caller cleanup.
Other decompiler signatures remain provisional. No x86 object layout or pointer
is reused as a native struct.

The key storage at **RVA `0xafab60`**, VA `0xefab60`, contains the fourteen bytes
`78 56 58 6f 34 30 6a 33 40 24 25 5c 25 60` (followed by NUL). The loop starts with byte 1 and wraps modulo
14, giving the key reported below. This independently confirms the script's key.

Reviewed call chain: archive-open candidate → load metadata → header parse →
magic/endian validation and extended metadata; load metadata → name reader →
XOR; load metadata → index reader → record-array reader → base/extended record.
For little-endian v2, the index reader directly copies the verified serialized
array instead of invoking field-by-field deserialization. v1 expands 32-byte
serialized records into 40-byte in-memory records.

String leads were checked separately: `GAME:\cache\gamedata.pak` at RVA
`0xa67828` has a data reference from VA `0xf6d1cc`; `cache/gamedata.pak` at RVA
`0xa67af8` is referenced at VA `0x42d366` in the function starting at RVA
`0x2d160`. These are observed string/xref locations, with high confidence;
that startup function's full role is unverified. `mercury.pakfs` at RVA
`0xafa9f8` is another observed string but no direct xref was exported.
Static PE imports include `CreateFileA`, `ReadFile` and `SetFilePointer`; the
reviewed archive layer accesses a virtual stream, and its complete path to those
Win32 imports and its payload decompressor remain untraced.

## Format reference and license evaluation

Luigi Auriemma's [Blur script](https://mirror.aluigi.org/bms/blur.bms), version
0.3.3, was downloaded to `local/references/blur.bms`. Its downloaded byte SHA-256
is `d5d40fb3252133d8ee305f4846c736808d4d22e07ab5baefa03d43dc6c4ba38c`.
It supports `2KAP` and big-endian `PAK2`, and explicitly warns that different
versions may need workarounds. Only the PC `2KAP` profile was validated here.
QuickBMS itself was neither installed nor executed.

The script has no explicit license declaration. The author's
[About page](https://mirror.aluigi.org/about.htm) says material is GPL, or public
domain where unspecified; that general statement does not provide a clear
script-specific grant. No script source is copied into tracked files. The new
Python inspector implements structural observations checked against local bytes
and executable behavior. The XOR key is a file-format constant, independently
confirmed in the executable. Project licensing remains undecided.

The script consumes three initial index words, but executable code reads only
two before the records. The third word belongs to record 0. Its loop then reads
the next record's first word while ending the previous record; this preserves
most names but obscures the physical record boundary. Its zero-word heuristic
also reads 24 bytes beyond the declared index in five archives, where final
padding is zero. Gamedata and gamedata_boot really have an extra 24 bytes per
record. The inspector implements the executable's boundaries, checks the header
record selector against the declared index size, and does not adopt payload
workarounds or the conditional zero-word heuristic.

## Observed layout

Integers below are little-endian. The inspector supports versions 1 and 2,
2048-byte alignment, and the strides in the table below. Other profiles fail
explicitly, even if the original game might support them.

| File offset | Width | Interpretation / confidence |
| --- | --- | --- |
| `0x00` | 4 | `2KAP`; directly compared by executable, high |
| `0x04` | 4 | Version; executable branches on `<2`, high |
| `0x08` | 4 | Alignment; stored by header routine and used for rounding, high |
| `0x0c..0x37` | 44 | Raw header words; meanings not fully recovered |
| `0x38` | 4 | Inline name-table byte count when nonzero (observed v1) |
| `alignment` | 4 | Name-table byte count when inline count is zero (observed v2) |

Encoded name bytes immediately follow their count. They are NUL-separated strings
XORed with repeating bytes `56 58 6f 34 30 6a 33 40 24 25 5c 25 60 78`.
The index begins at the next alignment boundary after the name bytes. Its first
two words are record count and serialized record byte count; records follow at
index + 8. The second byte of extended metadata (`0x11` in v2, `0x25` in v1)
selects the record profile: 0 gives 40 native bytes, 1 gives 64 native bytes.
The observed v1 serialized record omits the last eight bytes and uses 32 bytes. All seven tables exactly fit their count and
stride; every name offset points to a terminated ASCII string start.

| Record-relative offset | Width | Current interpretation |
| --- | --- | --- |
| `0x00` | 4 | Unknown word, preserved; hash-like distribution is not proof of semantics |
| `0x04` | 4 | Offset into decoded name bytes |
| `0x08` | 4 | Unknown word, preserved |
| `0x0c` | 4 | Logical byte size; one zlib payload verifies this meaning |
| `0x10` | 8 | Raw value resembling a timestamp; epoch/meaning unverified |
| `0x18` | 8 | Sector number; multiply by alignment for payload start; all-ones is absent |
| `0x20` | 4 | v2 unknown word, preserved |
| `0x24` | 4 | v2 word; stored byte count in the verified direct-zlib sample only |
| `0x28` | 24 | Additional word + 20 bytes in 64-byte records; preserved raw |

An absent sector does not by itself establish a directory, deletion, alias or
empty file. Logical size is not treated as a stored-size bound. Payload starts
are checked against the file and metadata boundaries; complete payload ranges
and compression semantics are not established by the index inspector.

## Local validation

| Archive | Version | Names bytes | Index offset | Records | Stride | Present starts |
| --- | --- | --- | --- | --- | --- | --- |
| boot.pak | 2 | 40,923 | `0xa800` | 931 | 40 | 718 |
| gamedata.pak | 2 | 22,035 | `0x6000` | 621 | 64 | 589 |
| gamedata_boot.pak | 2 | 2,402 | `0x1800` | 80 | 64 | 48 |
| lut_rampak.pak | 2 | 7,595 | `0x2800` | 218 | 40 | 5 |
| root.pak | 2 | 170,406 | `0x2a800` | 3,601 | 40 | 3,388 |
| root_streaming.pak | 2 | 1,392 | `0x1000` | 68 | 40 | 67 |
| shaders-speed.pak | 1 | 28,853 | `0x7800` | 1,256 | 32 | 1,255 |

Total: 6,775 named records, 6,070 present payload starts. Reports include every
record, raw unknown fields, metadata digests and complete archive SHA-256.

| Archive | SHA-256 |
| --- | --- |
| boot.pak | `29822d286846b6335bab0d9ff5ac1f5481cba3ff5b68924e8397453047f2a8b2` |
| gamedata.pak | `07653df9f01b2865cb0081131ee97f7dc8393f723a142c30e6337b7862368a2e` |
| gamedata_boot.pak | `e609cf6af136eb345e83c8737acb3a3f0bf3df123c8fd4aec323e7c0fedc5e72` |
| lut_rampak.pak | `ab944e90d84c262684192ee0fd5bb70a5c6a6a441a6ea1305bff79fb8cb3a4ed` |
| root.pak | `10a60bd58a619aa7dc7d726febf145a0f28a30cd82fce0fade848fa24e83184f` |
| root_streaming.pak | `26f3ae66805c901852c6aa05ed6967a3f8e26cafc102ddf3fcfa78a3a32a728f` |
| shaders-speed.pak | `a8b8c509a0a9dbad6d380d1eb9f5e8c515974d9092901a2ec06808a607265a0e` |

One bounded in-memory experiment on gamedata record 0,
`\gamedata\events\am1_hard.csv`, read 226 bytes at `0x11800` (sector 35).
Python's zlib decoder reached end-of-stream, consumed all 226 bytes and produced
5,683 bytes, exactly the logical size. Output is comma-separated text with CRLF;
SHA-256 `fa6f960db57104f3538d76081a4d2d292590c87eef518be7c7f47eada235faf0`.
Only statistics are saved in `reports/pak/zlib-sample.json`; decoded asset bytes
were not saved. This confirms a direct-zlib storage case, not general extraction.

Other sampled payload starts differ: boot begins with words 65536 and 15891;
lut_rampak begins with 65536 and 38750; a root_streaming sample begins `BANK`;
a shader sample has a 3199 word before a zlib signature. Those observations
suggest multiple storage paths, but their complete interpretation is pending.

## Reproduction and bounds

```sh
python3 tools/inspect_pak.py '/path/to/Blur/cache/boot.pak' \
  --out reports/pak/boot.json
python3 -m unittest discover -s tests -v
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

CLI input is opened `rb`; output inside the reference installation or equal to
input is rejected. Name and index tables each have a 16 MiB limit, record count
100,000, string count 200,000 and name length 4096. Memory and iterations depend
on these limits, not payload size. The CLI separately streams the complete input
for its SHA-256. It assumes a stable read-only reference file during the run.

To reproduce the single zlib experiment after generating gamedata.json:

```python
import hashlib, json, zlib
r = json.load(open('reports/pak/gamedata.json'))
e = r['entries'][0]
assert e['word_24'] == 226 and e['payload_offset'] == 0x11800
with open('/path/to/Blur/cache/gamedata.pak', 'rb') as f:
    f.seek(e['payload_offset'])
    compressed = f.read(e['word_24'])
decoder = zlib.decompressobj()
plain = decoder.decompress(compressed, 1024 * 1024)
assert decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail
assert len(plain) == e['logical_size'] == 5683
assert hashlib.sha256(plain).hexdigest() == 'fa6f960db57104f3538d76081a4d2d292590c87eef518be7c7f47eada235faf0'
```

Next: trace the payload dispatch and chunk framing, then implement a bounded
single-resource reader with synthetic compressed fixtures and reference hashes.
Use a small CSV or LUT texture first. Verify extended metadata and special absent
records before treating every index entry as an independent resource.
