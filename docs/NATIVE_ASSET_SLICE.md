# Native asset slice — 2026-10-07

The ARM64 research host now reads a supplied PC archive, decodes a verified
resource in C++, parses a narrow DDS volume profile and displays its slices using
SDL3's Metal renderer. This is an asset-access/render slice, not gameplay or a
reconstructed engine. No Amax/Discord integration or original executable execution
is involved.

## Implementation and evidence

`src/assets/assets.cpp` is new C++20 code, using standard seekable streams,
explicit fixed-width little-endian reads and the installed native zlib library.
It contains no SDL, Metal, Win32, Objective-C, x86 structures or host-pointer file
casts. Platform rendering remains in `src/main.cpp`.

The recovered archive behavior is linked to the exact executable baseline:
`Blur.exe` SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
The existing notebooks record instruction/xref evidence, confidence and provisional
types for these RVAs; no new original symbols are asserted by the C++ names.

| New implementation | Baseline RVA evidence | Confidence / practical limit |
| --- | --- | --- |
| `read_index` | `0x8b84e0`, `0x8b8e80`, `0x8b8560`, `0x8b9180`, `0x8b9390` | Static layout and decoded-name evidence; native output parity on selected v2 resources |
| `decode_payload` | `0x8b7eb0`, `0x8b9830`, `0x8b9be0`, `0x8b9df0` | Storage dispatch/framing plus native/Python decoded SHA-256 agreement; asynchronous original worker not ported |
| `path_crc`, `content_checksum` | `0x8b6460`, `0x8b6720`, `0x8b7340` | Known CRC vector, synthetic multi-block tests and all selected resources verified; arbitrary lookup normalization unported |
| `dds_volume_atlas` | No recovered game symbol | New implementation of a standard DDS profile, tested against synthetic pixels and an independent Python mapping |
| SDL texture upload/readback | No recovered game symbol | New rendering adapter; actual Metal framebuffer pixels verified |

See [PAK_FORMAT.md](PAK_FORMAT.md) and [PAK_PAYLOADS.md](PAK_PAYLOADS.md) for
archive hashes, RVA evidence and prior format research. Disassembly/pseudocode
remain in ignored `reports/`; the Ghidra database remains under ignored `local/`.

## DDS observation and interpretation

The resource `\graphics\textures\postprocess\showroom_lut_02.dds` is record 65
in the supplied `cache/lut_rampak.pak` (archive SHA-256
`ab944e90d84c262684192ee0fd5bb70a5c6a6a441a6ea1305bff79fb8cb3a4ed`).
Its decoded SHA-256 is
`0535459ef7b06100712cb56b1022ce25197c4020b522f5e7c2686d9013e65039`.

Observed DDS fields:

| File offset | Observed value | Interpretation |
| --- | --- | --- |
| `0x00` | `DDS ` | Legacy DDS signature |
| `0x04` | 124 | Header size |
| `0x08` | `0x801007` | Includes depth field flag |
| `0x0c`, `0x10`, `0x18` | 32, 32, 32 | Height, width, volume depth |
| `0x14` | 0 | No explicit row pitch supplied |
| `0x1c` | 0 | No mip count supplied |
| `0x4c`, `0x50`, `0x54`, `0x58` | 32, `0x40`, 0, 32 | Pixel-format size, RGB flag, no FourCC, 32 bits per pixel |
| `0x5c`, `0x60`, `0x64`, `0x68` | `0xff0000`, `0xff00`, `0xff`, 0 | R/G/B masks, no declared alpha |
| `0x70` | `0x200000` | Volume capability |

Field meanings follow Microsoft's [DDS_HEADER](https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-header)
and [DDS_PIXELFORMAT](https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-pixelformat)
references. No third-party DDS implementation was copied. The exact size is
128-byte prefix +32×32×32×4 bytes. The new reader interprets the data as tightly
packed little-endian BGRX, converts it to opaque RGBA, and arranges depth slices
in eight columns and four rows. The unused high source byte is ignored because
no alpha mask is declared.

This produces a 256×128 atlas, RGBA byte SHA-256
`d7d449207164fb3b40dd44d7c4e121231cca273e74794e3953b980b7d09459e9`.
An independent Python mapping matched every byte. DDS channel interpretation is
based on standard masks; the game's color-space, LUT sampling, orientation and
actual postprocessing application remain untraced. The atlas is a research view,
not the original color-grading effect.

## Verification actually performed

- Debug Mach-O ARM64 builds of `blur-native` and `blur-asset-probe` succeeded
  using AppleClang and the installed SDK zlib 1.2.12; no dependencies downloaded.
- 39 unittest tests passed, including eight native test methods with multiple
  synthetic codec, index and DDS cases. Both CTest checks passed.
- `tools/verify_native_assets.py` compared C++ decoded output length and SHA-256
  against the recorded Python results for **1,362 resources / 217,137,770 bytes**:
  all present boot, gamedata, gamedata_boot and LUT records, plus one root and one
  raw root_streaming record. Complete source archive hashes were checked first.
- The native host loaded the LUT with `--lut --probe`, without invoking Python.
- The actual SDL **Metal** renderer presented five frames. Before first present,
  framebuffer readback matched all **32,768 atlas texels**, sampling the center
  of each 2×2 nearest-neighbor block; channel tolerance was one byte value.
- A 2200×1360 framebuffer capture is saved locally at `reports/lut-render.bmp`,
  with a PNG preview at `reports/lut-render.png`. All assets/captures/reports remain
  ignored. The reference installation was opened read-only.

Native/Python parity establishes agreement for these files and algorithms, not
comparison against a captured PC game's runtime. No Linux build was performed.

## Bounds and supported profiles

The C++ index reader currently supports observed v2 `2KAP`, 2048-byte alignment,
external XOR names tables and 40/64-byte records. V1 and inline names profiles
fail explicitly. It keeps the Python table/count/name limits and detects sector
multiplication overflow. Unknown record fields are not given invented meanings;
only fields required by this slice are retained in native entries.

Payload decoding supports selectors 1/raw, 2/zlib, 4/chunked. Stored and decoded
resource limits are 16 MiB and 32 MiB, respectively; per-chunk stored limit is
64 KiB. Sizes, stream consumption, name CRC, content checksum and following-sector
bounds must pass. Empty/absent resources and other modes remain unsupported.
Resource selection in the host uses an exact name and rejects duplicates, without
assuming the game's CRC-based normalized lookup behavior.

DDS support is intentionally narrow: legacy RGB32 volume, observed BGR masks,
no alpha or mips, tight rows (or width×4 pitch), dimensions at most 256×256×64,
exact data length and bounded RGBA atlas. DX10, BC compression, cube maps, 2D
formats, mip chains, padded rows and general channel masks fail explicitly.

## Reproduction

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build -j 4
python3 -m unittest discover -s tests -v
ctest --test-dir build --output-on-failure
python3 tools/verify_native_assets.py '/path/to/Blur'
./build/blur-native --assets '/path/to/Blur' --lut --probe
./build/blur-native --assets '/path/to/Blur' --lut --verify-lut-render --frames 5
./build/blur-native --assets '/path/to/Blur' --lut --lut-snapshot reports/lut-render.bmp --frames 5
```

The native probe is a read-only research tool. `--resource ARCHIVE INDEX` writes
decoded bytes to stdout; `--codec SELECTOR SIZE CHECKSUM` decodes bounded binary
stdin; `--dds` converts a DDS stdin resource into RGBA atlas bytes on stdout.
Avoid redirecting decoded bytes into tracked source or the reference installation.
The comparison tool captures bytes in memory and saves metadata only. Unit tests
use synthetic temporary files; native tests can be skipped by unittest when the
probe has not been built, while CTest supplies the configured executable path.

Next: recover a mesh/resource's internal structure and produce one bounded native
geometry decode, using synthetic fixtures and observed counts/offsets. The LUT
slice proves an asset can reach a native renderer; it does not establish any
vehicle/track geometry, material system, camera or simulation.
