# CRT architecture decision input — 2026-10-08

The user requested an Astra High planning checkpoint before a major architecture
decision. This is that checkpoint: the narrow startup adapters now execute all
five C initializers, but the next failures require coupled CRT data structures,
locale/character tables, stdio, arguments, and later exception/floating-point
behavior. Do not infer a playable port from successful initialization.

## Current verified boundary

- Executable SHA-256:
  `dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
- Unmodified recomp-kit at `eb32cc42623e0e9e868b9d27423a57d5a5cba56b`.
- Strict whole-image translation and ARM64 headless build pass.
- 28 new import handlers and one data import live in `recomp/native/startup.cpp`.
  They are new implementations with synthetic tests, not recovered CRT source.
- All five C initializers return zero. The C++ initializer table is entered; its
  completion is unverified. Argument, FILE and locale dependencies remain missing.
- Latest probe: `reports/recomp-globals-boot.json` / `.log`, exit 6 after the memory
  adapter rejects a range downstream of unresolved zero-returning CRT imports.
- Existing 39 tests, normal CMake build, startup adapter/asset/help CTest checks pass.
- Original installation unchanged. Executable copies, game-derived listings,
  generated code, binaries, profiles and reports remain ignored.

The bounded adapter details and limitations are in STARTUP_RUNTIME_SLICE.md.
Next planning work must not expand zero-returning stubs to make startup appear
successful. Require real ABI behavior and executable evidence.

## Concrete alternatives

| Route | Potential benefit | Unresolved work |
| --- | --- | --- |
| Continue native CRT adapters | Small incremental builds, explicit guest storage and controlled platform boundaries | Model x86 FILE/locale/ctype/arguments and cleanup consistently; eventually C++ exceptions, math and per-thread lifecycle; synthetic tests alone do not establish original CRT parity |
| Recompile a compatible 32-bit CRT DLL | Preserve its data layouts and implementations for a much broader API surface | DLL initialization/TLS, auxiliary-module memory, import/forwarder coverage, Win32/NT services, translator instruction/FP/SEH coverage and differential tests |
| Hybrid with one CRT as owner | Lift CRT data/locale/stdio and replace specific proven hot or unsupported functions | Define an ownership/ABI seam; avoid mixing heap allocations, FILE objects, locale/TLS state or encoded pointers between incompatible implementations |

My provisional recommendation is a **bounded auxiliary MSVCR90 DLL spike before
handwriting FILE/locale layouts**. This is a hypothesis for the planner to challenge,
not a selected architecture or proof that the DLL boots.

## Local candidate observed read-only

CrossOver has an i386 Wine `msvcr90.dll` at:

`/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/i386-windows/msvcr90.dll`

Read-only PE inspection found:

- SHA-256 `f58ddc0f08d75e2d750ad66d54be059cd90e428704b5b4351a4efc1550df13ba`;
- size 668,752 bytes, PE32 machine `0x14c`, image base `0x10000000`,
  image size `0xa9000`;
- 1,452 exports, and **all 204 names imported by Blur are present**;
- 131 kernel32 imports and 12 ntdll imports;
- three forwarded exports: `__threadhandle`, `__threadid`, `_getdrives` to kernel32.

The NT imports are `NtQueryInformationFile`, `NtQuerySystemTime`,
`RtlAreBitsClear`, `RtlAreBitsSet`, `RtlInitializeBitMap`, `RtlSetBits`,
`RtlTimeToSecondsSince1970`, `RtlUnwind`, and four Wine debug helpers. Their runtime
coverage has not been established for this candidate.

Reports: `reports/recomp-crt-candidates.json` and `reports/recomp-crt-coverage.json`.
No DLL copy, Ghidra import, translation or execution of this candidate has occurred.
Export-name coverage proves neither ABI fidelity nor successful initialization.
Wine/third-party notices and distribution constraints must be recorded if staging
or incorporating it; no runtime/game DLL belongs in tracked source.

The game's `_Redist/vcredist_x86.exe` is another potential source of the original
VC90 DLL. It was only located, not extracted, run or hashed in this checkpoint.
Compare possible native Windows and Wine DLL baselines explicitly; do not call
the CrossOver/Wine DLL Microsoft's original runtime.

## Kit precedent and integration concerns

The pinned kit's `samples/wine-msvcrt/README.md` records a separate synthetic sample
using a Wine i386 **msvcrt.dll**, with strlen/atoi/qsort/sprintf results. This is
upstream-reported evidence for the auxiliary-module mechanism, not a local test
of Blur or MSVCR90. It records remaining instruction/table gaps and incomplete
Win32/NT coverage. Do not blindly copy its permissive translation flags.

Our candidate DLL starts at `0x10000000`, exactly outside Blur's current default
256 MiB guest arena. A module experiment needs explicit arena sizing/mapping and
collision checks; existing image/stack/TEB/shim regions must remain valid.
Both local msvcrt and msvcr90 candidates prefer that base, so loading both requires
a deliberate relocation strategy rather than overlapping images.

The loader's mapped-module exports take precedence over import shims. Plan which
side owns allocation/data, and whether existing native adapters are removed,
retained only for kernel services, or used as verified overrides. Treat floating
point and exception support as explicit acceptance gates, not assumed benefits.

## Requested planning deliverable

Select and justify a CRT ownership strategy. Produce a small executable spike
plan covering pinned inputs, arena/module layout, safe staging, initializers/TLS,
kernel/NT import audit, unsupported instruction/SEH behavior, and tests for data
exports, allocation, callback registration, locale/FILE behavior and teardown.
Define stop criteria and the evidence required before attempting Blur startup with
the DLL. Keep the installed game read-only and all original/generated artifacts
ignored. Prefer restoring correct behavior over moving the next crash farther.
