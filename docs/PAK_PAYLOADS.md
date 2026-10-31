# Bounded resource decoding — 2026-10-07

The new `tools/read_pak_resource.py` is a Python research implementation. It
reads individual resources into bounded memory and writes verification statistics
only. It is not yet a C++ engine asset service. The SDL/Metal bootstrap is unchanged;
no gameplay, Amax connection or Discord integration was added.

Later native follow-up: [NATIVE_ASSET_SLICE.md](NATIVE_ASSET_SLICE.md) records
the C++ implementation, identical resource hashes and one DDS LUT rendered
through Metal. The text below preserves the preceding Python research stage.

## Identity and executable evidence

All executable RVAs below apply only to `Blur.exe` SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`,
preferred base `0x400000`. This hash was rechecked before analysis. Archive hashes
match the identities in [PAK_FORMAT.md](PAK_FORMAT.md).

Ghidra exports in ignored `reports/ghidra-payload*.txt` contain instructions,
references and **inferred pseudocode**, not recovered source. Its automatic pass
had missed several functions referenced by stream vtables. The new hash-guarded
`tools/ghidra/SeedArchiveFunctions.java` disassembled and created those specific
functions in the local database. No vtable/data bytes were converted into native
pointers. Several Ghidra parameter types/order remain incorrect or uncertain;
roles below rely on narrow instruction behavior and archive verification.

| RVA | Preferred VA | Proposed research label | Evidence | Confidence |
| --- | --- | --- | --- | --- |
| `0x8b7eb0` | `0xcb7eb0` | resource_stream_dispatch | Compares storage byte with 1/2/4 and calls constructors at `0xcb7f9f`, `0xcb7f84`, `0xcb7f66`; uses record +`0x24` as stored length and sector × alignment | High static branch behavior; provisional signature |
| `0x8b94c0` | `0xcb94c0` | raw_stream_constructor | Selected for 1; installs vtable at RVA `0xafab70` | High static; raw storage validated on one resource |
| `0x8b9660` | `0xcb9660` | zlib_stream_constructor | Selected for 2; installs vtable at RVA `0xafab84` | High static |
| `0x8b96c0` | `0xcb96c0` | initialize_zlib_read | Vtable +4; sets stream input offset/length; calls inflate-init wrapper with version string 1.2.1 and x86 stream size `0x38` | High static |
| `0x8b9830` | `0xcb9830` | read_zlib_output | Vtable +`0x0c`; fills input in up to 32 KiB blocks; calls inflate routine and tests error/end status | High static; equivalent payload output validated on 637 resources |
| `0x6dd640` | `0xadd640` | inflate_init_wrapper | Calls next initialization routine with window bits 15 | High static; naming inferred from arguments and successful zlib reference decode |
| `0x8b9b30` | `0xcb9b30` | chunk_stream_constructor | Selected for 4; installs vtable at RVA `0xafac24`; initializes 64 KiB buffers | High static |
| `0x8b9be0` | `0xcb9be0` | initialize_chunk_read | Vtable +4; reads leading word, starts stored-byte accounting; uses constant 64 KiB output size | High static |
| `0x8b9df0` | `0xcb9df0` | read_chunk_output | Vtable +`0x0c`; reads per-chunk length, maps all-ones to 64 KiB input, dispatches work and copies decoded 64 KiB blocks | High static framing; asynchronous worker details unported |
| `0x8b6460` | `0xcb6460` | build_crc32_table | Uses reflected polynomial `0xedb88320` over 256 table entries | High static |
| `0x8b6720` | `0xcb6720` | lookup_path_crc_candidate | Initializes CRC to all-ones, hashes normalized path without final XOR, passes result to lookup | High static CRC; full normalization/collision behavior unported |
| `0x8b7340` | `0xcb7340` | verify_resource_content_candidate | Reads decoded data in 32 KiB blocks, resets CRC for each block, sums results, compares record +`0x20` | High static; checksum calculation validated on 1,362 resources |

The record's first word matches the **unfinalized reflected CRC32 of the stored
ASCII path** for every resource verified in this pass. Python equivalent:
`zlib.crc32(path_bytes) ^ 0xffffffff`. Lookup normalization for arbitrary user
paths, hash collisions, special absent records and empty names are not recovered.
The API selects an exact index record, bypassing that unresolved lookup behavior.

Record +`0x20` is the content checksum for the verified v2 profiles. Split decoded
bytes into 32 KiB blocks, compute an independent unfinalized CRC32 for each,
then sum modulo 2^32. This is **not** the CRC32 of the concatenated resource.
Record +`0x24` supplies stored bytes for these profiles. The 24-byte extended
record metadata remains opaque; content checksums do not establish authenticity.

## Supported storage and bounds

Only PC little-endian `2KAP` version 2 with storage byte 1, 2 or 4 is supported.
The storage byte is at file offset `0x10`; it is distinct from the record-stride
selector at `0x11`.

- **1/raw:** stored size must equal logical size; resource bytes are copied.
- **2/zlib:** one complete zlib stream starts directly at sector × alignment.
  Decoded size must exactly match the index; trailing/concatenated/incomplete
  streams fail. The previously verified CSV retains its recorded SHA-256.
- **4/chunked:** observed leading word is 65536, followed by repeated u32 stored
  lengths and raw-DEFLATE blocks. Full decoded blocks are 64 KiB; the final block
  equals the remaining logical size. All-ones length denotes a raw 64 KiB block.
  The leading word's broader semantics remain uncertain; other values fail.
  The asynchronous original stream is modeled as serial decoding. Synthetic
  fixtures cover mixed compressed/raw blocks; full original scheduling is unported.

Limits: 16 MiB stored and 32 MiB decoded per resource, 64 KiB per stored chunk,
256 MiB aggregate stored or decoded for a CLI run. Table bounds remain those of
the inspector. Stored ranges must fit the actual file and end before a following
payload sector. Output cannot exceed declared logical size plus one detection
byte during decompression. Every chunk must advance the bounded input/output;
extra bytes and wrong final sizes fail. Zero-length and absent resources fail
explicitly. The reference input must remain stable during inspection and decoding.

The Python module returns decoded bytes for future consumers; its CLI never
writes resource bytes. Only JSON summaries (source archive hash, name, offset,
sizes, checksums and stored/decoded SHA-256) are written outside the reference
installation. No third-party script code was incorporated.

## Supplied-file verification

| Archive | Profile | Verified resources | Stored bytes | Decoded bytes | Coverage |
| --- | --- | --- | --- | --- | --- |
| gamedata.pak | 2/zlib | 589 | 21,784,191 | 49,228,413 | All present records |
| gamedata_boot.pak | 2/zlib | 48 | 1,812,546 | 5,749,471 | All present records |
| boot.pak | 4/chunked | 718 | 72,066,110 | 150,415,156 | All present records |
| lut_rampak.pak | 4/chunked | 5 | 384,711 | 656,000 | All present records |
| root.pak | 4/chunked | 1 | 158,890 | 183,130 | Record 0 only |
| root_streaming.pak | 1/raw | 1 | 10,905,600 | 10,905,600 | Record 0 only |

**1,362 resources / 217,137,770 decoded bytes** passed name CRC, complete stored
range/stream consumption, logical size and content checksum checks. No resources
were executed. Raw storage has one supplied-file sample; root has one sample.
V1 shaders and the remaining root/streaming resources are unverified by this
reader. Validation is against file metadata and the original checksum algorithm;
the PC game's runtime output was not captured or compared.

The five LUT resources have `DDS ` signatures. Each decodes to 131,200 bytes
(64 KiB +64 KiB +128) and passes its content checksum. Example:
`\graphics\textures\postprocess\showroom_lut_02.dds`, SHA-256
`0535459ef7b06100712cb56b1022ce25197c4020b522f5e7c2686d9013e65039`.
Its compressed chunk lengths are 38,750, 38,904 and 133 bytes. DDS pixel layout,
texture interpretation and rendering are not yet implemented. The root sample's
name ends `.model`; no geometry interpretation follows from successful decoding.
The raw streaming sample begins `BANK`; its audio resource format remains opaque.

## Reproduction

```sh
python3 tools/read_pak_resource.py '/path/to/Blur/cache/gamedata.pak' \
  --all --out reports/pak/gamedata-resources.json
python3 tools/read_pak_resource.py '/path/to/Blur/cache/boot.pak' \
  --all --out reports/pak/boot-resources.json
python3 tools/read_pak_resource.py '/path/to/Blur/cache/lut_rampak.pak' \
  --all --out reports/pak/lut_rampak-resources.json
python3 tools/read_pak_resource.py '/path/to/Blur/cache/root_streaming.pak' \
  --entry-index 0 --out reports/pak/root_streaming-resource-0.json
python3 -m unittest discover -s tests -v
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

The 31 tests include synthetic zlib/chunked/raw resources, the standard CRC test
vector, multi-block checksum reset, corrupt/truncated/trailing streams, output
limits, metadata mismatch, invalid selectors and following-resource boundaries.
No copyrighted asset bytes are used as test fixtures.

For additional Ghidra reproduction use the existing local headless command from
RE_WORKFLOW.md with `-process Blur.exe -noanalysis`, these target groups and the
corresponding exporter output paths:

```text
reports/ghidra-payload.txt:
  cb9760 cb9c70 cb7510 cb6720 cb7340 cb6560
reports/ghidra-payload-dispatch.txt:
  cb7eb0 cb6460
reports/ghidra-payload-codecs.txt:
  cb94c0 cb9660 cb9b30 adeab0 add640
reports/ghidra-payload-streams.txt:
  cb96c0 cb9810 cb9830 cb9be0 cb9d50 cb9df0
```

For the last group, run `-postScript SeedArchiveFunctions.java` with those six
addresses before `-postScript ArchiveEvidence.java output-path ...`. Seeding
modifies only the ignored Ghidra database. Routine labels above are recorded in
this notebook, not asserted as original symbols.

Next technical slice: port the bounded payload codecs and checksum calculation
to pure C++ with the same synthetic fixtures and reference output hashes, then
inspect one LUT's DDS header and pixel data for a native texture render. Keep
extended metadata and unsupported profiles explicit rather than generalizing
from these successful resources.
