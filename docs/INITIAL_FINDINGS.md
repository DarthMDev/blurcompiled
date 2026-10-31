# Initial findings — 2026-10-06

Source is the supplied installation, read without modification. Full local detail
is in `reports/install.json`; the report is generated rather than tracked.

## Executable baseline

- `Blur.exe`: 13,738,496 bytes, PE32 / x86 (`0x014c`).
- SHA-256: `dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
- Preferred image base: `0x00400000`; entry RVA: `0x00944515`.
- Entry VA at preferred base: `0x00d44515` (loader/CRT entry, not identified gameplay).
- Five sections: `.text`, `.rdata`, `.data`, `.tls`, `.rsrc`.
- `.text` raw size: 10,820,096 bytes. No game function reconstruction completed.
- Retail patch/version and provenance are not established by the PE identity.
  This installation includes Amax modifications; never assume it is a clean retail baseline.

## Inventory

151 visible regular files, 7,860,279,705 bytes (about 7.32 GiB). Seven `.pak`
archives; nineteen sets of `.level`, `.levelrender`, `.levelphysics`, and
`.levelstream`; thirty-one Bink movies. Six Lua source and six Lua bytecode files.
The inventory does not establish that the installation is complete or unmodified.

Observed `cache/boot.pak` and `cache/gamedata.pak` start with `2KAP`. Other headers
are captured in the report. Subsequent name/index findings and a bounded inspector
are documented in [PAK_FORMAT.md](PAK_FORMAT.md); general extraction remains pending.
Luigi Auriemma's [QuickBMS script index](https://mirror.aluigi.org/quickbms_list.php)
lists a Blur-specific PAK2/2KAP script; evaluate it against this PC baseline before
choosing a decoder. The `.pak` extension alone does not identify a format.

## Static imports and implications

| Observed import | Native work |
| --- | --- |
| `d3d9.dll`, `d3dx9_42.dll` | Recover draw/resource semantics, shader formats, then a Metal implementation |
| `DSOUND.dll` | Recover mixing, streaming, timing; platform audio adapter |
| `DINPUT8.dll`, `XINPUT1_3.dll` | SDL input, bindings, controller state and rumble |
| `WS2_32.dll`, `IPHLPAPI.DLL` | Socket and adapter behavior, then protocol fidelity |
| `binkw32.dll` | Native movie playback strategy; the bundled x86 DLL is unusable natively |
| Kernel/User/GDI/Advapi/Shell, MSVCR90 | Threads, synchronization, files, windowing, ABI/runtime replacement |
| dbghelp, PSAPI | Crash/debug/process functionality to isolate or replace |

The installer bundles PhysX, but that alone does not establish how gameplay uses
it. Check dynamic loads, strings, call sites and live behavior before deciding a
physics replacement. Absence from static imports is not proof of absence.

The local `d3d9.dll` is a separate x86 DLL. Its exact loader/hook behavior remains
to be analyzed. Treat it separately from the operating system's graphics API.

## Toolchain actually used

AppleClang 21, CMake, installed SDL3 (runtime version 3.4.14), Python 3.10, and
Xcode's llvm-objdump. `build/blur-native` is Mach-O ARM64. Ghidra was not found on
PATH at the initial baseline. Subsequent local setup is recorded in RE_WORKFLOW.md.
