# Xenia Blur graphics leads — October 30, 2026

Research findings, not a verified fix for this native build.

## Upstream reports

[Xenia Blur issue #589](https://github.com/xenia-project/game-compatibility/issues/589) covers title 41560845. In an [August 4, 2020 comment](https://github.com/xenia-project/game-compatibility/issues/589#issuecomment-668542208), Triang3l attributes excessive brightness to average-luminance GPU-to-CPU readback. A [July 12, 2023 report](https://github.com/xenia-project/game-compatibility/issues/589#issuecomment-1632597558) describes races working with `d3d12_readback_resolve = true`. Older reports note a substantial performance cost. These are upstream reports; our observed dark/red output is a different symptom, so shared cause remains a hypothesis.

The [Canary Blur patch file](https://github.com/xenia-canary/game-patches/blob/main/patches/41560845%20-%20Blur.patch.toml) contains two disabled-by-default workarounds: `HDR fix` bypasses luminance behavior, while `Disable HDR` removes HDR and changes dependent effects. The file targets its recorded executable hash; matching title/media alone does not establish patch applicability to our pinned XEX. No guest patches were applied.

## Local evidence and next test

The preserved SDK Metal command processor defines `metal_resolve_sync` with default false at line 141. `IssueCopy` calls `CommitAndWaitCurrentCommandBuffer` when enabled after a successful resolve. Its comment describes CPU visibility of completed GPU output. This is a concrete existing diagnostic mechanism related to the upstream readback lead; it does not prove format, endian, tiling or exposure correctness.

Next controlled comparison: launch the same verified candidate-010 with `--metal_resolve_sync=true`, isolated new user/cache/run paths, record the effective args, inspect the same title/menu scene and its diagnostics, then close that instance. Expect a possible performance cost. No additional render-code change is necessary for that test. The separate Plume submission-ID ordering defect remains independently confirmed.

The public issue comments fetched through GitHub API are stored in ignored `reports/xenia-blur-20261030/issue-589-comments.json`. No reference installation, original executable, game data or candidate runtime was modified during this research.


## Resolve-sync comparison result

The user authorized the comparison. Candidate-010 was launched with
`--metal_resolve_sync=true` in `run-002-resolve-sync`, using the same verified
runtime artifacts and fresh isolated user/cache directories. The user reported
`Still dark/red`. Logs again show one Plume UI submission, no composite or resolve
completion, monotonically advancing submission-ID rejections, and the existing
1280-wide resolve / 320-pitch errors. The diagnostic option alone did not resolve
the reported appearance; this does not exclude other HDR/readback/format/tiling
or synchronization defects. The option was supplied through the CLI; no separate
GPU synchronization instrumentation or framebuffer comparison was collected.
The test process was closed after the visual report, with termination confirmed.
No default setting, original game input or renderer source was changed. The
run's launch, close and diagnostic-summary JSON records are retained beside its
logs in the ignored candidate directory.
