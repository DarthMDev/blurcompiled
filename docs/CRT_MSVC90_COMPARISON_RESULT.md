# Microsoft VC90 translation and runtime checkpoints

## Static binding and startup slice passed, 2026-10-15

**The exact Microsoft CRT now works through load-time PE imports on Apple
Silicon.** In a fresh synthetic executable, actual CRT attach runs before EXE
entry; imported `malloc` and `_HUGE` resolve to the real DLL's code/data addresses.
The fixture then passes the existing dynamic allocation, binary FILE, C-locale
and missing-file errno checks. The identical PE passes under CrossOver using the
exact pinned DLL. This remains a synthetic CRT/loader result, not Blur integration,
complete Windows ABI parity, or a playable port.

The new implementation is isolated in `local/recomp-kit-static-binding`, based on
the same kit pin plus the preserved prior runtime/translator changes. The clean
kit, passing execution checkout, prior binaries and reports were not modified.
The incremental review patch is `recomp/patches/msvc90-static-binding.patch`;
`recomp/patches/msvc90-runtime-static-r1.patch` is the complete patch against the
original kit pin, including the prior translator/runtime changes.

The loader now maps and publishes all configured DLL export tables before binding
any IAT. It records guest DLL dependencies, binds named/ordinal code and data
imports to their actual guest addresses, then initializes dependencies before their
users and before main TLS callbacks/EXE entry. Successful loads acquire dependency
references; failures unwind their acquisitions without dropping another caller's
existing references. Dynamic release and normal process exit detach in reverse
initialization order. Static attachment receives an opaque nonnull **guest**
startup marker; failure/dynamic unload uses null; process termination uses nonnull.
These choices follow the documented
[DllMain contract](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain).
Cyclic configured guest DLL graphs and auxiliary static TLS are explicitly
rejected. Entry-less DLLs can load without a fabricated translated entry.

A new three-image synthetic chain independently verifies mapping and ordering.
The same EXE and two DLLs produce this sequence under CrossOver and translation:
`B attach → A attach → EXE start/complete → A detach → B detach`.
A imports B code and a shared B data export; the DLL configuration deliberately
lists A before B. An explicit LoadLibrary/FreeLibrary pair does not detach A
while its load-time reference remains. All four raw DLL-entry stack/nonvolatile
checks pass. The actual Microsoft CRT fixture similarly has exactly one attach
and one process detach, with both raw ABI checks passing and guest exit zero.
Both memory-only hosts exit one for absence of presented frames; that is checked
separately from guest exit zero.

The Microsoft process-termination path differs from the prior explicit-unload
path: its process detach returns with 10 CRT-owned heap allocations and two TLS
slots retained (97 arena blocks / 10,605 requested bytes total). The null-reserved
explicit unload had released CRT-owned allocations. No process-termination heap
cleanup assertion was substituted for the actual result, and no memory-lifetime
parity is inferred from these counters.

The focused synthetic CTest exercises shared-dependency references, repeated
static-attach idempotence, dynamic unload/reload, rollback after nested and later
EXE-root failures, preservation of preexisting owners, cycle/missing-dependency
rejection, code-by-name/data-by-ordinal binding, shared data storage, missing
exports, bounded import names/descriptors and DLLs without entries. The native
SEH and strict-execution CTests also pass. The prior expanded dynamic CRT probe
passes on the new loader as a regression check.

### Concrete next blocker: C++ compiler support input

The new synthetic Clang MSVC-x86 C++ probe compiles, but does not link. After
correcting `_CxxThrowException`'s stdcall import decoration, the sole unresolved
symbol is `??_7type_info@@6B@` (`type_info::vftable`). The exact DLL exports
`_CxxThrowException` and `__CxxFrameHandler3`, but does **not** export that vftable.
No matching compiler support library/object was found in the ignored local
inputs. No dummy vtable or alternate CRT was substituted, and this exception
probe has not executed. A compatible x86 C++ compiler support object/library, or
an independently verified compiler-support implementation, is the next input
needed for this reference gate. `local/recomp-crt-msvc90-eh-r1/blocker.json`
records PE symbol SHA-256/RVAs, source/object hashes, exact link failure and search
evidence. The source is `recomp/dll-probe/exception_probe.cpp`.

Evidence and reproduction checkpoints:

- `reports/recomp-static-dll-result-final.json`: gated consolidated result.
- `tools/recomp_static_dll_result.py`: rechecks exact generated CRT acceptance,
  current combined patch, symbol RVAs, matching observations, entry ABI and logs.
- `local/recomp-static-chain-r1/`: chain inputs, compile/export/strict translation,
  native build/run and CrossOver reference.
- `local/recomp-crt-msvc90-static-r2/`: exact-CRT static fixture and matching runs.
- `local/recomp-static-binding-r1/`: synthetic tests and dynamic CRT regression.

Local reproduction scripts and command arrays are preserved inside these ignored
experiment roots; fresh roots are required for reruns. The initial static-CRT
attempt's failed build and the initial C++ link failures are preserved separately.

Validation: 59 Python tests, both project CMake builds, default project CTests,
three opt-in startup/bootstrap/assets CTests, and native DLL-binding/SEH/strict
CTests passed. The reviewed whole-CRT generated sources are byte-identical to
the accepted callback generation; no listing edit, permissive flag or new recovery
guess was needed. Broader FP/NLS/FILE-error behavior, VC90 C++ exception execution,
thread notifications and full loader-lock/concurrency behavior remain open gates
before using this CRT for Blur startup.


## Independent reference unblocked, 2026-10-15

The exact Microsoft DLL now completes two independent native-x86 reference runs
under CrossOver 26.3 in the isolated scratch bottle. Both exit zero and complete
allocation/reallocation/preservation/free and unload. This is a CrossOver-hosted
Microsoft CRT reference, not independent Windows OS parity.

The scratch bottle's higher-version built-in SxS manifest was preserved and
renamed with a disabled suffix. The original package manifest and unchanged DLL
were staged under the matching `9.0.21022.8` SxS identity. The activation-context
trace then selects that exact identity, and the native DLL attaches and returns.
No DLL was patched or relabeled. Existing bottles and the installed game were not
modified. This resolves the name-based reference blocker; the earlier absolute
load's non-return remains unexplained and is not attributed to R6034.

Wine rebases the DLL to `0x7a6e0000`, so a separate synthetic reference variant
checks export RVAs relative to the module and checks allocation success without
requiring the translated arena's address range. The original native smoke fixture
and its evidence remain unchanged. All 15 observed exports in the expanded run
match the exact PE's export RVAs. Each symbol's SHA-256, RVA, log evidence and
confidence are in `reports/recomp-crt-msvc90-reference-result.json`.

Observed reference values: `_HUGE` words `0 / 0x7ff00000`, `_adjust_fdiv = 0`,
`_msize(32-byte allocation) = 32`, and `_msize(96-byte reallocation) = 96`.
The data values agree with the prior native smoke observations; full allocation
parity is not inferred from those examples.

The expanded reference additionally passes opaque-FILE binary write/seek/read
of `41 0a 00 ff 5a`, close/remove, missing-file `errno = 2`, initial and explicit
`C` locale, and preservation of `C` after an invalid locale request. Source is
`recomp/crt-probe/src/msvc90_reference_probe.c`; the exact built source and PE,
commands, output and hashes are preserved under
`local/recomp-crt-msvc90-oracle/reference-probe-r2/`. Comments and failure-label
wording were subsequently clarified in the source without changing checks.
The same compiled PE subsequently passed all these checks on the translated
Apple Silicon host with strict execution enabled. `_msize` values also match.
Attach/detach stack and nonvolatile checks pass; detach leaves zero CRT-owned
allocations and TLS slots, with the two process command-line cache blocks retained.
This is one expanded run, not full parity. Matching observations and hashes are
in `reports/recomp-crt-msvc90-parity-result.json`.

Static source inspection also identifies an open integration obligation:
`loader_load` binds the main IAT before mapping auxiliary DLLs, and `run_entry`
does not attach static dependencies. The existing auxiliary attach path is
explicit `LoadLibrary`. This is an observed implementation limitation, not a
verified static-DLL contract. Static binding, dependency ordering, reference
lifetime, attach-failure rollback and cycles need synthetic coverage before
returning to Blur's C++ startup. No Blur CRT replacement has been performed.

Evidence roots: `local/recomp-crt-msvc90-oracle/exact-sxs-r1/` and
`reference-probe-r{1,2}/`; fresh native parity work is isolated in
`local/recomp-crt-msvc90-parity-r1/`. VC90 exceptions and broader FP behavior
remain open gates.


Validation: 59 Python tests, both project CMake builds, default project CTests,
three opt-in startup/bootstrap/assets CTests, and native SEH/strict-runtime CTests
passed. The optional old CRC CTest could not start its preserved broken venv; a
direct alternate-venv attempt exited 132 and is not counted as passing. No CRC
code changed.

## Executable result, 2026-10-15

The subsequent continuous-work request authorized continuing through the native
probe and narrow runtime fixes. **The translated Microsoft CRT now executes the
synthetic allocation path on Apple Silicon in three fresh processes.** Actual
DLL process attach and detach return success; allocation, reallocation, size
queries, data preservation and free assertions pass. This is a dynamic CRT smoke
result, not Blur integration, complete CRT ABI parity or a playable port.

The exact Microsoft SHA-256 remains
`ed0170d3de86da33e02bfa1605eec8ff6010583481b1c530843867c1939d2185`.
All DLL addresses in this section belong only to that image. Evidence is the PE,
hash-pinned listings, strict generation reports and runtime logs; confidence is
high for observed bytes, targets and completed calls, with general behavior and
independent parity unverified. The original kit at the documented pin is still
clean, and the installed game and Blur adapter baseline were not modified.

| Executed milestone | Measured result |
| --- | --- |
| Static translation acceptance | Exact reviewed inventory accounts for six data exports, 33 recovery events and ten withdrawn data guesses; no permissive flags or edited listings |
| Native host | ARM64 host plus translated fixture and CRT compiled through kit build tools |
| First guarded entry | Stopped before missing InitializeCriticalSectionAndSpinCount could return a fabricated result |
| Native lock service | Implemented for the existing single-processor guest profile; valid-pointer initialization, recursive enter/leave, stack cleanup and boundary guards tested |
| Allocation path | 32-byte allocation, pattern write, growth to 96 bytes, preservation, size checks and DLL free completed |
| Detach recovery | A runtime-reached callback at RVA `0x6c76d` was recovered through `--discovered` in a new strict whole-module pass, without changing old listings |
| Final generation | 2,609 emitted functions, 3,291 dispatch entries; same reviewed data diagnostics; no failed/unmodelled bodies or table gaps |
| Raw DLL-entry ABI | Attach and detach ESP match the expected stdcall value before wrapper restoration; EBX/EBP/ESI/EDI preserved |
| Heap lifetime | OS heap handles track allocations, reject foreign frees, preserve realloc failure state, and release outstanding allocations on private-heap destruction |
| Environment lifetime | ANSI/UTF-16 blocks are distinct OS-owned allocations, freed through matching services; no successful no-op free on this path |
| Three fresh runs | Fixture complete, guest exit 0, no unknown/interpreted execution or called logging-only imports; CRT-owned allocations 0, heap handles 4, TLS slots 0 after detach |

Each run starts at 85 arena blocks / 6,964 requested bytes. Attach reaches 95 /
12,389, five handles, two TLS slots and eight allocations owned by the CRT's OS
heap. Detach finishes at 87 / 7,042, four handles, zero TLS slots and zero owned
allocations. The two retained blocks / 78 bytes match the native process command
line caches: 26 ANSI and 52 UTF-16 bytes. Three fresh-process runs have identical
counters. Same-process unload/reload growth has not yet been tested.

The headless host exits 1 because no frames were presented; its run report records
**guest exit code 0**, and the memory-only fixture completed. The result checker
requires this precise distinction rather than treating an arbitrary nonzero exit
as success. Export results for malloc/realloc/free/_msize and the three observed
data exports match their real DLL RVAs; these per-symbol identities, evidence and
confidence are retained in the consolidated report. `_HUGE` was observed as low
word zero / high word `0x7ff00000`, and `_adjust_fdiv` as zero; independent parity
for these values remains open.

The native runtime work is a new implementation. Critical-section spin count is
zero for the existing one-processor guest profile, consistent with the
[Microsoft contract](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initializecriticalsectionandspincount).
Private heap destruction follows the documented
[ownership/reclamation contract](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapdestroy),
and tested realloc failure preserves the original allocation as specified by
[HeapReAlloc](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heaprealloc).
Supported calls use growable heaps and observed flags; fixed-capacity heaps,
in-place-only realloc and allocation-exception behavior remain explicitly
unsupported. The environment contents remain the kit's fixed guest environment;
this does not implement full process environment, encoding or NLS parity.

### Current blocker: independent matching runtime baseline

A separate ignored CrossOver 26.3 bottle was created; no existing bottle or game
was used, and no redistributable installer was run. The matching Microsoft DLL
and the same synthetic executable were staged with an external EXE assembly
manifest and the preserved package assembly manifest.

The name-based LoadLibrary call fails. Wine's activation-context log redirects
`Microsoft.VC90.CRT 9.0.21022.8` to its `9.0.30729.6161` assembly and fails the
native-only DLL load with `c0000135`. A diagnostic fixture differing only in its
absolute DLL path and an added GetLastError observation maps the pinned file as
native at `0x7a700000`, but its initialization does not return. The isolated
bottle's wineserver was terminated; this was not a successful reference run.
The exact cause of that non-return is unestablished. It is not evidence that the
DLL is incompatible, nor validation of the translated runtime.

**This blocks the independent ABI/behavior gate.** FILE/locale, callback/error
paths, FP and VC90 C++ exceptions still require a working matching reference.
Static dependency binding/lifetime and broader Blur CRT replacement remain
unattempted. The useful next prerequisite is a correctly matched reference
activation context or an independent Windows setup, rather than replacing more
CRT behavior based on generated C alone.

### Reproduction and validation

The final combined, reviewable patch is
`recomp/patches/msvc90-runtime-r4.patch`, applied only in
`local/recomp-kit-msvc90-execution`. Earlier translator and runtime revisions,
failed runs and binary snapshots remain intact. The exact static inventory is
`recomp/crt-probe/msvc90-data-inventory.json`; its acceptance code refuses changed
bytes, references, recovery diagnostics, root gaps or withdrawn guesses.

- `tools/recomp_crt_acceptance.py`: exact inventory gate, with synthetic tests.
- `tools/recomp_crt_execute.py`: isolated fixture compile/export/translation and initial native build/run.
- `tools/recomp_crt_runtime.py`: versioned native runtime revisions.
- `tools/recomp_crt_callback.py`: runtime-evidenced callback acceptance and build.
- `tools/recomp_crt_lifetime.py`: final build/tests and three independently preserved runs.
- `tools/recomp_crt_smoke_result.py`: rechecks the immutable evidence and writes the consolidated result.

The actual command arrays, pins, source/patch hashes and outputs are in each
ignored manifest/report. Existing experiment roots deliberately refuse reuse.
The final run commands were:

```sh
local/recomp-msvc90-venv/bin/python tools/recomp_crt_lifetime.py build --revision 4
local/recomp-msvc90-venv/bin/python tools/recomp_crt_lifetime.py test --revision 4
local/recomp-msvc90-venv/bin/python tools/recomp_crt_lifetime.py run --revision 4 --repeat 0
local/recomp-msvc90-venv/bin/python tools/recomp_crt_lifetime.py run --revision 4 --repeat 1
local/recomp-msvc90-venv/bin/python tools/recomp_crt_lifetime.py run --revision 4 --repeat 2
local/recomp-msvc90-venv/bin/python tools/recomp_crt_smoke_result.py
```

Final validation: 59 project Python tests, normal/opt-in CMake builds, three
project CTests, native SEH CTest and the strict-runtime CTest with eight synthetic
subprocess cases passed. Native cases cover missing/unknown/interpreted calls,
normal stack cleanup, recursive critical sections, heap ownership/realloc failure/
reclamation, and environment block lifetime. Prior instruction/translator tests
remain historical evidence for the unchanged translator patch. No compatibility,
performance, rendering or gameplay claim follows from these checks.

The consolidated result is `reports/recomp-crt-msvc90-smoke-result.json`.
Runtime logs are under `local/recomp-crt-msvc90-execution/lifetime-r4/run{0,1,2}`;
reference setup/loader logs and input hashes are under
`local/recomp-crt-msvc90-oracle/`. Native code, binaries, generated C, listings,
databases, bottles and run evidence remain ignored.

---

The sections below are historical comparison/follow-up checkpoints.


## Follow-up executed, 2026-10-15

The user authorized one patch plus one corrective revision in this chat. That
supersedes the historical Astra stop below; no model handoff was needed.
The original kit, game inputs, listings and comparison evidence remain unchanged.
A separate checkout at the same pin is `local/recomp-kit-msvc90-noreturn`.

**Outcome:** strict code generation now completes, but the conservative acceptance
gate remains open on data/root classification. No DLL host was built or executed.

The initial patch identifies only literal PE IAT slots for Kernel32 ExitProcess
and ExitThread as terminal. Recovery, successor analysis and emission end those
paths; if an exit import unexpectedly returns, generated code explicitly fails.
Ordinary indirect calls, other DLLs with matching export names, TerminateProcess
and resumable RaiseException are not assigned this new terminal behavior.
Independent branches/alternate entries after an exit call remain reachable.

The first patched run removed all three fatal dispatch errors and exited zero
in 42.65 seconds, exposing the previously unavailable complete diagnostics. It
reported 747 recovery errors and seven missing export roots. The corrective
revision preserves 16-byte SIMD memory operands as `xmmword` during PE recovery,
using existing emitter semantics. A synthetic PE regression checks this path.
No listing, PE header, imported service or instruction semantics were replaced.

| Final strict measurement | Result |
| --- | --- |
| Exit / elapsed / timeout | 0 / 58.07 seconds / no |
| Emitted functions / parsed candidates | 2,608 / 2,618; ten speculative roots withdrawn |
| Dispatch entries | 3,290 |
| Failed bodies / unmodelled instructions | 0 / 0 |
| Jump tables / entries | 73 / 626; no missing targets or undecoded sites |
| Recovery diagnostics | 33 events at 11 distinct starts, down from 747 |
| Entry and remaining executable export coverage | Actual PE entry and 1,391 of the original 1,397 executable-section export roots present |
| Missing roots | Five vtable candidates and `_wctype` mask-table candidate in `.text` |

All addresses and labels in this follow-up belong only to Microsoft DLL SHA-256
`ed0170d3de86da33e02bfa1605eec8ff6010583481b1c530843867c1939d2185`.
Evidence: PE tables/bytes, generated dispatch, hash-pinned automatic listings and
strict statistics. Confidence is high for addresses, bytes and reference forms;
data classification is an evidence-backed candidate classification, not ABI or
runtime proof. Complete per-symbol evidence is in the ignored review report.

The missing export RVAs are `0x1e18`, `0x1e38`, `0x1e44`, `0x1e50`, `0x1e7c`
(vtable candidates, with object-field stores referencing the addresses), and
`0x32c8` (`_wctype`, with repeated 16-bit mask-like values). Executable section
permissions alone had overclassified these as functions. The real code export
`__libm_sse2_powf`, RVA `0x7f454`, now has dispatch coverage after the width fix.
Its earlier stop at RVA `0x7f498` decodes as MOVAPD with 16-byte memory input.

The 11 remaining recovery starts are IAT cells at RVAs `0x1000`, `0x1004`,
`0x1008`, `0x1018`, plus constant-data candidates at `0x8fa0`, `0x8fa4`,
`0x13cd0`, `0x13cf0`, `0x13d00`, `0x13d10`, `0x1dba8`. Typed scalar/SIMD
loads in hash-pinned listings reference the latter seven addresses. This explains
the diagnostic clusters; it does not prove arbitrary control flow cannot enter
them. Ten withdrawn data-pointer guesses are also preserved in the audit.
The wrapper correctly continues to report `translation_gate: false`; it was
not weakened to accept exit zero or silently discard these obligations.

The arena experiment ran three fresh processes at each candidate arena size,
using the same anonymous/private/read-write mmap flags as the pinned POSIX
platform implementation. All six reserved, touched, verified and unmapped their
arenas. Reserving 1.88 GiB produced no peak-RSS increase; touching about 16.7 MiB
raised peak RSS by about 16.5 MiB, similar to the 257 MiB case. This supports
retaining the Microsoft preferred base on this Apple Silicon machine for a small
probe, rather than implementing relocations now. It does not establish actual
loader initialization, full-arena commit capacity, concurrency or mobile fit.
The measurement uses peak RSS, not current RSS or committed-memory accounting.

Validation: 213 focused translator/import/recovery/SEH/table regressions and 74
synthetic instruction differential cases passed on the final revision; 56 project
Python tests, normal and opt-in CMake builds, and three CTests passed. Unicorn
crashed inside memory mapping in the sandbox; the explicitly approved unrestricted
run passed. No original game was used by these tests. The first width regression
caught an incorrect conversion-table edit before the second DLL attempt; the
final fix and test both pass. The two DLL attempts exhaust this work budget.

Reviewable patches: `recomp/patches/msvc90-exit-iat.patch` (round 0) and
`recomp/patches/msvc90-exit-iat-simd-width.patch` (final combined patch). Both
apply to the original pin; the clean original checkout was not modified.
`tools/recomp_crt_followup.py` verifies the patch, candidate, listings and ancestry,
allows only rounds 0/1, preserves outputs, and caps each run at 120 seconds.
`tools/recomp_arena_probe.py` performs the isolated synthetic memory measurement.
Exact commands, source/patch hashes, statistics and generated-source hashes are
in `reports/recomp-crt-msvc90-noreturn-round{0,1}.json`; arena results are in
`reports/recomp-crt-msvc90-arena.json`. The read-only audit and its reproduction
script are `reports/recomp-crt-msvc90-noreturn-review.json` and
`local/recomp-crt-msvc90-noreturn/review_remaining.py`.

**Continue toward recompilation:** keep Microsoft VC90 as the working candidate.
First incorporate the evidence-backed data/export inventory and review the ten
withdrawn guesses, retaining failure for unexplained code roots. Then run the
existing synthetic fixture with real PE attach, export resolution, allocation,
reallocation, free and detach, failing at unresolved services or skipped entry.
Only add narrowly required OS behavior within the existing runtime-fix budget;
never convert missing services to successful zero-return stubs. Keep the current
Blur adapters intact until one DLL demonstrably owns CRT state.

After that fixture passes, implement/test static dependency binding and lifetime
ordering with synthetic DLLs. Then return to the existing five-C-initializer Blur
checkpoint and measure C++ initializer progress. FILE/locale, FP and C++ exception
parity remain gates before broad startup use. No new whole-game translation,
graphics work or gameplay claim follows from emitted DLL C.

---

The remainder records the unchanged 2026-10-14 comparison.


2026-10-14. The bounded comparison is complete. **Neither candidate passes the
unchanged kit's strict translation gate.** No DLL host was built, no DLL or
installer was executed, and no translator/loader patch or listing repair was
performed. This result does not establish CRT compatibility or gameplay.

Recommend Astra plan a narrow Microsoft indirect-import no-return/dispatch
follow-up, together with a high-base arena feasibility review, before choosing an
executable probe. Its matching assembly identity and different measured blockers
make it a credible next investigation; they do not yet justify selecting it for
execution. Keep CrossOver's bounded FISTTP/UD2 contract conditional and preserve
the coherent native CRT fallback if loader/recovery/FP/EH work becomes broad.

## Measured inputs and comparison

Kit: clean `eb32cc42623e0e9e868b9d27423a57d5a5cba56b`.
Blur SHA-256:
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
Redistributable SHA-256:
`c6a7e484f4d84883bc1205bccea3114c0521025712922298ede9b2a1cd632357`.

| Observation | Microsoft | CrossOver (preserved prior attempt) |
| --- | --- | --- |
| DLL SHA-256 | `ed0170d3de86da33e02bfa1605eec8ff6010583481b1c530843867c1939d2185` | `f58ddc0f08d75e2d750ad66d54be059cd90e428704b5b4351a4efc1550df13ba` |
| PE | i386; 655,872 bytes | i386; 668,752 bytes |
| Preferred map, half-open | `[0x78520000,0x785c3000)` | `[0x10000000,0x100a9000)` |
| Actual PE entry RVA | `0x22d40` | `0xd140` |
| Exports / Blur name coverage | 1,450 / 204 of 204 | 1,452 / 204 of 204 |
| Blur export classifications | No differences versus CrossOver | PE section classification; ABI unverified |
| Static TLS / delay imports | Both absent | Both absent |
| Base relocations | 6,010 HIGHLOW, 64 ABSOLUTE; directory `0x9f000`, size `0x3304` | 10,112 HIGHLOW, 46 ABSOLUTE |
| Dependencies | 161 Kernel32 names | 131 Kernel32 + 12 NTDLL names |
| Registration inventory | 125 handler candidates, 9 signature-only, 27 absent from literal scan | Kernel32: 86 / 7 / 38; NTDLL: 12 absent |
| Forwarders | None | Three Kernel32 forwarders; none statically imported by Blur |
| Strict attempt | Exit 1, 46.02 seconds; no timeout | Exit 1, 5.27 seconds; no timeout |
| Fatal diagnostics | Three unresolved direct dispatch targets | Two FISTTP + four UD2 failed bodies, eight downstream direct-call gaps |
| Complete upstream statistics | Not emitted after fatal invariant | Not emitted after fatal invariant |

The Microsoft payload's assembly manifest and file/product version independently
identify `Microsoft.VC90.CRT`, x86, `9.0.21022.8`, token
`1fc8b3b9a1e18e3b`. Blur's embedded manifest requests that same identity. No
external manifest was found in the installation directory listing. The package
policy redirects `9.0.20718.0–9.0.21022.8` to `9.0.21022.8`. Matching package/EXE
identity is observed; the modified executable's runtime compatibility is unresolved.

Exactly one CRT payload was extracted/selected: the canonical SxS member
`msvcr90.dll.21022.08.Microsoft_VC90_CRT_x86.RTM`. The `ul_` and `nosxs_`
alternatives were listed but not extracted or translated. Its actual byte SHA1
matches the manifest's XML dsig identity digest
`29268ef0cd220ad3c5e9812befd3f5759b27a266`. The manifest's legacy file `hash`
attribute differs; it was recorded, not treated as a whole-file hash assertion.
Only the CRT DLL, its assembly manifest/policy/catalog were extracted from the
inner cabinet. Installer resources/EULAs remain in the ignored outer extraction.
No certificate trust or redistribution license determination is claimed.

System bsdtar listed and extracted the outer package; it rejected the signed inner
CAB header. Installed p7zip 17.05 listed the inner CAB, reporting a 9,192-byte tail,
and extracted the selected members without changing the archive. Member paths
and attributes were checked before extraction; absolute/traversal paths and links
were refused. Source and staged redistributable hashes match at the checkpoint.
The installed game was only read.

## Strict failure evidence and limits

One Ghidra 12.1.4 analysis/export completed in 40.8 seconds under the external
900-second cap, producing 2,239 hash-pinned automatic assembly listings. The
actual entry has a listing. The comparison config explicitly seeded the entry
and all executable exports (1,397 unique roots), retaining all listings. The old
CrossOver config seeded its actual entry; its export used all automatic listings.
Neither was a root-limited or permissive translation.

The following symbols/addresses belong **only** to the Microsoft SHA-256 above.
Evidence: strict log, listing SHA-256, PE IAT/export tables, independent Capstone
byte decode. Confidence is high for bytes, addresses and export labels; automatic
function boundaries and runtime reachability remain unverified.

| Function RVA / observed export | Call-site RVA / bytes | IAT name / target RVA |
| --- | --- | --- |
| `0x221b5` / no export label | `0x221c6` / `ff 15 14 10 52 78` | ExitProcess / `0x221cc` |
| `0x23251` / `_endthread` | `0x2327c` / `ff 15 4c 10 52 78` | ExitThread / `0x23282` |
| `0x233ef` / `_endthreadex` | `0x2340c` / `ff 15 4c 10 52 78` | ExitThread / `0x23412` |

All three rejected targets immediately follow indirect IAT calls and contain
`cc` (INT3) bytes. The listings end at those calls. The pinned translator knows
ExitProcess/ExitThread in `NORETURN_IMPORTS`, but the indirect-call emission and
final-fallthrough handling still produce these rejected continuation dispatches.
A narrow no-return boundary defect is a **hypothesis**, not a repaired result.
Do not label the bytes harmless, skip bodies, fabricate entry targets or infer
that all remaining recovery errors will disappear after a fix.

The captured Microsoft log contains no unsupported-instruction failed-body
diagnostics. Its automatic listings contain zero FISTTP/UD2 and two each
FNSTENV/FLDENV. This inventory is not an exhaustive disassembly proof. The fatal
invariant occurred before `table.c` and upstream statistics, so entry/export
dispatch coverage, table gaps, undecoded sites, recovery-error counts and full
instruction acceptance remain unestablished. Neither error-count comparison nor
absence of failed-body messages can pass the gate.

## Loader, dependency and initialization burden

The Microsoft preferred base requires minimum arena size `0x785c3000`
(2,019,307,520 bytes, about 1.88 GiB), versus CrossOver's proposed 257 MiB.
The checked guest map is nonoverlapping and uses 32-bit half-open addresses.
It reserves the fixture, heap, stack, sentinel, PEB/TEB/TLS area and shim ranges;
it is a translation configuration, not an approved memory allocation.

`runtime/memory.cpp:mem_init` calls `os_vm_reserve(GUEST_SIZE)`; the POSIX path
uses anonymous private RW mmap without an eager full-arena memset. Actual commit,
address-space pressure and resource limits were not measured. The existing heap
ends at `0x0e000000`, so the larger arena does not automatically enlarge it.
The kit has no general auxiliary relocation pass. The DLL was not rebased and
its preferred base/header were not edited. High-base allocation versus a properly
implemented relocation path requires a separate decision and tests.

Microsoft has a load-configuration security-cookie address (RVA `0x9a2a8`)
and six SafeSEH entries at table RVA `0x21a60`; metadata does not prove cookie
initialization or exception handling. No static TLS callbacks are present, but
Tls* services, DLL process/thread notifications and cleanup remain obligations.
Actual entry attach, static dependency/IAT ordering, failure propagation,
reference lifetime and single-owner CRT state remain later gates for both images.

The dependency audit records registered argument counts, source hashes/lines,
handler presence and review flags, against `imports_init`, Kernel32 and wide
registration paths. These are static observations, not a live registry or ABI
verification. No imported behavior was validated for either DLL. Concrete source
limitations include shared-heap allocation independent of heap handle, HeapDestroy
only removing the handle, TLS error-state semantics, critical-section deletion,
and unsupported SEH continuation. Kernel32 RtlUnwind registration does not
establish VC90 C++ unwinding parity.

Microsoft statically imports GetProcAddress and LoadLibraryA/W. Raw string
observations include ADVAPI32, USER32, MSCoree and PGORT90 names; they are possible
dynamic obligations, not measured load requests. No additional system DLLs were
extracted. Runtime resolution and FP/EH behavior remain untested. Eliminating
Wine imports reduces one observed dependency category, but total service fidelity
cannot be ranked by import count alone.

## Reproduction and preserved evidence

New tooling is `tools/recomp_crt_compare.py`, additive to the unchanged previous
wrapper. It accepts a hash/metadata-pinned candidate descriptor and ignored work
root, freezes ancestry/source/input hashes, refuses existing outputs, and has
only prepare/export/translate actions. Timeouts kill and reap a dedicated child
process group, preserving partial logs. Five synthetic tests cover candidate
identity, checked maps, immutable inputs, timeout/log preservation and the strict
gate's refusal of incomplete statistics or dispatch coverage.

The unavailable old `local/recomp-venv` was preserved. A separate Python 3.11
venv was created at `local/recomp-msvc90-venv`, using the pinned kit's recorded
`tools/requirements.txt` plus `pytest==8.4.2 ninja==1.13.0`. The exact package
freeze is under `local/recomp-crt-msvc90-preflight/environment-freeze.txt`.
Initial sandbox DNS failure required approved network access for these downloads;
the kit was not changed. No substitute kit/tool source was downloaded.

```sh
python3 -m venv local/recomp-msvc90-venv
local/recomp-msvc90-venv/bin/python -m pip install \
  --cache-dir local/pip-cache -r local/recomp-kit/tools/requirements.txt \
  pytest==8.4.2 ninja==1.13.0
local/recomp-msvc90-venv/bin/python tools/recomp_crt_compare.py prepare \
  --work local/recomp-crt-msvc90-translation \
  --candidate local/recomp-crt-msvc90-preflight/candidate.json \
  --ancestry local/recomp-crt-msvc90-preflight/baseline.json \
  --out reports/recomp-crt-msvc90-prepare.json
local/recomp-msvc90-venv/bin/python tools/recomp_crt_compare.py export \
  --work local/recomp-crt-msvc90-translation \
  --out reports/recomp-crt-msvc90-export.json
local/recomp-msvc90-venv/bin/python tools/recomp_crt_compare.py translate \
  --work local/recomp-crt-msvc90-translation \
  --export-report reports/recomp-crt-msvc90-export.json \
  --out reports/recomp-crt-msvc90-strict.json
```

These are the recorded commands; existing paths now deliberately refuse reuse.
For a future authorized reproduction choose fresh work/report paths, preserve the
ancestry/descriptor, and verify all source/input hashes first. Outer and inner
extraction commands, members, tool versions and hashes are in
`local/recomp-crt-msvc90-preflight/{package,payload}-extraction.json`.

The consolidated ignored `reports/recomp-crt-msvc90-comparison.json` fingerprints
prepare/export/strict reports, logs, detailed dispatch bytes, dependency audit,
initialization/dynamic obligations, source inputs and manifests. Its ancestry
fingerprints every prior CrossOver artifact before work. Baseline Blur config,
adapters, executable, reports and the clean kit were reverified unchanged.

Validation: 56 Python tests, normal and opt-in CMake builds, and the three
startup-adapter/bootstrap-help/native-assets CTests passed. These validate tooling
and existing fallback targets; they do not establish DLL compatibility.

**Stop here for Astra.** No remaining comparison attempt is authorized by this
result. Choosing translator work, loader changes, DLL execution or a broader
native CRT slice is the next planning decision.
