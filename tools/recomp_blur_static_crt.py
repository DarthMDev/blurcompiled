#!/usr/bin/env python3
"""Isolated experiment: build and test Blur with statically bound Microsoft MSVCR90.

This integrates the verified MSVCR90 translation with Blur's whole-game image,
routing all MSVCR90 imports to the real DLL, initializing guest TEB/TLS,
and evaluating C++ static initializers.
"""
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
KIT = ROOT / "local/recomp-kit-static-binding"
BLUR_EXE = ROOT / "local/recomp-blur/original/Blur.exe"
BLUR_GEN = ROOT / "local/recomp-blur/full-gen"
AUX_GEN = ROOT / "local/recomp-crt-msvc90-execution/callback-round1/generated"
MSVCR90_DLL = ROOT / "local/recomp-crt-msvc90-translation/original/msvcr90.dll"
MSVCR90_ANALYSIS = ROOT / "local/recomp-crt-msvc90-translation/analysis/msvcr90.dll"

WORK = ROOT / "local/recomp-blur-msvcr90"
DEFAULT_JOBS = 6


def copy_if_different(src: Path, dst: Path) -> bool:
    if dst.exists() and dst.stat().st_size == src.stat().st_size:
        if dst.read_bytes() == src.read_bytes():
            return False
    shutil.copy2(src, dst)
    return True


def prepare_work():
    WORK.mkdir(parents=True, exist_ok=True)
    build_root = WORK / "build"
    build_root.mkdir(parents=True, exist_ok=True)
    
    # 1. Prepare game.toml with aux module
    base_toml = (ROOT / "recomp/game.toml").read_text()
    probe_toml = (ROOT / "local/recomp-crt-msvc90-static-r2/game.toml").read_text()
    
    aux_match = re.search(r'(\[modules\.aux\.msvcr90\].*?)(?=\n\[translate\])', probe_toml, re.DOTALL)
    if not aux_match:
        raise ValueError("Could not extract aux.msvcr90 configuration from probe game.toml")
    aux_block = aux_match.group(1).strip()
    # Ensure correct relative paths
    aux_block = aux_block.replace('path = "original/msvcr90.dll"', f'path = "{MSVCR90_DLL.as_posix()}"')
    aux_block = aux_block.replace(
        'listings = "/Users/michael/github/blur-native/local/recomp-crt-msvc90-translation/analysis/msvcr90.dll"',
        f'listings = "{MSVCR90_ANALYSIS.as_posix()}"'
    )
    
    # Insert guest_size into [game]
    game_toml = re.sub(r'(\[game\].*?heap_base = 0x[0-9a-f]+)', r'\1\nguest_size = 0x785c3000', base_toml, count=1, flags=re.DOTALL)
    # Insert aux module before [translate] and set overrides header
    game_toml = game_toml.replace('[translate]', aux_block + '\n\n[translate]\noverrides = "native/startup.h"')
    
    toml_path = WORK / "game.toml"
    if not toml_path.exists() or toml_path.read_text() != game_toml:
        toml_path.write_text(game_toml)
    
    # 2. Stage generated code (Blur + aux-msvcr90)
    gen_dir = build_root / "recomp/gen"
    gen_dir.mkdir(parents=True, exist_ok=True)
    for f in BLUR_GEN.glob("chunk_*.c"):
        copy_if_different(f, gen_dir / f.name)
    for name in ("table.c", "funcs.h", "thunks.h", "x86.h", "symbols.json"):
        if (BLUR_GEN / name).exists():
            copy_if_different(BLUR_GEN / name, gen_dir / name)
            
    aux_dir = gen_dir / "aux-msvcr90"
    aux_dir.mkdir(parents=True, exist_ok=True)
    for f in AUX_GEN.glob("*"):
        if f.is_file():
            copy_if_different(f, aux_dir / f.name)
            
    # Copy native sources
    native_dir = WORK / "native"
    native_dir.mkdir(parents=True, exist_ok=True)
    for f in (ROOT / "recomp/native").glob("*"):
        copy_if_different(f, native_dir / f.name)

    # Copy globals.toml if needed
    if (ROOT / "recomp/globals.toml").exists():
        copy_if_different(ROOT / "recomp/globals.toml", WORK / "globals.toml")
        
    # Copy latest runtime/x86.h with SSE helpers
    copy_if_different(KIT / "runtime/x86.h", gen_dir / "x86.h")
    
    # Lower all unmodelled SSE instructions
    patch_script = ROOT / "tools/patch_unmodelled_sse.py"
    if patch_script.exists():
        spec = importlib.util.spec_from_file_location("patch_sse", patch_script)
        patch_sse = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch_sse)
        patch_sse.patch_all(gen_dir)
        
    print("Work directory prepared:", WORK)


def build(reconfigure: bool = False):
    spec = importlib.util.spec_from_file_location("kit_build", KIT / "tools/build.py")
    kit_build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(kit_build)
    
    build_root = WORK / "build"
    cmake_dir = build_root / "cmake/macos"
    ninja_file = cmake_dir / "build.ninja"
    
    jobs = DEFAULT_JOBS
    
    if reconfigure or not ninja_file.exists():
        defines = kit_build.game_defines(WORK, build_root) + [
            "-DRECOMP_VIDEO=OFF",
            "-DCMAKE_C_FLAGS=-DBLUR_USE_AUX_MSVCR90=1 -DRECOMP_NULL_CHECKS=1",
            "-DCMAKE_CXX_FLAGS=-DBLUR_USE_AUX_MSVCR90=1 -DRECOMP_NULL_CHECKS=1",
        ]
        for name in ("sdl3", "glslang"):
            source = ROOT / "recomp/build/cmake/macos-stub/_deps" / (name + "-src")
            defines.append(f"-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={source}")
            
        print("Configuring CMake...")
        cmd_cmake = [
            kit_build.cmake_tool("cmake"),
            "--preset", "macos",
            "-B", str(cmake_dir),
            f"-DPython3_EXECUTABLE={sys.executable}"
        ] + defines
        subprocess.check_call(cmd_cmake, cwd=KIT)
    
    print(f"Building headless host with MSVCR90 aux module (parallel jobs: {jobs})...")
    ninja_bin = shutil.which("ninja")
    if ninja_bin and ninja_file.exists():
        cmd_build = [ninja_bin, "-C", str(cmake_dir), "-j", str(jobs), "pop_headless"]
        subprocess.check_call(cmd_build, cwd=ROOT)
    else:
        cmd_build = [
            kit_build.cmake_tool("cmake"),
            "--build", str(cmake_dir),
            "--parallel", str(jobs),
            "--target", "pop_headless"
        ]
        subprocess.check_call(cmd_build, cwd=ROOT)
    print("Headless host built successfully:", build_root / "recomp/pop_headless")


def check():
    build_root = WORK / "build"
    cmake_dir = build_root / "cmake/macos"
    jobs = DEFAULT_JOBS
    print(f"Checking native runtime & DX code for compiler errors (jobs: {jobs})...")
    ninja_bin = shutil.which("ninja")
    targets = ["recomp_runtime", "recomp_dx", "host_common"]
    if ninja_bin and (cmake_dir / "build.ninja").exists():
        cmd = [ninja_bin, "-C", str(cmake_dir), "-j", str(jobs)] + targets
        subprocess.check_call(cmd, cwd=ROOT)
    else:
        cmd = ["cmake", "--build", str(cmake_dir), "--parallel", str(jobs), "--target"] + targets
        subprocess.check_call(cmd, cwd=ROOT)
    print("Syntax & type checking passed successfully with 0 errors!")


def probe():
    binary = WORK / "build/recomp/pop_headless"
    scratch = WORK / "boot-scratch"
    scratch.mkdir(parents=True, exist_ok=True)
    
    env = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_")}
    env.update(
        RECOMP_EXE=str(BLUR_EXE),
        RECOMP_NO_MODS="1",
        RECOMP_PROFILE_DIR=str(scratch / "profile"),
        RECOMP_FRAMES=str(scratch / "frames"),
        RECOMP_MAX_SECONDS=os.environ.get("RECOMP_MAX_SECONDS", "10"),
        RECOMP_MAX_FRAMES=os.environ.get("RECOMP_MAX_FRAMES", "1"),
        RECOMP_FRAME_EVERY=os.environ.get("RECOMP_FRAME_EVERY", "0"),
        RECOMP_LOG="1",
        RECOMP_IMPORT_STATS="1",
        RECOMP_DISCOVERY=str(WORK / "discovery.txt")
    )
    
    log_file = WORK / "boot.log"
    print("Running Blur startup probe with real MSVCR90...")
    with log_file.open("w") as out:
        max_sec = float(env.get("RECOMP_MAX_SECONDS", "10"))
        timeout = max(20.0, max_sec + 15.0)
        try:
            res = subprocess.run([str(binary)], cwd=ROOT, env=env, stdout=out, stderr=subprocess.STDOUT, timeout=timeout)
            code = res.returncode
        except subprocess.TimeoutExpired:
            code = -1
            print(f"Probe timed out after {timeout}s")
            
    print(f"Probe completed with exit code: {code}")
    print("--- Log tail ---")
    lines = log_file.read_text().splitlines()
    for line in lines[-40:]:
        print(line)


def main():
    action = sys.argv[1] if len(sys.argv) > 1 else "all"
    if action == "check":
        check()
        return
    if action in ("prepare", "all"):
        prepare_work()
    if action in ("build", "all", "rebuild"):
        build(reconfigure=(action == "rebuild"))
    if action in ("probe", "all"):
        probe()


if __name__ == "__main__":
    main()


