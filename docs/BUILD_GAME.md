# Build BlurCompiled from your own Xbox game files

Validated on 2026-10-31: a fresh translation from the supported input and the
pinned public SDK produced a native ARM64 executable; headless `--help` exits zero.
No saved translation, private SDK workspace or prebuilt game host was reused.
Gameplay and graphics correctness for this build have not yet been validated.

Initial target: macOS on Apple Silicon. The public SDK version is 0.10.0, release
commit `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`. Its archive SHA-256 is pinned in
`recomp/rexglue-sdk-pin.json`. This path does not use the private SDK workspace.

## Supported input

Supply an extracted Xbox 360 game directory containing `default.xex` and its
original asset directories. The executable accepted by this recipe has:

- Size: 13,905,920 bytes.
- SHA-256: `8ad07d3ad05ef451444f63dfca7d93fd92b70127f641b85d82723140f039ac38`.

Other revisions are rejected because the analysis overrides are binary-specific.
Do not upload your game files. The verifier reads the executable and references
the game directory; it does not modify it or copy its assets into the repository.

## Install the pinned public SDK

Install CMake 3.25+, Python 3.10+, and Apple C++ development tools with C++23
support. For matching the tested toolchain, use AppleClang 21 / Xcode 26.

```sh
python3 tools/setup_rexglue.py
```

If downloading separately, use `--archive /path/to/rexglue-sdk-0.10.0-mac-arm64.zip`.
The installer verifies the same hash either way. It installs approximately
313 MB beneath ignored `local/rexglue-sdk/` and refuses to overwrite a prior
installation. The SDK contains its compiler tools, headers, runtime and GPU plugin.

## Verify the input and generate C++ locally

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

## Configure and compile the host

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

## Renderer and validation boundary

The public SDK supplies the game graphics backend. The new template does not
silently substitute the maintainer's patched Metal plugin or claim complete
Plume rendering. The independent Plume support targets remain available in the
main CMake project. Graphics correctness, full Plume frame integration, Windows,
Linux and an offline race remain milestones.

Preparation, fresh code generation, executable compilation and headless help
are verified. Gameplay remains separate. See [roadmap](ROADMAP.md) for the latest
acceptance status; a successful compile does not establish correct rendering.

## Check the executable

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
