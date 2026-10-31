# New startup adapters — 2026-10-08

This slice adds new Windows/VC90 compatibility behavior through recomp-kit's
`[translate].native` extension. It changes no kit files or original executable
instructions. The kit remains pinned to
`eb32cc42623e0e9e868b9d27423a57d5a5cba56b`. Blur's input is still SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
These are new adapters based on documented API contracts, not recovered CRT source
or proof of full VC90 compatibility.

`recomp/native/startup.cpp` registers 16 missing imports before the PE loader
patches its IAT. The whole-archive link retains its registration constructor.
The empty overrides header replaces no game function. Guest pointers remain
32-bit arena addresses; native pointers are temporary accesses to validated
arena storage. No platform APIs are introduced into recovered logic.

Implemented bounded behavior:

- `InterlockedCompareExchange`: aligned 32-bit atomic comparison/exchange, old
  value result, sequentially consistent host ordering and kit write tracking.
  [Microsoft contract](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-interlockedcompareexchange).
- `OpenMutexA`: correct stdcall argument count and forwarding to the kit's
  existing named-mutex service. Access-right/inheritance fidelity remains that
  service's responsibility and is not newly validated here.
- `_initterm` and `_initterm_e`: guest callback execution over the bounded
  half-open table, null skipping, ordered execution, and first-error propagation
  for `_e`. [Microsoft contract](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/initterm-initterm-e).
- `memcpy`, `memmove`, `memset`: bounded guest memory access, correct destination
  return value and byte semantics; writes retain dirty spans/watchpoints.
- `malloc`, `calloc`, `_calloc_crt`, `realloc`, `free`, `_aligned_malloc`,
  `_aligned_free`, `_msize`, `_errno`: guest heap services, overflow/exhaustion
  reporting, zero initialization, valid alignment, preserved reallocation data,
  and guest-TEB-specific errno storage.
  [Aligned allocation contract](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-malloc),
  [aligned free contract](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-free).

Limitations: invalid guest ranges, invalid frees, overlapping `memcpy`, invalid
alignment/zero-size aligned allocation and malformed/oversized initializer tables
stop with an explicit diagnostic. Original invalid-parameter handler and exception
dispatch are not implemented. Alignment/free metadata is provided by the replacement
guest allocator, not laid out like the original VC90 heap. Memory allocation does
not by itself supply CRT locale, C++ exceptions, encoded pointers or imported data.
Remaining missing imports retain the kit's diagnostic fallback and must not be
treated as implemented behavior.

## Executable evidence

The original startup at preferred VA `0xd44255` (RVA `0x944255`, same hash above)
pushes the initializer table arguments before calls through wrappers at preferred
VAs `0xd44f74` and `0xd44f6e` (RVAs `0x944f74` and `0x944f6e`). The wrappers
dispatch through the `_initterm_e` and `_initterm` IAT entries. Confidence: observed
static arguments/import identities; these labels name API wrappers, not original
source symbols.

The observed tables are:

| Table | Start RVA | End RVA (exclusive) | Slots | Non-null slots |
| --- | --- | --- | --- | --- |
| `_initterm_e` | `0xa62b34` | `0xa62b4c` | 6 | 5 |
| `_initterm` | `0xa53734` | `0xa62b30` | 15,615 | 4,985 |

Evidence: exported startup instructions and ignored
`reports/recomp-initializers.json`, produced from the pinned PE bytes. These counts
are observed pointers, not verified function meanings or complete code coverage.
The earlier kit's zero-return stubs skipped this work entirely.

## Tests and build

`tests/startup_adapter_tests.cpp` supplies synthetic arena contents, callback
results and doubles for the kit's heap/mutex services. It validates the new
adapter's ABI metadata, compare-exchange match/mismatch, initializer ordering/null
skipping/error stopping, empty ranges, memory operations, dirty tracking,
allocation overflow/alignment/free/reallocation, and separate errno slots. It
does not revalidate the underlying kit allocator/scheduler/mutex implementation
or compare these adapters against a live Windows VC90 DLL.

```sh
cmake -S . -B build-recomp -DBLUR_RECOMP_STARTUP_TESTS=ON
cmake --build build-recomp -j 4
ctest --test-dir build-recomp -R 'startup-adapter|native-assets|bootstrap-help' --output-on-failure
python3 -m unittest discover -s tests -v
cmake --build build -j 4
local/recomp-venv/bin/python tools/recomp_experiment.py build-headless --out reports/recomp-headless-build.json
local/recomp-venv/bin/python tools/recomp_experiment.py boot-probe --out reports/recomp-startup-boot.json
```

The build wrapper preserves unchanged generated-code timestamps during adapter
edits and records hashes of the adapter sources/configuration. The probe refuses
to run when those hashes differ from the last successful build. It still uses a
local executable copy and isolated profile with no assets, mods or video.

## Observed follow-up startup

The full ARM64 headless host compiled and linked successfully with the unchanged
kit and this adapter. The first build needed a complete generated-code rebuild
because adding the native extension changes its compile definition; one 20 MiB
generated file was particularly expensive. A subsequent build through the wrapper
reported `ninja: no work to do`, confirming preserved generated-code timestamps.

The follow-up probe (`reports/recomp-startup-boot.json` and `.log`) did not reach
the previous null-call/SEH abort. It entered the actual `_initterm_e` table,
called four non-null entries and propagated error **`0x18`** from the fourth.
The first callback was preferred VA `0xd44434` (RVA `0x944434`). The fourth
table entry is preferred VA `0xd43ed9` (RVA `0x943ed9`). All apply only to the
executable hash recorded above. Confidence: observed table/traversal output and
static return-path instructions; not independently validated VC90 startup.

Static inspection of the fourth callback shows a call at `0xd43ede`, followed by
NEG/SBB and an AND with `0x18`, mapping failure to this error. Its helper at
`0xd44031` (RVA `0x944031`) calls `0xd43f8c` and converts a null result into -1.
Together with the missing imports below, this points to the on-exit registration
path; that diagnosis is an inference, not a recovered source symbol.

Newly reached missing imports were `__set_app_type`, `_encode_pointer`,
`__p__fmode`, `__p__commode`, `_controlfp_s`, `_decode_pointer`, `_lock`,
`__dllonexit`, and `_unlock`, all from MSVCR90. The kit's remaining zero-return
fallbacks were explicitly logged. No claim of correct global CRT initialization
is made: those missing data/control operations still invalidate it.

The guest returned from its entry; the host exited with code 1, no timeout, and
**zero presented frames**. It did not set a display mode or render a scene. The
copied executable's hash remained unchanged. A mod teardown/run-record warning
was also logged; successful teardown is not established. The registry audit now
finds 148 registered handlers among the 445 static imports, with 297 logging-only
entries. Registration is not verified game-wide compatibility.

Validation: all 39 existing Python tests passed, the normal CMake build passed,
and CTest passed the startup adapter, native asset and bootstrap-help checks.
Synthetic tests validate the new adapter contract; the live probe identifies
remaining startup dependencies. Next bounded work is CRT mode/global storage,
process-scoped pointer encoding, floating-point control and on-exit registration,
before attempting graphics initialization.

## Follow-up: all C initializers return success

A second bounded adapter extension supplies writable guest `_fmode`/`_commode`
storage, application type and math-error callback storage, reversible opaque
pointer encoding, CRT numbered locks through the kit's scheduler-aware critical
sections, on-exit callback registration/dispatch, and the observed precision-only
`_controlfp_s` request. It also registers `_adjust_fdiv` as a zero-initialized data
import: the modeled guest CPU does not have the obsolete Pentium FDIV hardware
bug. There are now 28 function handlers plus this data import.

These are **new implementation choices**, not recovered Windows CRT internals:

- Pointer encoding uses a nonzero process-specific cookie and XOR. It preserves
  opaque encode/decode round trips, including null and all-ones sentinels; it does
  not claim to reproduce the Windows cipher or cross-process encoded values.
- CRT locks use the kit's existing guest critical-section services, not a host
  mutex that could block while retaining the guest scheduler baton.
- `__dllonexit` stores callbacks in a bounded caller-supplied guest array and
  leaves pointers/data intact on allocation failure. `_onexit` manages a runtime
  array; `_cexit` pops and calls entries LIFO. This is callback dispatch only:
  full CRT stream closing, pre-terminator execution and process-exit fidelity are
  not implemented. Math-error callback storage does not implement math routines.
- Floating-point control handles queries and precision-only masks. The verified
  startup request selects 53-bit x87 precision and reports its abstract CRT control
  word. Other masks stop explicitly. SSE control state, exception/rounding changes,
  host FP environment changes and game-wide FP equivalence are not claimed.

Contract sources: [pointer encoding](https://devblogs.microsoft.com/oldnewthing/20201113-00/?p=104447),
[`__dllonexit`](https://learn.microsoft.com/en-us/cpp/c-runtime-library/dllonexit),
[`__p__fmode`](https://learn.microsoft.com/en-us/cpp/c-runtime-library/p-fmode),
[`_controlfp_s`](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/controlfp-s).

Extended synthetic tests pass for mode storage identity/mutation, full-width
pointer round trips, scheduler lock routing, callback table reallocation/failure,
LIFO execution without replay, and precision change/query. The 39 existing tests,
normal CMake build and three selected CTest checks also pass. The full translated
host rebuilt incrementally: only adapter compilation, archive and final link.

`reports/recomp-globals-boot.json` and `.log` now show **all five C initializers
returning zero**, followed by entry into the C++ initializer table at VA
`0xe53734` (RVA `0xa53734`), first callback VA `0xd4420a` (RVA `0x94420a`).
Hash/provenance are the same as above; confidence is observed runtime dispatch,
not complete C/C++ runtime correctness. Missing calls include `__getmainargs`,
`__iob_func`, `memcpy_s`, `setlocale`, `___lc_handle_func`,
`___lc_codepage_func`, and `__pctype_func`. A new memory adapter refuses an invalid
guest range after those unresolved calls return zero. The host reports an abort
at guest EIP `0xd43f86` (RVA `0x943f86`), exit code 6, no timeout and an unchanged
copied executable. No complete frame report or playable result is established.

Locale/character tables, x86 FILE layout and CRT argument setup introduce a larger
ABI/lifecycle decision. The user-requested Astra High planning checkpoint has been
flagged; [CRT_ARCHITECTURE_HANDOFF.md](CRT_ARCHITECTURE_HANDOFF.md) records the
concrete alternatives and local DLL candidate evidence before further expansion.
