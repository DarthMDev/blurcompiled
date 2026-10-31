# recomp-kit experiment — 2026-10-07

This is a static recompilation feasibility experiment alongside the native asset
reader. It does not establish a playable Blur port. The original installation is
never written; the executable used by the experiment is a hash-verified copy in
ignored `local/recomp-blur/original/`. Generated C, builds, databases, profiles,
and logs remain ignored.

## Pinned inputs and ownership

- [recomp-kit](https://github.com/veritr1x/recomp-kit), commit
  `eb32cc42623e0e9e868b9d27423a57d5a5cba56b`, recorded in `recomp/kit-pin.json`.
  Its MIT `LICENSE` and dependency `NOTICE` remain in the local checkout. No kit
  source was copied into tracked source or modified. Project licensing remains
  undecided; generated game code is private local output.
- Blur.exe SHA-256
  `dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`,
  image base `0x400000`, PE/CRT entry RVA `0x944515`. This is the supplied
  installation baseline, whose retail/Amax provenance is not established.
- Local Ghidra 12.1.4 and JDK 21 from `RE_WORKFLOW.md`. The kit recommends a
  different Ghidra release; the assembly export used here ran successfully with
  our existing database.

`recomp/game.toml` has no recovered game-state globals. Required legacy frame
hooks are disabled sentinels, the animation rewrite list is empty, and the kit's
Populous-specific built-in mods are disabled. The heap starts at `0x02000000`,
above this image's end (`0x0125b000`). These are experimental host configuration
choices, not recovered Blur behavior. Guest pointers index the kit arena and are
never native pointers.

## Verified small slice

Proposed label: **CRC table builder**; preferred VA `0xcb6460`, RVA `0x8b6460`,
for the executable hash above. Evidence: exact instructions in
`reports/ghidra-payload-dispatch.txt`, calls from archive checksum/path routines,
256 stores, reflected polynomial `0xedb88320`. Confidence: validated translated
behavior for this bounded integer routine; the label is inferred, not an original
source symbol.

`tools/recomp_slice.py` passes its assembly listing through the pinned kit's
translator. An opt-in CMake target compiles the resulting C to an ARM64 shared
library using the kit's unmodified `x86.h` helpers. `tools/recomp_slice_host.c` is
a new minimal verification host; it aborts if unexpected runtime services are
invoked. It is independent of the kit's full host build.

All 256 output words match both an independent bitwise CRC table calculation
and the original executable instructions run in Unicorn. Sixteen synthetic
invocations vary initial registers, direction flag, and output alignment.
Verification compares eight registers, returned EIP, defined arithmetic flags,
16-byte output guards and a 4 KiB stack region. Table SHA-256:
`12f3e0576d447eb37b36d82ba0c1c5481b8f0d12fdc70347ce4a076b229d4c86`.
The report is `reports/recomp-crc-parity.json`. Unicorn's ARM cache query crashes
inside the execution sandbox; the comparison passed outside it. This is not a
comparison with a live Windows game process and says nothing about floating point,
threading, rendering or gameplay.

## Whole-image translation

The assembly-only exporter found 28,922 automatic function listings initially.
The first strict translation failed on five switches and unresolved dispatches.
The exporter had omitted unowned switch bodies; this was not evidence of an
unsupported CPU instruction.

`RepairRecompFlow.java` seeds code blocks reached by the following observed
bounded tables, refreshes automatic bodies, and gives otherwise unowned blocks
candidate entries. All addresses below share the executable hash above. Evidence
is the instruction/table bytes and `reports/recomp-repair-script.log`. Confidence
is **static candidate block boundaries**, not validated game-function meanings.

| Indirect jump RVA | Table RVA | Bound evidence |
| --- | --- | --- |
| `0x8cbb4` | `0x8cd2c` | EAX compared with 3; JA excludes other values |
| `0x305391` | `0x30578c` | EAX compared with 3; JA excludes other values |
| `0x6eba87` | `0x6ebbac` | EAX compared with 6; JA excludes other values |
| `0x71556c` | `0x715a20` | EAX compared with 7; JA excludes other values |
| `0x865264` | `0x8654a0` | 52-entry byte selector at RVA `0x8654c8`, bounded by CMP/JA |

Direct branch evidence also seeds RVA `0x6ea35c` (jump at `0x6ea356`) and
`0x715a04` (conditional branches in the `0x7154b0` region). The candidate at
RVA `0x8cb40` was found by the kit's discovery pass; its decoded body and bounded
switch were reviewed. No original function names are claimed for these blocks.
The repaired export has 28,950 entries, including separate switch blocks.

The second strict translation passed in about 272 seconds:

- 48,157 emitted bodies from 48,252 parsed/discovered candidates;
- 68,191 dispatch symbols; 3,130 detected jump-table sites, all decoded;
- zero reported translation failures, unresolved direct dispatches or table gaps;
- 95 guessed blocks withdrawn, 1,127 recovery errors, and 14 discovery rounds.

The candidate/symbol counts are not a decompilation percentage or proof of complete
coverage. The recovery errors and withdrawn candidates require runtime evidence;
unknown paths may still be absent. No permissive table-gap or unmodelled-instruction
options were passed. Reports: `reports/recomp-strict.json` and
`reports/recomp-strict-upstream.json`; generated C: `local/recomp-blur/full-gen/`.

## Host and API coverage

The kit's ARM64 headless stub host compiled with the Blur configuration, pinned
SDL3/glslang sources, video disabled and built-in game mods disabled. Its diagnostic
load used an isolated executable copy and profile. It mapped the PE but did not
execute Blur's entry: its stub dispatch returned immediately. Zero frames were
presented. This run supplies registry evidence only, not original-game startup.

Blur's static import table contains **445 imports across 16 DLLs**. Matching those
against the loaded host's own import report found **132 registered handlers and
313 logging-only entries**. A registered handler is not evidence that all of its
behavior is correct for Blur. `tools/recomp_experiment.py audit` also performs a
conservative literal source-table scan; its macro limitations are explicit.

Observed gaps in this pinned host:

- All 204 MSVCR90 imports are logging-only, including CRT startup, memory/math,
  exceptions, locale and file operations. Imported data such as `_acmdln`,
  `_HUGE` and `__sys_nerr` must be treated as data, not callable stubs.
- All 16 D3DX9_42 imports are logging-only. These include shader compilation,
  reflection, matrix helpers, and texture/cube/volume creation. Existing D3DX9_26
  handlers cover only a subset; DLL renaming alone would not supply the behavior.
- All 32 Winsock imports are logging-only. This gives no Amax functionality.
- Ten of 17 Bink imports are logging-only; video is disabled for this host spike.
- The imported XInput and DirectSound **ordinals** are logging-only despite the
  kit's named adapters. Ordinal mappings need separate verified ABI handling.

Full details are in ignored `reports/recomp-imports.json` and
`reports/recomp-boot.log`. Static imports omit delay/runtime-loaded modules,
including any unestablished PhysX or local hook DLL behavior.

## Translated startup result

The full translated headless host also **compiled and linked successfully**:
`recomp/build/recomp/pop_headless` is Mach-O ARM64. Its build used the kit's
`tools/build.py`, four jobs, unchanged pinned kit source, and video OFF.
The linker warned that a common-data section alignment was reduced from 32 KiB
to 16 KiB; this warning has not been investigated. Build report:
`reports/recomp-headless-build.json`.

Unlike the stub check, `boot-probe` executed the translated PE entry and CRT
startup. The isolated run exited with code 6 and no timeout. It observed these
unimplemented calls, in this order:

1. `KERNEL32!InterlockedCompareExchange`;
2. `MSVCR90!_initterm_e` and `_initterm`;
3. `KERNEL32!OpenMutexA`;
4. `MSVCR90!memcpy` and `_aligned_malloc`;
5. a call to guest address zero, then `MSVCR90!_except_handler4_common`;
6. unsupported `ExceptionContinueExecution`, followed by a runtime abort.

The upstream default import handler returned zero and warned about unknown
stdcall argument counts. This is diagnostic fallback, not implemented behavior.
In particular the initializer calls did not run the CRT initializer tables, and
memory/allocation calls did not supply their semantics. The abort log places
guest EIP at preferred VA `0x42d69e` (RVA `0x2d69e` for the same pinned hash);
no game-function meaning is assigned to that address.

The copied executable's hash remained unchanged. No completed frame report was
available because the abort path deliberately avoids signal-unsafe reporting;
no menu or rendered-game result was established. The run had the executable only,
with no assets, plugin modifications or video. Evidence:
`reports/recomp-translated-boot.json` and `reports/recomp-translated-boot.log`.

The next bounded slice is **verified startup runtime support**, especially the
atomic primitive and VC90 initializers, allocation, imported data and exception
ABI. Do not add success-returning stubs to advance the game. Resolve the real
behavior, rerun startup and check new failures before tackling D3DX/shader support.
The existing native asset decoder remains an independent comparison tool.

Final checks: 39 Python tests passed, the normal CMake build passed, and all three
opt-in CTest checks passed (bootstrap help, native assets, and CRC parity).

## Reproduction

From the repository root, provision the ignored kit and venv once:

```sh
git clone https://github.com/veritr1x/recomp-kit.git local/recomp-kit
git -C local/recomp-kit checkout eb32cc42623e0e9e868b9d27423a57d5a5cba56b
python3 -m venv local/recomp-venv
local/recomp-venv/bin/python -m pip install -r local/recomp-kit/tools/requirements.txt pytest==8.4.2 ninja==1.13.0
local/recomp-venv/bin/python tools/recomp_experiment.py prepare --exe '/path/to/Blur/Blur.exe'
```

Export and repair listings using the existing hash-pinned Ghidra project:

```sh
JAVA_HOME='/path/to/user/github/blur-native/local/tools/jdk-21.0.12.1+1/Contents/Home' \
local/tools/ghidra_12.1.4_PUBLIC/support/analyzeHeadless \
  local/ghidra blur-dba0264b -process Blur.exe -noanalysis \
  -scriptPath tools/ghidra -postScript RepairRecompFlow.java \
  -postScript ExportRecompListings.java local/recomp-blur/repaired \
  -log reports/recomp-repair.log -scriptlog reports/recomp-repair-script.log
local/recomp-venv/bin/python tools/recomp_experiment.py audit --out reports/recomp-imports.json
local/recomp-venv/bin/python tools/recomp_experiment.py translate --out reports/recomp-strict.json
```

Build/verify the independent slice:

```sh
cmake -S . -B build-recomp -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_PREFIX_PATH=/opt/homebrew -DBLUR_RECOMP_SLICE=ON
cmake --build build-recomp -j 4
ctest --test-dir build-recomp --output-on-failure
python3 -m unittest discover -s tests -v
cmake --build build -j 4
```

The optional slice requires the private executable/listing input; default builds
do not require recomp-kit. No original instruction bytes or asset fixtures are
stored in tracked source.

Before building the full translated headless host, provision the kit's stub tree
through its documented CMake cache workflow. From the repository root:

```sh
cmake -S local/recomp-kit --preset macos-stub \
  -B /path/to/user/github/blur-native/recomp/build/cmake/macos-stub \
  -DRECOMP_GAME_DIR=/path/to/user/github/blur-native/recomp \
  -DPOP_BUILD_ROOT=/path/to/user/github/blur-native/recomp/build \
  -DPython3_EXECUTABLE=/path/to/user/github/blur-native/local/recomp-venv/bin/python \
  -DRECOMP_VIDEO=OFF
local/recomp-venv/bin/python local/recomp-kit/tools/build.py \
  --game-dir /path/to/user/github/blur-native/recomp --stub --target headless --jobs 4
```

After successful strict translation:

```sh
local/recomp-venv/bin/python tools/recomp_experiment.py build-headless --out reports/recomp-headless-build.json
local/recomp-venv/bin/python tools/recomp_experiment.py boot-probe --out reports/recomp-translated-boot.json
```

This wrapper publishes generated files under the kit's build lock and builds
through its `tools/build.py`, reusing the pinned dependencies fetched for the
stub. It does not patch or silently accept failed translation. The runtime profile
and executable used for any subsequent boot probe must stay in ignored local
directories; never run against the original installation as a writable game root.
