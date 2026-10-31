# Contributing

The current target is the Xbox 360 recomp with Plume graphics integration.
Graphics correctness is the first priority; Windows/Linux ports, reproducible
builds, online research and Discord Rich Presence also need contributors.

## Before starting

Read the [current status](README.md), [roadmap](docs/ROADMAP.md), and
[development guide](docs/DEVELOPMENT.md). Open an issue for large changes so the
approach can be discussed first. Keep each pull request focused on one problem.

## Quality and evidence

Understand and be able to explain every change you submit, including code made
with AI tools. Describe the concrete problem, resulting behavior, and validation.
Separate observations, hypotheses, recovered behavior and new implementations.
Avoid claiming a warning-free log proves correct rendering.

For game behavior, compare with original hardware or another trustworthy
reference where possible. For recovered symbols, record executable SHA-256, RVA,
evidence and confidence. Offsets belong to a specific binary; never carry guest
pointers directly into native host logic. Keep platform APIs outside recovered
logic. Use synthetic fixtures for parser tests.

## Validation

After code changes run:

```sh
python3 -m unittest discover -s tests -v
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Use your configured build directory (`build-plume` for the optional Plume suite).
Report skipped checks and platform limitations. For rendering changes, include
backend, game input hash, candidate revision, relevant settings, scene, and
before/after evidence. Remove account identifiers and credentials from logs.

## Bug reports

Include OS/architecture, GPU, build revision, backend, reproduction steps,
expected/actual behavior, and relevant sanitized log excerpts. Say whether you
used a diagnostic override or a fresh cache. Never upload your game dump or a
capture containing game binaries or shader bytecode.

## Original inputs and licensing

Do not modify the installed reference game during analysis. Keep game files,
assets, generated translations, packet captures and decompiler databases in
ignored local directories. Do not include them in pull requests or releases.
Preserve third-party notices and identify the origin/license of borrowed code.
Original project contributions are licensed under GPL-3.0-or-later, consistent
with [LICENSE](LICENSE). By contributing original code, you agree to those terms.
Upstream code retains its own license and notices; identify it explicitly.

## Online changes

Document protocols and service compatibility with evidence. Do not invent Amax
messages, promise Blur support from an unrelated service, or treat DNS/URL
redirection as authentication. Use synthetic/offline fixtures before live tests.
Never publish credentials, authentication tokens or private account traffic.

## Development trails

Entire trails can help explain decisions and failed experiments. Review exactly
what will be uploaded; keep game data, private paths and secrets out of published
trails. Agent hook/session folders stay local and ignored. Clear PR descriptions
and reproducible evidence remain necessary.
