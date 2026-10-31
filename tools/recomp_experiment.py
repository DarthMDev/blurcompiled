#!/usr/bin/env python3
"""Read-only import audit and strict whole-image recompilation experiment.

This reports source registration candidates, not verified API compatibility.
It never invents success-returning shims or changes the reference installation.
"""
import argparse
from collections import Counter
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

from inspect_install import PE
from recomp_slice import ROOT, KIT, EXE, SHA, check_inputs, check_kit


def native_fingerprint():
    """Identify the new adapter sources independently from the upstream kit pin."""
    files = [ROOT / "recomp/game.toml"] + sorted((ROOT / "recomp/native").glob("*"))
    return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in files if p.is_file()}


def prepare(source):
    """Copy only the verified exe into ignored scratch; preserve existing input."""
    check_kit()
    from hashlib import sha256
    data = source.read_bytes()
    if sha256(data).hexdigest() != SHA:
        raise ValueError("Source executable is not the pinned Blur baseline")
    if EXE.is_symlink():
        raise ValueError("Existing local input is a symlink; preserve it and use a regular local copy")
    EXE.parent.mkdir(parents=True, exist_ok=True)
    if EXE.exists():
        if sha256(EXE.read_bytes()).hexdigest() != SHA:
            raise ValueError("Different local input exists; refusing to overwrite")
    else:
        EXE.write_bytes(data)
    print("Verified local executable copy:", EXE)


def audit(out, registry_log=None):
    data, pin = check_inputs()
    pe = PE(data)
    registrations = {}
    literal = re.compile(r'\{\s*"([^"\n]+\.dll)"\s*,\s*"([^"\n]+)"\s*,\s*([^,\n]+),\s*(\w+)\s*\}', re.I)
    for directory in (KIT / "runtime", KIT / "dx"):
        for source in directory.glob("*.cpp"):
            text = source.read_text()
            for match in literal.finditer(text):
                dll, name, argc, handler = match.groups()
                key = (dll.lower(), name)
                record = dict(source=source.relative_to(KIT).as_posix(), handler=handler,
                              arguments=argc.strip(), status="signature_only" if handler == "nullptr" else "handler_candidate")
                if key not in registrations or handler != "nullptr":
                    registrations[key] = record
            # Expand only this explicitly reviewed registration macro.
            if source.name == "bink.cpp":
                for name, count, handler in re.findall(r'\bBINK\((\w+),\s*(\d+),\s*(\w+)\)', text):
                    registrations[("binkw32.dll", f"_Bink{name}@{count}")] = dict(
                        source="dx/bink.cpp", handler=handler, arguments=str(int(count) // 4), status="handler_candidate")
    runtime_registry = {}
    if registry_log:
        mode = None
        for line in registry_log.read_text().splitlines():
            if line.startswith("implemented and called") or line.startswith("implemented but not reached"):
                mode = "registered_handler_unverified"
            elif line.startswith("logging-only"):
                mode = "logging_only"
            elif line.startswith("  ") and "!" in line and mode:
                dll, name = line.strip().split()[0].split("!", 1)
                runtime_registry[(dll.lower(), name)] = mode
    imports = []
    for group in pe.imports():
        rows = []
        for function in group["functions"]:
            key = function.replace("ordinal:", "ord", 1) if function.startswith("ordinal:") else function
            found = registrations.get((group["dll"].lower(), key))
            row = dict(name=function, **(found or dict(status="not_found_by_source_scan")))
            if registry_log:
                row["runtime_registration"] = runtime_registry.get((group["dll"].lower(), key), "not_found_in_run_report")
            rows.append(row)
        group_report = dict(dll=group["dll"], counts=dict(Counter(r["status"] for r in rows)), functions=rows)
        if registry_log:
            group_report["runtime_counts"] = dict(Counter(r["runtime_registration"] for r in rows))
        imports.append(group_report)
    report = dict(executable_sha256=SHA, kit_commit=pin["commit"], imports=imports,
                  total_imports=sum(len(g["functions"]) for g in imports),
                  limitations="Static PE imports only; literal source table entries and reviewed BINK macro only. Registration can be inactive or incomplete; other macros/data aliases may be missed. COM methods, runtime loads and call behavior are not verified.")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + "\n")
    for group in imports:
        print(group["dll"], len(group["functions"]), group.get("runtime_counts", group["counts"]))


def translate(out):
    """Keep a strict failure report even when upstream exits before its JSON."""
    _, pin = check_inputs()
    out.parent.mkdir(parents=True, exist_ok=True)
    log = out.with_suffix(".log")
    generated = ROOT / "local/recomp-blur/full-gen"
    command = [sys.executable, str(KIT / "tools/recomp/translate.py"),
               "--game", str(ROOT / "recomp"), "--out", str(generated),
               "--report", str(out.with_name(out.stem + "-upstream.json"))]
    start = time.monotonic()
    with log.open("w") as output:
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=1200)
    text = log.read_text()
    failures = re.findall(r'fn_([0-9a-f]+) failed: (.*)', text)
    dispatch = re.findall(r'fn_([0-9a-f]+) dispatches to ([0-9a-f]+)', text)
    report = dict(executable_sha256=SHA, kit_commit=pin["commit"], exit_code=result.returncode,
                  seconds=round(time.monotonic() - start, 2),
                  strict=True, command=command, log=str(log),
                  function_failures=[dict(rva=hex(int(a, 16) - 0x400000), reason=r) for a, r in failures],
                  unresolved_dispatch=sorted(set(b for _, b in dispatch)),
                  limitations="Translation success does not prove execution, complete function discovery or gameplay")
    out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return result.returncode


def build_headless(out):
    """Publish only a successful strict translation, then use kit build tools."""
    _, pin = check_inputs()
    strict = json.loads((ROOT / "reports/recomp-strict.json").read_text())
    if strict["exit_code"] != 0 or strict["kit_commit"] != pin["commit"] or strict["executable_sha256"] != SHA:
        raise ValueError("A successful pinned strict translation is required first")
    spec = importlib.util.spec_from_file_location("kit_build", KIT / "tools/build.py")
    kit_build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(kit_build)
    build_root = ROOT / "recomp/build"
    with kit_build.buildlock.BuildLock(build_root.parent, "Blur experiment"):
        generated = ROOT / "local/recomp-blur/full-gen"
        published = build_root / "recomp/gen"
        # Preserve timestamps when changing only handwritten adapters; generated
        # game code need not be recompiled on every incremental build.
        same = published.is_dir() and (
            {p.name for p in published.iterdir() if p.is_file() and p.name != "x86.h"}
            == {p.name for p in generated.iterdir() if p.is_file()}) and all(
            (published / p.name).is_file() and (published / p.name).read_bytes() == p.read_bytes()
            for p in generated.iterdir() if p.is_file())
        if not same:
            kit_build.publish_generated(build_root, lambda stage: shutil.copytree(generated, stage, dirs_exist_ok=True))
    defines = kit_build.game_defines(ROOT / "recomp", build_root) + ["-DRECOMP_VIDEO=OFF"]
    # Reuse the exact dependency sources already fetched for the stub build.
    for name in ("sdl3", "glslang"):
        source = build_root / "cmake/macos-stub/_deps" / (name + "-src")
        if not source.is_dir():
            raise ValueError("Build the kit's macos-stub dependency tree first")
        defines.append("-DFETCHCONTENT_SOURCE_DIR_%s=%s" % (name.upper(), source))
    out.parent.mkdir(parents=True, exist_ok=True)
    log = out.with_suffix(".log")
    with log.open("w") as output:
        command = [kit_build.cmake_tool("cmake"), "--preset", "macos", "-B",
                   str(build_root / "cmake/macos"), "-DPython3_EXECUTABLE=" + sys.executable] + defines
        result = subprocess.run(command, cwd=KIT, stdout=output, stderr=subprocess.STDOUT, timeout=300)
        if result.returncode == 0:
            command = [sys.executable, str(KIT / "tools/build.py"), "--game-dir", str(ROOT / "recomp"),
                       "--target", "headless", "--jobs", "4"]
            result = subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT, timeout=1800)
    report = dict(executable_sha256=SHA, kit_commit=pin["commit"], exit_code=result.returncode,
                  log=str(log), binary=str(build_root / "recomp/pop_headless"), video_enabled=False,
                  native_sources=native_fingerprint(),
                  limitations="Headless translated host build; no playable-game claim")
    out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return result.returncode


def boot_probe(out):
    """Run the translated headless host against scratch input/profile only."""
    _, pin = check_inputs()
    if EXE.is_symlink():
        raise ValueError("Boot probes require a regular ignored executable copy")
    built = json.loads((ROOT / "reports/recomp-headless-build.json").read_text())
    if built["exit_code"] != 0 or built["kit_commit"] != pin["commit"] or built["executable_sha256"] != SHA:
        raise ValueError("A successful pinned headless build is required")
    if built.get("native_sources") != native_fingerprint():
        raise ValueError("Adapter sources changed; build-headless must record a fresh build before probing")
    binary = ROOT / "recomp/build/recomp/pop_headless"
    scratch = ROOT / "local/recomp-blur"
    env = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_")}
    env.update(RECOMP_EXE=str(EXE), RECOMP_NO_MODS="1",
               RECOMP_PROFILE_DIR=str(scratch / "translated-boot-profile"),
               RECOMP_FRAMES=str(scratch / "translated-boot-frames"),
               RECOMP_MAX_SECONDS="10", RECOMP_MAX_FRAMES="1", RECOMP_FRAME_EVERY="0",
               RECOMP_LOG="1", RECOMP_IMPORT_STATS="1",
               RECOMP_DISCOVERY=str(ROOT / "reports/recomp-boot-discovery.txt"))
    out.parent.mkdir(parents=True, exist_ok=True)
    log = out.with_suffix(".log")
    timed_out = False
    with log.open("w") as output:
        try:
            result = subprocess.run([str(binary)], cwd=ROOT, env=env,
                                    stdout=output, stderr=subprocess.STDOUT, timeout=30)
            code = result.returncode
        except subprocess.TimeoutExpired:
            timed_out, code = True, -1
    text = log.read_text()
    frames = re.search(r'presented frames:\s*(\d+)', text)
    report = dict(executable_sha256=SHA, kit_commit=pin["commit"], exit_code=code,
                  timed_out=timed_out, log=str(log), mode="translated headless host",
                  native_sources=native_fingerprint(),
                  presented_frames=int(frames.group(1)) if frames else None,
                  input_unchanged=hashlib.sha256(EXE.read_bytes()).hexdigest() == SHA,
                  limitations="Isolated startup diagnostic with exe only, no assets, mods or video; no playable-game claim")
    out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return code if code >= 0 else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "audit", "translate", "build-headless", "boot-probe"))
    parser.add_argument("--out", type=Path)
    parser.add_argument("--exe", type=Path, help="Reference executable for prepare; copied read-only")
    parser.add_argument("--registry-log", type=Path, help="Optional headless runtime import report from this pin")
    args = parser.parse_args()
    if args.action == "prepare":
        if not args.exe:
            parser.error("prepare requires --exe")
        prepare(args.exe)
        return 0
    if not args.out:
        parser.error("audit/translate/build-headless require --out")
    if args.action == "audit":
        audit(args.out, args.registry_log)
        return 0
    if args.action == "translate":
        return translate(args.out)
    if args.action == "build-headless":
        return build_headless(args.out)
    return boot_probe(args.out)


if __name__ == "__main__":
    sys.exit(main())
