# BlurCompiled

Target the Xbox 360 recompilation with Plume graphics integration. Apple Silicon
is the current development platform; Windows and Linux ports are planned.
The installed game is reference input, not the source tree. Never modify it as part
of analysis. Keep original binaries, extracted assets, user data, packet captures,
and decompiler databases in ignored local directories, outside tracked source.

Distinguish observed facts, hypotheses, recovered behavior, and new implementations.
Every recovered symbol needs executable SHA-256, RVA, evidence, and confidence.
Do not describe the bootstrap renderer as a playable port or decompiled game.
Do not invent Amax messages or assume redirects implement its authentication.
Treat offsets as specific to one binary; translate guest addresses explicitly into native memory.
Prefer small verified slices: asset read, resource decode, render, simulation.

Run `python3 -m unittest discover -s tests -v` and the CMake build after code changes.
Use synthetic fixtures for parser tests. Keep platform APIs out of recovered logic.
