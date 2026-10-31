# Static trap inventory and batch regeneration

**Continuation:** [missing-listing recovery and audio](HANDOFF_RECOVERY_AUDIO_2026-10-19.md)
records the next batch, the containing-owner/suffix correction and newer probes.

`tools/recomp_trap_batch.py` replaces repeated startup probes for **known emitted
instruction traps** with static inventory and bounded owner generation. It does
not discover every indirect-call target, implement new instructions, establish
whole-function parity, or prove menu/gameplay reach.

The pinned baseline contains 1,614 distinct trap addresses across 605 owners:
1,339 CVTPS2PD, 97 CVTPD2PS, 42 CMPNLESS, and 136 other instructions. Counts are
unique instruction addresses; overlapping owners may reference the same bytes.
All refer to Blur.exe SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.

The first inventory uses the unchanged-input manifest from
`probe-next-004e02c0`. It classifies 25 owners as covered by loaded replacement
modules, 440 as candidates for existing tested lowerings, and 140 for review.
The corrected unique-instruction summary is in
`local/trap-batch-20261019/inventory-v2/inventory.json`. The earlier inventory
used by the first build is preserved; its instruction histogram counts owner
occurrences, but its unique-site count and owner classifications agree.

The first build compiled 402 of the 440 candidate owners, covering 1,192 distinct
baseline trap sites and 1,196 dispatch entries. With 54 sites covered by earlier
active modules, 368 baseline sites remain outside this batch. Hash/source and
dispatch validation found zero conflicts, including the six reserved observer
entries. Results are in `local/trap-batch-20261019/{build/batch,load/load}.json`.
The load manifest records VA/RVA, executable hash, evidence and confidence for
every entry. This is structural acceptance, followed by the bounded integration
result below; full behavioral parity remains unverified.

The 38 build review items comprise 17 extent-limit failures, 11 tables needing
explicit validation, seven table-boundary failures, two open/indirect branches,
and one compilation failure. The latter is a recursive CALL_FN in owner
`00865310` (RVA `00465310`): the bounded generator's minimal header lacks that
macro. No instruction or generated-source patch was applied; it is not loaded.

## Combined integration result

Fresh strict `local/startup-native-r2/compare-step/probe-trap-batch-20261019/`
loaded the 402 modules alongside the pre-existing verified load list. All hashed
inputs were unchanged. The replacement for the previous trap's owner `00761ea0`
was loaded. The run reached BinkDoFrame and approximately frame 45,300 before aborting on
CVTPS2PD at VA `0073ddea`, RVA `0033ddea`. It took 252.12 seconds, exited by
signal 6, and reported no missing dispatch targets or unimplemented imports.
No visible menu, movie decoding, or racing is established.

That next trap was already inventoried as a review item: baseline recovered
entry `0073dde7` / RVA `0033dde7`, seven instructions through `0073ddfb` in
`chunk_069.c`, has no standalone corrected Ghidra listing. No functions.tsv
owner was found for that range. Evidence is the baseline generated header,
independent PE instruction decode in the inventory, and the new strict log.
Confidence is high for static byte identity and observed trap reach; entry
boundaries and behavior need independent recovery. The batch did not invent a
listing or entry to clear it. This is the next recovery gate, rather than a
reason to return to one-by-one discovery of the already-inventoried sites.

Validation: 84 root Python tests, root CMake build, and all four CTests passed.
Logs are under `reports/trap-batch-*`. Native host/translator semantics were
unchanged; only the inventory/batch tooling, synthetic tests, docs and ignored
generated modules were added.

## Workflow and gates

1. Inventory original generated ownership headers and alternate-entry wrappers;
   independently decode each trap in the hash-pinned PE. Record VAs, RVAs,
   instruction bytes, owner/listing hashes and evidence. Active module binaries
   must match an unchanged-input probe before they count as coverage. Early
   module manifests without extents use their hash-verified module descriptors.
2. Generate candidates only when all their uncovered trap instructions belong
   to the explicitly tested set: CVTPS2PD, CVTPD2PS, CMPNLESS. Shared trap bytes
   in overlapping owners and unusual owner starts go to review.
3. Preserve complete baseline owners and every declared alternate entry. Use
   the existing strict emitter with the 8 KiB maximum; refuse open CFGs, SEH,
   jump tables needing an audit, retained traps and unresolved branches. Compile
   successful modules through kit `tools/lazy_static.py`. Failed attempts and
   logs are retained; they are never added to the load list.
4. Validate compiled and generated-source hashes, then reject both sides of
   duplicate batch dispatch entries and any collision with active modules or
   explicitly reserved observer entries. Save a separate load manifest with
   executable identity, VA/RVA, evidence and confidence for every entry.
5. Run one fresh strict integration probe with only the load-manifest modules.
   Preserve its hashes, logs, bounds and outcome. Static discovery is not
   observed reach; no module is described as behaviorally verified merely
   because it compiled.

Use the existing local Python environment containing Capstone and pefile.
Each output must be fresh and inside the ignored `local/` directory. Example
commands from the repository root (substitute fresh absolute output paths):

```sh
local/recomp-msvc90-venv/bin/python tools/recomp_trap_batch.py inventory \
  --active-probe /absolute/repository/local/path/to/result.json \
  --out /absolute/repository/local/fresh-inventory
local/recomp-msvc90-venv/bin/python tools/recomp_trap_batch.py build \
  --inventory /absolute/repository/local/fresh-inventory/inventory.json \
  --out /absolute/repository/local/fresh-batch
local/recomp-msvc90-venv/bin/python tools/recomp_trap_batch.py validate \
  --batch /absolute/repository/local/fresh-batch/batch.json \
  --reserved-entry 0x00add770 \
  --out /absolute/repository/local/fresh-load-list
```

The example reserved address is this host's stream observer. Repeat
`--reserved-entry` for the other known observer hooks. The input probe already
protects active translated-module entries. The validate step reads and hashes
every input again; it does not modify a host, module or the reference game.

Synthetic unit coverage includes owner boundaries, duplicate trap expressions,
alternate ownership, orphan/out-of-extent traps, retained-trap coverage and
dispatch conflicts. The runtime remains strict: later unknown targets and
unsupported instructions still abort and provide the next evidence slice.
