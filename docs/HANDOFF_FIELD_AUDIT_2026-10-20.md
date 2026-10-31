# Field initialization observer batch — October 20

Read `AGENTS.md` and `HANDOFF_TABLE_BATCH_R2_2026-10-20.md` first.
No menu or racing established. Installed game/reference assets remain untouched.

Pinned executable SHA-256:
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
Unchanged diagnostic host:
`d65e2847e3c15468bb5aec92824ba2a22a2472a98059aca890a63d86671718e4`.
Original host remains untouched:
`597b115f468a5cf18522c8d02e82f15deed82e7e8f55865337e0763da0987e18`.

## Static audit

`local/field-audit-20261020/ledger.json` records executable/host hashes, VA/RVA,
bytes, generated source hashes, dispatch provenance, evidence and confidence for
eight baseline symbols. None is supplied by the preceding four repaired modules.
The pinned virtual slot at VA `00e66a7c` / RVA `00a66a7c`, offset 4 from
`00e66a78`, contains `00426c80`. `00428ff0` installs that vtable; it calls
`00b212e0`, which registers through `00b211c0`, before derived initialization.
These are observed instruction identities, not assigned class meanings.

A concrete field initializer was identified: VA `0042926b` / RVA `0002926b`
in `00429150` stores EDX to [ESI+0x1c], after loading EDX from [[ESI+8]+0x88].
The derived constructor does not initialize that field. Constructor registration
preceding later initialization is an ordering hypothesis requiring observation,
not authorization to move registration or change scheduler policy.

`00419660` saves/restores ESI and directly does not write EBX/EDI/EBP; child
calls remain relevant to complete ABI claims. Its RET 4 means expected ESP
advance on return is 8, not zero. Reader `00426c80` RET 12 means advance 16.

## Read-only observer library

Eight hooks are batched in `observer/watch.dylib`: `00426c80`, `00419660`,
`00428ff0`, `00429110`, `00429150`, `00b212e0`, `00b211c0`, `00b20f80`.
They chain previous dispatch, preserve default base dispatch, use bounded
snapshots and track constructor/initializer/destructor phases by actual runtime
object identity. They do not reuse a previous run's heap address or mutate guest
registers, memory, stack, allocation, return behavior, or scheduling policy.

The host's existing write-watchpoint globals are armed/restored around selected
constructor, initializer and reader scopes. This changes host diagnostic
metadata only. It does not establish coverage of bulk-copy writes or of periods
outside those scopes. No inline seam was invented at the faulting store.
No allocator interposition was added. Runtime vtable snapshots at reader entry
and the actual caller return address identify the virtual call edge.

The `valid` log field means only that the prospective store fits the guest
arena and avoids its null page; it does not establish allocation/lifetime validity.
Logs for `00b20f80` refer to a collection-like receiver, not the reader's object;
do not interpret its offsets as the same layout. Phase history is observational,
not proof that no unobserved initialization/free path exists. `00429150` also
has an existing suffix entry `004293e9`, after the candidate field store.

Built through existing `tools/lazy_static.py` without rebuilding the main host
or any prior module. Root validation: 96 Python tests passed, one desktop test
skipped; CMake build and four CTests passed. Logs:
`reports/field-audit-{unit,build,ctest}.log`. Earlier native DX cursor failures
remain a separate unresolved limitation; no green native-suite claim is made.

## Dynamic findings so far

One object at runtime VA `08749850` was observed through constructor entry,
base registration, constructor exit and three reader calls. Its vtable becomes
`00e66a78`, whose slot 4 matches `00426c80`. The field +0x1c is already zero
at constructor entry and remains zero at all observed reader entries. Constructor
completed; no primary initializer or destructor call was observed. The reader
calls return with expected nonvolatile registers/ESP. Field +0x48 is zero, so
`00419660` is not called on these observed paths. No field change or ABI anomaly
was reported. The earlier nonzero invalid field value was not reproduced.

This narrows the observed case to a callback using an object whose pointer field
has not been established at the observed seams. It does not prove which missing
initialization, publication, or synchronization path is responsible, nor rule out
all unobserved writes/free/reuse. No speculative field initialization, pointer
clamp, call skip, registration move, or scheduler patch was applied. The original
reader still executes its store for field value zero; existing strict execution
is not proof that all low-address memory writes fault.

Clock/tick counters cease advancing around boot time 133.7s while scheduler
checkpoint counts continue increasing. Tick entries/exits are balanced, guard is
clear, pinning is off. This supports absent clock/message host service rather
than a stuck tick guard or pinned clock in the observed stalled interval.
The watchdog deadline has a larger outer-timeout margin in this run.

## Evidence and continuation

`local/field-audit-20261020/` contains `static-audit.py`, `ledger.json`, pinned
baseline excerpts, `deepseek-response.md`, `observer/generated/`, observer build
log/load manifest, `run.py`, `console.log`, and `probe/`. All 504 prior modules
are preserved plus the observer library (505 dylibs total). Original diagnostic
sources and host are unchanged. Fresh evidence paths are required for reruns.
The run finished in 396.89 seconds with host exit 4, no external timeout, no
signal or unsupported-instruction trap. The host watchdog reported at boot time
390s and produced its normal report; all result input hashes stayed unchanged.
The outer deadline margin therefore worked in this diagnostic run. The 360s
cooperative close did not occur: clock/message host service had ceased while
scheduler checkpoints still advanced. The canned watchdog reason says the guest
stopped calling into the runtime; that wording does not negate observed runtime
scheduler activity. The legacy report's DirectDraw/no-D3D counters also do not
negate the separate D3D9 raster-attempt counters.

The run reported 31,246 uniformly black frames. That establishes neither menu,
racing nor successful rendering. Static byte/entry/slot identities and observed
phase order have high confidence; the missing initialization's root cause remains
unknown. Original nonzero invalid pointer `fdcdedbd` was not reproduced.

Latest full input list is `local/field-audit-20261020/probe/result.json` (505
libraries, including the new chained observer). Preserve all of it for the next
probe. `observer-events.txt` and `checkpoint.json` summarize the exact observations.
A concise next manual DeepSeek prompt is `next-deepseek-prompt.md`: audit constructor
caller `00418133`, initializer caller `004189fe`, and registration/iteration/worker
ordering before a speculative synchronization or field-state patch.

## Requested cleanup

The user explicitly requested cleanup after this diagnosis. `plan-cleanup.py`
planned pruning only generated frame_*.ppm captures in ten older probe folders
with adjacent host.log. It retains temporal samples and first/last captures of
every distinct file size. Original assets, recovered modules, executable inputs,
logs, JSON results, source, tests, and decompiler databases are excluded.
`apply-cleanup.py` requires the current probe's result, protects its entire input
list, checks file size/mtime before unlinking, and re-verifies all input hashes
after deletion. The plan is approximately 50.3 GiB / 23041 redundant captures.
Execution and final sizes will be recorded in cleanup-report.json.

Cleanup completed: 23,041 old generated captures removed, 50.302 GiB reclaimed;
52 representative captures retained with hashes. No changed files were skipped.
All current probe input hashes were verified again after deletion and match.
Local artifacts decreased to approximately 10.1 GiB; volume free space increased
to approximately 77.4 GiB. The manifest lists every removed file and retained
capture. Logs/results may still mention historical captures intentionally pruned
by this requested cleanup; do not infer those files still exist.

Final validation after the observer/cleanup scripts: 96 Python tests passed,
one desktop test skipped; CMake build and all four CTests passed. Logs are
`reports/field-audit-final-{unit,build,ctest}.log`. No additional strict probe or
main-host rebuild was needed for cleanup.
