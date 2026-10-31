# Build BlurCompiled from your own Xbox game files

Validated on 2026-10-31 on Apple M3 / macOS 27.0.1 / AppleClang 21: the patched
SDK and open-source conversion libraries compile from pinned source, all four
locally reconstructed shader fixtures match their captured hashes, and the
native executable reaches the title screen. Logs confirm the MSC Metal backend,
Plume bridge initialization and accepted Plume UI draws. The title scene still
has the known dark/red issue. Full frame replacement and race correctness are
not established. Twelve C++ contracts and 21 Python tests pass.

No old candidate object files, captured shader payloads or prebuilt game host
are required. The supported user-owned game supplies the local inputs.

## Supported input

Supply an extracted Xbox 360 game directory containing `default.xex` and its
original asset directories. The executable accepted by this recipe has:

- Size: 13,905,920 bytes.
- SHA-256: `8ad07d3ad05ef451444f63dfca7d93fd92b70127f641b85d82723140f039ac38`.

Other revisions are rejected because the analysis overrides are binary-specific.
Do not upload your game files. The verifier reads the executable and references
the game directory; it does not modify it or copy its assets into the repository.

## Native Metal + Plume game build

Use this path to reproduce the native Metal backend and **active bounded Plume
UI/composite bridge** used by the graphics investigation. The SDK still renders
most of the scene; this does not implement complete Plume frame replacement or
fix the dark/red graphics issue. The stock SDK path below uses Vulkan/MoltenVK
and produces different rendering failures.

Requirements: Apple Silicon, macOS 15+, Xcode 26 / AppleClang 21, CMake 3.25+,
Ninja, Python 3.10+, and the supported game above. Install
[Apple's Metal Shader Converter](https://developer.apple.com/metal/shader-converter/)
separately. Pass its installation prefix containing
`include/metal_irconverter.h` and `lib/libmetalirconverter.dylib` (normally
`/usr/local`). This repository does not download Apple's converter binary.
Different converter releases can affect compilation and rendering; the local
receipt records its exact library/header hashes.

From the checkout root:

```sh
python3 tools/setup_rexglue.py
python3 tools/setup_plume.py
python3 tools/setup_metal.py
python3 tools/setup_metal_converters.py --apple-converter /usr/local
python3 tools/build_metal_game.py --game-root "/path/to/your/extracted/Blur"
```

Skip a setup command when its verified installation already exists. Setup tools
refuse to overwrite prior trees; use `--output` for another installation. The
build driver accepts `--sdk-source`, `--metal-deps`, `--public-sdk`, `--plume`,
`--output` and `--jobs` (default four). Run it in a normal terminal: decoding and
code generation reserve the SDK's 4 GB virtual address arena.

The SDK and DXBC compiler are compiled from hash-pinned public source. The
verified public SDK archive supplies the bootstrap generator, extraction runtime and Vulkan loader/MoltenVK compatibility dependencies. The source SDK
can also regenerate the translation with its newly compiled generator. The game
runtime, Metal GPU plugin, host, and Plume bridge are newly compiled; no old candidate object files are used.
Building compiler sources takes time and several GB of ignored local space.

The build has two CMake phases. First it builds the Metal plugin's CPU fixture
translator. It then decodes four shader templates from the verified XEX,
applies documented capture-specific vertex normalization, and verifies all
microcode and SPIR-V hashes before writing a local header. Finally it enables
the live Plume hooks and builds the game executable. The fixture metadata lists
executable SHA-256, RVAs, evidence and confidence; no game shader bytes are in Git.

Keep the staged runtime and converter libraries beside the executable. The host
requests the Metal backend explicitly and fails if it cannot create it. Launch:

```sh
local/blurcompiled-metal/build/blur_x360 \
  --game_data_root="/path/to/your/extracted/Blur" \
  --user_data_root="$PWD/local/game-user" --cache_root="$PWD/local/game-cache" \
  --no-fullscreen --window_width=1280 --window_height=720
```

Use a fresh cache when comparing graphics changes. A successful compile and
verified shader fixtures do not establish scene correctness or a playable race.

## Stock public SDK build (Vulkan/MoltenVK)

This simpler path validates game translation and the host, **not** the native
Metal/Plume candidate. It remains useful for comparing the two SDK backends.

### Install the pinned public SDK

Install CMake 3.25+, Python 3.10+, and Apple C++ development tools with C++23
support. For matching the tested toolchain, use AppleClang 21 / Xcode 26.

```sh
python3 tools/setup_rexglue.py
```

If downloading separately, use `--archive /path/to/rexglue-sdk-0.10.0-mac-arm64.zip`.
The installer verifies the same hash either way. It installs approximately
313 MB beneath ignored `local/rexglue-sdk/` and refuses to overwrite a prior
installation. The SDK contains its compiler tools, headers, runtime and GPU plugin.

### Verify the input and generate C++ locally

```sh
python3 tools/prepare_xbox_game.py   --game-root "/path/to/your/extracted/Blur"   --rexglue local/rexglue-sdk/mac-arm64/bin/rexglue
```

This creates ignored `local/blurcompiled-game/` with a native host project,
manifest, analysis overrides and generated code. No generated game code enters
Git. `build-status.json` and `codegen.log` record the exact input/tool hash and
whether generation succeeded. Existing output directories are never overwritten;
choose a fresh `--output local/another-build` for a separate attempt.

The generator reserves a 4 GB virtual guest-address arena. A restricted process
sandbox may prevent this even when ordinary RAM is available. Run code generation
in a normal local terminal if the sandbox refuses that reservation.

### Configure and compile the host

From the repository root:

```sh
cmake -S local/blurcompiled-game -B local/blurcompiled-game/build   -DCMAKE_PREFIX_PATH="$PWD/local/rexglue-sdk/mac-arm64"   -DCMAKE_BUILD_TYPE=Release -DREXGLUE_RECOMP_DEBUG_INFO=none
cmake --build local/blurcompiled-game/build --target blur_x360 --parallel 4
```

The generated SDK glue configures runtime/plugin staging and code generation.
The host uses `PPCImageConfig.func_mappings`; its template excludes the SDK's
unused legacy registrar source, matching the previously validated host and
avoiding an expensive duplicate registration function.
This is a separate build from the repository's graphics-contract tests.
Keep runtime libraries alongside the host; copying only the executable is not
a standalone distribution. Game files must still be available at runtime.

### Renderer and validation boundary

The public SDK supplies the game graphics backend. The new template does not
silently substitute the maintainer's patched Metal plugin or claim complete
Plume rendering. The independent Plume support targets remain available in the
main CMake project. Graphics correctness, full Plume frame integration, Windows,
Linux and an offline race remain milestones.

Preparation, fresh code generation, executable compilation and headless help
are verified. Gameplay remains separate. See [roadmap](ROADMAP.md) for the latest
acceptance status; a successful compile does not establish correct rendering.

### Check the executable

```sh
local/blurcompiled-game/build/blur_x360 --help
```

This exits without initializing SDL or opening a window. Normal launches delegate
to the SDK. To try a windowed game launch locally, supply your original game path:

```sh
local/blurcompiled-game/build/blur_x360 \
  --game_data_root="/path/to/your/extracted/Blur" \
  --user_data_root="$PWD/local/game-user" --cache_root="$PWD/local/game-cache" \
  --no-fullscreen --window_width=960 --window_height=540
```

Keep `librexruntime.dylib`, `librexgpu-xenos.dylib` and any other staged libraries
alongside the executable. This command is a local validation step, not a claim
that rendering or races are correct.
