# Boot archive completion and movie-path checkpoint — October 19

Read `AGENTS.md` and the earlier startup/CVTPS2PD/comparison handoffs first.
This checkpoint supersedes the exact failure in
`HANDOFF_STARTUP_HASH_LOCK_2026-10-18.md`. It records observed behavior, one
bounded recovered entry, and one small runtime slice. No whole-image
regeneration, generated-instruction edit or failure bypass occurred, and the
reference installation stayed read-only.

## Pinned inputs

- Blur executable SHA-256
  `dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`, base
  `00400000`.
- MSVCR90 SHA-256
  `ed0170d3de86da33e02bfa1605eec8ff6010583481b1c530843867c1939d2185`.
- Previously verified host preserved at
  `local/startup-native-r2/bin/pop_headless-f287e9bc`, SHA-256
  `f287e9bcc2fe19b2cfddead09e4683196fec0e5e2f842f60324009efa80a1eb1`.
  It was not relinked for the earlier runtime/header changes; the preserved
  copy is the diagnostic baseline, not the current host.

## New observed entry — `00580040` / RVA `00180040`

The 170-second diagnostic `probe-load-complete-1` completed the entire
`boot.pak` stream (556 full 128 KiB reads plus a final 77,824-byte read) and
then aborted on:

```
crt.guard.unknown_call target=00580040 EIP=005656ea ESP=029e32ac
```

The target is a genuine 13-byte function entry after `int3` padding, not an
instruction chosen from the middle of an owner:

| VA / RVA | Bytes | Instruction |
| --- | --- | --- |
| `00580040` / `00180040` | `8b442404` | `MOV EAX,[ESP+4]` |
| `00580044` / `00180044` | `894120` | `MOV [ECX+0x20],EAX` |
| `00580047` / `00180047` | `83c020` | `ADD EAX,0x20` |
| `0058004a` / `0018004a` | `c20400` | `RET 4` |

Reached call: `FUN_00564350` performs `MOV EDX,[EDX+0x50]` and an indirect
`CALL EDX` at VA `005656f5` on a pool-block object returned by `FUN_00535b00`.
The vtable slot at `+0x50` contains `00580040`. The observer's EIP `005656ea`
is the instruction following the preceding allocator call at `005656e5`; the
call itself is the indirect `CALL EDX` at `005656f5`. Confidence is high for
the byte/entry identity and observed reach; the surrounding pool/free-list
behavior is not claimed.

Evidence and recovery:

- `local/startup-native-r2/compare-step/observed-00580040/`: disposable Ghidra
  redisassembly, independent PE Capstone decode, and a byte-boundary check whose
  four instructions agree exactly. The bounded body is `[00580040, 0058004c]`.
- `local/startup-native-r2/compare-step/module-00580040/`: generated module with
  no alternate entries, no tables, no tail targets, no SEH, and no retained
  runtime traps. `translation.json` reports `instructions: 4`,
  `unmodelled: []`, `unresolved_branches: []`.
- Module SHA-256
  `6b4c80b9cd054508bf08fa3bd4d87ef7c58e0766f9db068a3d616b73288710ec`.
  Input manifest: `module-00580040/inputs.json`.
- The module was generated with the existing `recover_observed.py` path and
  kit `tools/lazy_static.py`; no baseline listing or generated C was edited.

## Observed startup completion and movie reach

The fresh 26,000-frame diagnostic `probe-load-complete-2` used the same strict
mode, read-only asset root and bounded modules as the earlier runs, plus
`module-00580040`:

| Event | Log line | Frame |
| --- | --- | --- |
| last `boot.pak` read (`offset=72876032`, returns 77,824 bytes) | 38739 | 14,350 |
| first `BinkWait` (`caller=0068459c`, handle `02a1fd50`) | 39210 | 14,425 |
| `BinkRegisterFrameBuffers`, `BinkDoFrame`, `BinkShouldSkip` | 39211–39213 | 14,425 |
| strict abort `InterlockedExchangeAdd` | 40281 | 14,875 |

This is the first observed entry into the integrated movie playback routine
`FUN_00684570` in this host. The Bink shim was built without FFmpeg, so its
record is the documented "finished record" placeholder; `BinkDoFrame` returns
success without decoding, and no movie frame or menu frame is claimed. The
movie callers and the strict abort are the evidence; the placeholder's visual
result is not playback evidence.

`Tex::AddDirtyRect` and `IDirect3DDevice9::UpdateTexture` were reached in the
same path and currently log "not implemented" before returning success. They
are separate integration gates, not proof that a frame was uploaded.

## Runtime slice — interlocked services

The reached import was not implemented:

```
crt.guard.unimplemented_import KERNEL32.dll!InterlockedExchangeAdd \
  argc=254 EIP=00554376 ESP=03047840
```

`argc=254` is the unknown-argument sentinel. The pinned image imports
`InterlockedIncrement`, `InterlockedDecrement`, `InterlockedExchange`,
`InterlockedExchangeAdd` and `InterlockedCompareExchange`. The first three were
already implemented; the last two are now registered with argument counts 2
and 3:

- `InterlockedExchangeAdd(pointer, value)` returns the old 32-bit contents and
  stores `old + value`.
- `InterlockedCompareExchange(pointer, exchange, comparand)` returns the old
  contents and stores `exchange` only when the old value equals `comparand`.

The runtime is cooperative: no guest yield is possible between the read and
write inside either shim. These are new OS-service implementations, not
recovered game code and not a general atomic/memory-model claim.

Synthetic validation: `runtime/tests/crt_strict_tests.cpp` gained an
`interlocked` subprocess case covering return-old/add, `0xffffffff` wraparound,
`0x7fffffff` overflow to `0x80000000`, and equal/mismatched
CompareExchange writes and returns. `crt_strict_tests` now reports
`11 subprocess cases passed` (it was ten). The binaries build through the kit
`tools/build.py` path.

## Diagnostic atomic override and the module-loading defect

The clean headless relink is still pending, so the preserved host was first
advanced with a small load-time override module, in the same style as the
existing crypto slice:

- `local/startup-native-r2/compare-step/atomic-module/install.cpp` registers the
  two new interlocked shims with `imports_register` in a constructor and links
  with `-Wl,-undefined,dynamic_lookup`; it does not modify the host binary.
- Built module SHA-256
  `93cebb401ae3ed816e720929d832f39da8c6f720fc5d1d279a82a21a271a60c6`.
- With the preserved host and this module, the previous `InterlockedExchangeAdd`
  guard no longer fires; the run reached the movie path and then a new SSE trap.
  This is diagnostic evidence that the shim gap is removed, not acceptance of
  the unrelinked host.

While preparing that run, a second defect was found in the run helpers:
`Path.glob("module-????????/code.dylib")` silently returned **zero** paths on
this volume after the drive was remounted, while the literal path
`module-0049ef20/code.dylib` and `glob("module-0049ef20/*")` still worked. The
directory-only pattern `module-????????` and `probe-*/result.json` also worked.
The consequence was that fourteen bounded slices (`0049ef20`, `0049f000`,
`004d3780`, `00580040`, `00587cf0`, `005deca0`, `00819f60`, `00847000`,
`00af4070`, `00b2a830`, `00bb4f70`, `00bb5210`, `00bb5320`, `00bd6840`) were
omitted from a smoke run, which then trapped at the CVTPS2PD in `00587cf0`.
`run_probe.py` and `run_direct.py` now enumerate `compare-step` with
`os.listdir` plus `re.fullmatch(r"module-[0-9a-f]{8}")` and construct literal
paths. A 2,400-frame smoke run with the fixed enumeration was clean and logged
45 `observed.replace` lines, including `00587cf0`. This is a diagnostic
loading fix, not a game or runtime change.

## Post-load entry chain

After the atomic gap was removed, the next aborts formed a short post-load chain.
Each is a bounded module under `local/startup-native-r2/compare-step/`; all
report `unmodelled: []`, `unresolved_branches: []`, no tables and no SEH. None
of them replaces a whole-image regeneration, and none claims whole-function
behavioral parity.

| Reached / RVA | Trigger | Owner / extent | Instructions | Module SHA-256 |
| --- | --- | --- | --- | --- |
| `005416e8` / `001416e8` | `CVTPS2PD XMM0,XMM1` | `FUN_005416a0`, 213 bytes | 44 | `eed1f2624f88a5cff9e637192ede42b41099b180da7c9beae470b967fd1f2bd1` |
| `00578f10` / `00178f10` | pool vtable `+0xc` unknown call | gap entry, `00578f10..00578ffe` | 64 | `9451b710fb455da9d1cf3742cc08ca1d209c6adfed8f6d160cd823bd8b22c8e1` |
| `005800b0` / `001800b0` | same pool loop, another block vtable | gap entry, `005800b0..005801ec` | 86 | `8cd97b25cda6ff53c0c9142eaea72ea7b724d256d3d6c4b900ff5b6141918ccf` |
| `0056087b` / `0016087b` | `CVTPS2PD XMM0,XMM0` | `FUN_005607d0`, 202 bytes | 64 | `1bb08b3214afefa61461c8281029b942c3ad90312dda3328f6a36b1770a3b701` |
| `00561880` / `00161880` | pool vtable `+0x30` unknown call | gap entry, `00561880..0056197e` | 91 | `04a71c4008034a94384fdc083ec150d8c0f55354b9747c25bbace207436e9839` |
| `00581475` / `00181475` | `CVTPS2PD XMM0,XMM1` | `FUN_00581400`, 331 bytes, alternate `00581440` | 106 | `f33a716d10c720382c17b19bb45e0763f5d6e1e9987260a0bc8fce6a95a6c535` |

The gap entries are called indirectly after the allocator `FUN_00535b00` returns
a pool block. `00578f10` and `005800b0` come from the loop in `FUN_005659b0` at
VA `00565a98` (`MOV EDX,[EAX]`, `MOV EAX,[EDX+0xc]`, `CALL EAX`).
`00561880` comes from `FUN_00560750` at VA `0056077f` (`MOV EDX,[EAX]`,
`MOV EDX,[EDX+0x30]`, `CALL EDX`). The targets are padded function entries
outside the baseline `functions.tsv` extent; each was recovered with a
disposable Ghidra export and an independent PE Capstone boundary check. The two
`CVTPS2PD` sites are inside listed owners and use the already-tested lowering,
so their modules are generated from the baseline listing.

The reach order is observed, not assumed: `probe-allmods-2` stopped at
`00578f10`, `probe-allmods-3` at `005800b0`, `probe-allmods-4` at `0056087b`,
and `probe-allmods-5` at `00561880`. The movie path (`BinkDoFrame`) was reached
in each of those runs before the abort, so these entries are on the post-movie
startup path. The caller return `00572644` for `0056087b` is in a
scene/transform helper range, not in the Bink shim itself. This is a series of
pool-block virtual methods; each block type has its own slot implementation, so
the chain is expected to be finite but is advanced one observed entry at a
time.

## Host rebuild and next gate

The earlier host was compiled before the latest `x86.h` helper edits and does
not contain the new shims. A headless relink therefore requires those dependency
edges to be rebuilt; the old binary is preserved as noted above. The relink was
started but stopped when the external drive dropped, and no post-relink probe
has been run. The next exact actions are:

1. Complete the headless rebuild through kit `tools/build.py` (not a direct
   compiler invocation) and record the new host SHA-256.
2. Re-run the same long diagnostic with a fresh output name, without the
   temporary atomic module, to validate the real runtime integration.
3. If the movie path now spins because the no-FFmpeg record has zero
   count/current fields, audit the observed Bink record layout (`+0x08` count,
   `+0x0c` current, `+0x10` count, `+0x14` current) and make the placeholder a
   consistent finished record with synthetic tests. Do not change the guest
   movie state machine.
4. If the boot layer transitions, look for the first front-end/Lua/menu
   activity and the first new file or message after `boot.pak`.

Rendering caveat: the CPU rasterizer deliberately skips shader-model-3 draws
("shader model 3 runs on the GPU renderer only"), and Blur reports `vs_3_0` /
`ps_3_0`. The headless host has no GPU backend, so even a correct menu state
will present black frames. A *visible* menu needs the kit's Metal/GPU renderer
path (for example `pop_smoke`, which draws into an offscreen Metal texture
without a window) or a CPU SM3 implementation. That is a separate gate from
the startup state machine.

## Reproduction

Use fresh output names. The long runs are bounded diagnostics outside the
60 s/3600-frame probe-tool cap; they preserve input hashes, a read-only asset
root, strict execution and a fresh directory. `run_direct.py` now writes no
frame files (`RECOMP_FRAME_EVERY=0`) and loads the bounded modules by explicit
enumeration.

```sh
python3 local/startup-native-r2/compare-step/run_direct.py NEW_NAME 220 26000
```

The diagnostic atomic-override variant adds two arguments:

```sh
python3 local/startup-native-r2/compare-step/run_direct.py NEW_NAME 220 26000 \
  local/startup-native-r2/bin/pop_headless-f287e9bc \
  local/startup-native-r2/compare-step/atomic-build/atomic_startup.dylib
```

The module list is built by the helper: the three `cvtps2pd-final-*` modules,
`module-4e0620`, every literal `module-<8 hex>` directory under `compare-step`
(which now includes `module-00580040`, `module-00587cf0` and
`module-005416a0`), the explicit `-r2` modules, the crypto and import-watch
observers, and the stream observer.

## Artifacts

- `local/startup-native-r2/compare-step/probe-load-complete-1/`: 170 s /
  20,000-frame run that completed the full `boot.pak` stream and aborted on the
  unknown call to `00580040`; this is the evidence that motivated the module.
- `local/startup-native-r2/compare-step/probe-vtable-2/`: clean 20,000 frames
  after adding `module-00580040`; `boot.pak` was still streaming (395 reads)
  when the frame cap was reached, and there was no unknown call or fault.
- `local/startup-native-r2/compare-step/probe-load-complete-2/`: full
  `boot.pak` stream, movie reach, and the new strict abort on
  `InterlockedExchangeAdd`; preserved even though the run ended by abort.
- `local/startup-native-r2/compare-step/module-00580040/` and
  `observed-00580040/`.
- `local/startup-native-r2/compare-step/crt_strict_tests` build logs and the
  runtime test source in the kit working tree.

No goal, automation, subagent, commit or PR was created.
