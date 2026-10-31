#!/usr/bin/env python3
"""Build the native Metal + bounded Plume game host from user-owned inputs."""
import argparse
import json
import platform
from pathlib import Path
import subprocess
import sys
from prepare_xbox_game import prepare, generate
from prepare_metal_fixtures import prepare as prepare_fixtures
from setup_metal import ROOT, digest


def run(command):
    print("Running:", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True)


def build(game, project, sdk, dependencies, public_sdk, plume, jobs):
    # All original game access is read-only. Each host/fixture build is new.
    prepare(game, project)
    generate(project, public_sdk / "bin/rexglue")
    build_dir = project / "build"
    fixtures = project / "fixtures"
    configure = ["cmake", "-S", project, "-B", build_dir, "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_OSX_ARCHITECTURES=arm64", "-DREXGLUE_RECOMP_DEBUG_INFO=none",
                 "-DREXGLUE_ENABLE_TRACY=OFF", "-DBLUR_METAL_PLUME=ON",
                 f"-DREXSDK_DIR={sdk}", f"-DBLURCOMPILED_SOURCE_DIR={ROOT}",
                 f"-DREXGLUE_METAL_DEPS_ROOT={dependencies}",
                 f"-DBLUR_PUBLIC_SDK_PREFIX={public_sdk}",
                 f"-DBLUR_PLUME_SOURCE_DIR={plume}", f"-DBLUR_PLUME_FIXTURE_DIR={fixtures}"]
    run([*configure, "-DBLUR_METAL_PREPARE_FIXTURES=ON"])
    run(["cmake", "--build", build_dir, "--target", "rexgpu-xenos", "--parallel", jobs])
    extractor_build = project / "extractor"
    run(["cmake", "-S", ROOT / "recomp/metal/fixtures", "-B", extractor_build,
         "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_PREFIX_PATH={public_sdk}"])
    run(["cmake", "--build", extractor_build, "--parallel", jobs])
    plugin = sdk / "out/mac-arm64/librexgpu-xenos.dylib"
    prepare_fixtures(game, fixtures, extractor_build / "blur-extract-shader", plugin)
    run([*configure, "-DBLUR_METAL_PREPARE_FIXTURES=OFF"])
    run(["cmake", "--build", build_dir, "--target", "blur_x360", "--parallel", jobs])
    executable = build_dir / "blur_x360"
    run([executable, "--help"])
    (project / "metal-build-status.json").write_text(json.dumps({
        "status": "native Metal and bounded Plume host compiled; gameplay not established",
        "sdk_source": str(sdk), "metal_dependencies": str(dependencies),
        "fixtures": str(fixtures), "complete_plume_frame": False,
        "executable": str(executable), "executable_sha256": digest(executable),
        "headless_help_verified": True, "game_launched": False,
    }, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "local/blurcompiled-metal")
    parser.add_argument("--sdk-source", type=Path, default=ROOT / "local/metal-sdk/sdk")
    parser.add_argument("--metal-deps", type=Path, default=ROOT / "local/metal-converters/prefix")
    parser.add_argument("--public-sdk", type=Path, default=ROOT / "local/rexglue-sdk/mac-arm64")
    parser.add_argument("--plume", type=Path, default=ROOT / "local/plume")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if sys.platform != "darwin" or platform.machine().lower() not in ["arm64", "aarch64"]:
        parser.error("Native Metal builds currently require macOS Apple Silicon")
    if not 1 <= args.jobs <= 16:
        parser.error("--jobs must be between 1 and 16")
    build(args.game_root.resolve(strict=True), args.output.resolve(),
          args.sdk_source.resolve(strict=True), args.metal_deps.resolve(strict=True),
          args.public_sdk.resolve(strict=True), args.plume.resolve(strict=True), args.jobs)


if __name__ == "__main__":
    main()
