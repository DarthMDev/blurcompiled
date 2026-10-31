#!/usr/bin/env python3
"""Isolated, hash-pinned MSVCR90 preflight and strict translation experiment.

The initial translation gate never executes a DLL or changes Blur's build.
Candidate exports are observed PE metadata, not verified ABI implementations.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import subprocess
import sys
import time

try:
    from .inspect_install import PE
except ImportError:
    from inspect_install import PE

ROOT = Path(__file__).resolve().parents[1]
KIT = ROOT / "local/recomp-kit"
WORK = ROOT / "local/recomp-crt-probe"
SOURCE = ROOT / "recomp/crt-probe"
PIN = "eb32cc42623e0e9e868b9d27423a57d5a5cba56b"
DLL_SHA = "f58ddc0f08d75e2d750ad66d54be059cd90e428704b5b4351a4efc1550df13ba"
DLL = Path("/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/i386-windows/msvcr90.dll")
BASE, IMAGE_SIZE, ARENA_SIZE = 0x10000000, 0xa9000, 0x10100000
BASELINE_FILES = ("recomp/game.toml", "recomp/native/startup.cpp", "recomp/native/startup.h",
                  "recomp/globals.toml", "recomp/kit-pin.json", "local/recomp-blur/original/Blur.exe",
                  "reports/recomp-globals-boot.json", "reports/recomp-globals-boot.log",
                  "reports/recomp-headless-build.json")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class CRTImage(PE):
    """Bound PE32 metadata needed by the experiment, including code/data exports.

    Validate ranges against one image/section and distinguish file-backed bytes
    from zero-filled storage. Security-directory offsets are file offsets.
    """
    def __init__(self, data):
        super().__init__(data)
        pe = self.u32(0x3c)
        opt = pe + 24
        opt_size = self.u16(pe + 20)
        self.image_size = self.u32(opt + 56)
        if self.machine != 0x14c or not self.image_size or self.image_base + self.image_size > 0x100000000:
            raise ValueError("Expected a nonwrapping i386 PE32 image")
        if not self.headers_size or self.headers_size > min(len(data), self.image_size):
            raise ValueError("Invalid SizeOfHeaders")
        for i, section in enumerate(self.sections):
            section["flags"] = self.u32(opt + opt_size + i * 40 + 36)
            span = max(section["virtual_size"], section["raw_size"])
            if section["rva"] < self.headers_size or not self.in_image(section["rva"], span):
                raise ValueError("Section lies outside the image or overlaps its headers")
            for other in self.sections[:i]:
                other_span = max(other["virtual_size"], other["raw_size"])
                if section["rva"] < other["rva"] + other_span and other["rva"] < section["rva"] + span:
                    raise ValueError("Overlapping PE sections")
        self.directories = {}
        count = self.u32(opt + 92)
        if count > 16 or 96 + 8 * count > opt_size:
            raise ValueError("Truncated or excessive data-directory table")
        for index in range(count):
            rva, size = self.u32(opt + 96 + index * 8), self.u32(opt + 100 + index * 8)
            if bool(rva) != bool(size):
                raise ValueError("Partially specified data directory")
            if rva:
                if index == 4:
                    self.read(rva, size)
                elif not self.in_image(rva, size):
                    raise ValueError("Data directory outside image")
            self.directories[index] = (rva, size)
        entry_section = self.section(self.entry_rva)
        if not entry_section or not entry_section["flags"] & 0x20000000:
            raise ValueError("PE entry is absent or not executable")
        self.rva_read(self.entry_rva, 1)

    def in_image(self, rva, size):
        return 0 <= rva < self.image_size and 0 <= size <= self.image_size - rva

    def section(self, rva):
        return next((s for s in self.sections if s["rva"] <= rva < s["rva"] +
                     max(s["virtual_size"], s["raw_size"])), None)

    def rva_read(self, rva, size):
        if not self.in_image(rva, size):
            raise ValueError("RVA range outside image")
        if rva < self.headers_size:
            if size > self.headers_size - rva:
                raise ValueError("RVA range crosses headers")
        else:
            section = self.section(rva)
            if not section or size > section["raw_size"] - (rva - section["rva"]):
                raise ValueError("RVA range is not file-backed in one section")
        return self.read(self.offset(rva), size)

    def rva_u32(self, rva):
        return int.from_bytes(self.rva_read(rva, 4), "little")

    def string(self, rva):
        section = self.section(rva)
        remaining = (section["raw_size"] - (rva - section["rva"])) if section else self.headers_size - rva
        raw = self.rva_read(rva, min(remaining, 4096))
        end = raw.find(b"\0")
        if end < 0:
            raise ValueError("Unterminated PE string in file-backed range")
        return raw[:end].decode("ascii", "strict")

    def imports(self):
        # The inherited descriptor parser already checks a bounded terminator;
        # additionally bound each descriptor and thunk to file-backed sections.
        groups = []
        if not self.import_rva:
            return groups
        for i in range(min(self.import_size // 20, 4096)):
            raw = self.rva_read(self.import_rva + i * 20, 20)
            words = [int.from_bytes(raw[n:n + 4], "little") for n in range(0, 20, 4)]
            if not any(words):
                return groups
            original, _, _, name, first = words
            functions = []
            for j in range(65536):
                value = self.rva_u32((original or first) + 4 * j)
                self.rva_read(first + 4 * j, 4)
                if not value:
                    break
                functions.append("ordinal:%d" % (value & 0xffff) if value & 0x80000000
                                 else self.string(value + 2))
            else:
                raise ValueError("Unterminated import thunk table")
            groups.append({"dll": self.string(name), "functions": functions})
        raise ValueError("Unterminated import descriptor table")

    def exports(self):
        rva, size = self.directories.get(0, (0, 0))
        if not rva:
            return []
        self.rva_read(rva, 40)
        first = self.rva_u32(rva + 16)
        count, named = self.rva_u32(rva + 20), self.rva_u32(rva + 24)
        funcs, names, ords = [self.rva_u32(rva + n) for n in (28, 32, 36)]
        if count > self.image_size // 4 or named > count:
            raise ValueError("Excessive export table")
        self.rva_read(funcs, 4 * count)
        self.rva_read(names, 4 * named)
        self.rva_read(ords, 2 * named)
        labels = {}
        for i in range(named):
            ordinal = int.from_bytes(self.rva_read(ords + 2 * i, 2), "little")
            if ordinal >= count:
                raise ValueError("Export ordinal outside address table")
            labels.setdefault(ordinal, []).append(self.string(self.rva_u32(names + 4 * i)))
        result = []
        for i in range(count):
            address = self.rva_u32(funcs + 4 * i)
            if not address:
                continue
            if not self.in_image(address, 1):
                raise ValueError("Export RVA outside image")
            section = self.section(address)
            forwarder = self.string(address) if rva <= address < rva + size else None
            kind = "forwarder" if forwarder else "code" if section and section["flags"] & 0x20000000 else "data_candidate"
            for label in labels.get(i, [None]):
                result.append({"name": label, "ordinal": first + i, "rva": address,
                               "kind": kind, "section": section["name"] if section else None,
                               "forwarder": forwarder})
        return result


def validate_candidate(image, arena_size=ARENA_SIZE):
    if image.image_base != BASE or image.image_size != IMAGE_SIZE or image.entry_rva != 0xd140:
        raise ValueError("Candidate metadata differs from the pinned DLL")
    if arena_size % 4096 or BASE + IMAGE_SIZE > arena_size or BASE < 0x10000000:
        raise ValueError("Candidate does not fit outside runtime reservations")
    if image.directories.get(9, (0, 0)) != (0, 0):
        raise ValueError("Auxiliary static TLS is unsupported in this experiment")


def check_kit():
    actual = subprocess.check_output(["git", "-C", str(KIT), "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output(["git", "-C", str(KIT), "status", "--porcelain"], text=True).strip()
    if actual != PIN or dirty:
        raise ValueError("Experiment requires the clean, pinned recomp-kit checkout")


def fingerprint(paths):
    return {str(p.relative_to(ROOT)): digest(p) for p in paths}


def baseline():
    return fingerprint([ROOT / p for p in BASELINE_FILES])


def verify_baseline(expected):
    if baseline() != expected:
        raise ValueError("Existing Blur baseline changed during the isolated experiment")


def safe_output(path):
    path = path.absolute()
    if path.is_symlink() or path.resolve() != path:
        raise ValueError("Experiment output must not traverse symlinks")
    if not any(parent == ROOT / name for parent in path.parents for name in ("local", "reports")):
        raise ValueError("Generated experiment output must be under ignored local/ or reports/")
    return path


def write_report(path, report):
    path = safe_output(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        raise ValueError("Report already exists; select a new report path to preserve evidence")
    path.write_text(json.dumps(report, indent=2) + "\n")


def run_logged(command, log, timeout, env=None):
    log = safe_output(log)
    log.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    timed_out = False
    with log.open("x") as output:
        try:
            result = subprocess.run([str(p) for p in command], cwd=ROOT, env=env,
                                    stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
            code = result.returncode
        except subprocess.TimeoutExpired:
            timed_out, code = True, -1
    return {"command": [str(p) for p in command], "exit_code": code, "timed_out": timed_out,
            "seconds": round(time.monotonic() - started, 2), "log": str(log.relative_to(ROOT))}


def require_step(step):
    if step["exit_code"] != 0:
        raise ValueError("Command failed; inspect " + step["log"])


def source_registry():
    found = {}
    pattern = re.compile(r'\{\s*"([^"\n]+\.dll)"\s*,\s*"([^"\n]+)"\s*,\s*([^,\n]+),\s*(\w+)\s*\}', re.I)
    for source in (KIT / "runtime").glob("*.cpp"):
        for dll, name, args, handler in pattern.findall(source.read_text()):
            key = (dll.lower(), name)
            if key not in found or handler != "nullptr":
                found[key] = {"source": str(source.relative_to(KIT)), "handler": handler,
                              "arguments": args.strip(), "status": "signature_only" if handler == "nullptr" else "handler_candidate"}
    return found


def preflight(dll_data, blur_data):
    image, blur = CRTImage(dll_data), CRTImage(blur_data)
    validate_candidate(image)
    exports = image.exports()
    requested = next(g["functions"] for g in blur.imports() if g["dll"].lower() == "msvcr90.dll")
    by_name = {e["name"]: e for e in exports if e["name"]}
    registry = source_registry()
    dependencies = []
    for group in image.imports():
        rows = []
        for name in group["functions"]:
            row = dict(name=name, **registry.get((group["dll"].lower(), name.replace("ordinal:", "ord")),
                                                {"status": "not_found_by_literal_scan"}))
            rows.append(row)
        dependencies.append({"dll": group["dll"], "counts": dict(Counter(r["status"] for r in rows)), "functions": rows})
    return {"candidate_sha256": hashlib.sha256(dll_data).hexdigest(), "machine": image.machine,
            "image_base": image.image_base, "image_size": image.image_size, "entry_rva": image.entry_rva,
            "arena_size": ARENA_SIZE, "directories": image.directories,
            "exports": exports, "export_count": len(exports), "blur_import_count": len(requested),
            "missing_blur_exports": sorted(set(requested) - by_name.keys()),
            "blur_export_bindings": [dict(import_name=n, **by_name[n]) for n in requested if n in by_name],
            "dependencies": dependencies,
            "limitations": "Observed PE metadata and conservative literal source scan only; names/registration do not prove ABI or behavior. No DLL execution."}


def prepare(args):
    check_kit()
    saved = baseline()
    if WORK.exists():
        raise ValueError("Experiment directory exists; preserve it rather than overwriting")
    dll_data = args.dll.read_bytes()
    if hashlib.sha256(dll_data).hexdigest() != DLL_SHA:
        raise ValueError("DLL SHA-256 mismatch")
    evidence = preflight(dll_data, (ROOT / "local/recomp-blur/original/Blur.exe").read_bytes())
    safe_output(WORK / "original/msvcr90.dll")
    original = WORK / "original"
    original.mkdir(parents=True)
    (original / "msvcr90.dll").write_bytes(dll_data)
    steps = []
    steps.append(run_logged([args.dlltool, "-m", "i386", "-k", "-d", SOURCE / "kernel32.def",
                             "-l", WORK / "kernel32.lib"], args.out.with_name(args.out.stem + "-dlltool.log"), 30))
    require_step(steps[-1])
    steps.append(run_logged([args.clang, "--target=i686-pc-windows-msvc", "-O2", "-march=i386",
                             "-ffreestanding", "-fno-stack-protector", "-fno-builtin", "-fno-asynchronous-unwind-tables",
                             "-c", SOURCE / "src/probe.c", "-o", WORK / "probe.obj"],
                            args.out.with_name(args.out.stem + "-clang.log"), 60))
    require_step(steps[-1])
    steps.append(run_logged([args.linker, "/machine:x86", "/entry:start", "/subsystem:console", "/nodefaultlib",
                             "/base:0x400000", "/fixed", "/dynamicbase:no", "/safeseh:no", "/timestamp:0",
                             "/out:" + str(original / "crt-probe.exe"), WORK / "probe.obj", WORK / "kernel32.lib"],
                            args.out.with_name(args.out.stem + "-link.log"), 60))
    require_step(steps[-1])
    exe = CRTImage((original / "crt-probe.exe").read_bytes())
    if exe.image_base != 0x400000 or exe.image_base + exe.image_size > 0x02000000:
        raise ValueError("Synthetic executable collides with guest heap")
    config = (SOURCE / "game.toml.in").read_text().replace("@EXE_SHA256@", digest(original / "crt-probe.exe"))
    config = config.replace("@EXE_ENTRY@", hex(exe.image_base + exe.entry_rva)).replace("@DLL_SHA256@", DLL_SHA)
    (WORK / "game.toml").write_text(config)
    (WORK / "globals.toml").write_text("# Synthetic fixture has no recovered game globals.\n[globals]\n")
    # Check this exact configuration with the pinned kit's schema.
    sys.path.insert(0, str(KIT / "tools"))
    import game_config
    game_config.load(WORK)
    source_files = [SOURCE / "src/probe.c", SOURCE / "kernel32.def", SOURCE / "game.toml.in",
                    ROOT / "tools/ghidra/ExportPinnedRecompListings.java", Path(__file__).resolve()]
    info = plistlib.loads(Path("/Applications/CrossOver.app/Contents/Info.plist").read_bytes())
    manifest = {"kit_commit": PIN, "baseline": saved, "source_inputs": fingerprint(source_files),
                "staged_inputs": fingerprint([original / "msvcr90.dll", original / "crt-probe.exe",
                                               WORK / "game.toml", WORK / "globals.toml"]),
                "candidate_source": str(args.dll), "candidate_sha256": DLL_SHA,
                "crossover_version": info.get("CFBundleShortVersionString"),
                "crossover_build": info.get("CFBundleVersion"), "steps": steps,
                "notice": "Wine/CrossOver candidate is local reference input, not redistributed. See pinned kit sample Wine LGPL note; preserve candidate distribution notices before incorporation."}
    verify_baseline(saved)
    if digest(args.dll) != DLL_SHA or digest(original / "msvcr90.dll") != DLL_SHA:
        raise ValueError("Candidate source or staged bytes changed during prepare")
    (WORK / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    write_report(args.out, dict(manifest=manifest, preflight=evidence, fixture=exe.report(),
                                prepared=True, dll_executed=False))
    print("Prepared isolated synthetic fixture and candidate; no DLL execution:", args.out)
    return 0


def verified_manifest():
    check_kit()
    safe_output(WORK / "manifest.json")
    manifest = json.loads((WORK / "manifest.json").read_text())
    if manifest["kit_commit"] != PIN or manifest["candidate_sha256"] != DLL_SHA:
        raise ValueError("Experiment manifest identity mismatch")
    verify_baseline(manifest["baseline"])
    for name, sha in {**manifest["staged_inputs"], **manifest["source_inputs"]}.items():
        path = ROOT / name
        if path.is_symlink() or digest(path) != sha:
            raise ValueError("Experiment input changed: " + name)
    return manifest


def export(args):
    manifest = verified_manifest()
    home = args.ghidra_home
    java = args.java_home
    properties = (home / "Ghidra/application.properties").read_text()
    env = dict(os.environ, JAVA_HOME=str(java), MAXMEM="4G")
    env["PATH"] = str(java / "bin") + os.pathsep + env.get("PATH", "")
    project = WORK / "ghidra"
    project.mkdir(exist_ok=True)
    analysis = WORK / "analysis"
    if analysis.exists():
        raise ValueError("Assembly export exists; preserve it and use an explicit repair round")
    analysis.mkdir()
    report = {"kit_commit": PIN, "candidate_sha256": DLL_SHA, "steps": [],
              "ghidra_properties_sha256": hashlib.sha256(properties.encode()).hexdigest(),
              "ghidra_version": next(line for line in properties.splitlines() if line.startswith("application.version=")),
              "java": str(java), "dll_executed": False, "exporter_sha256": digest(ROOT / "tools/ghidra/ExportPinnedRecompListings.java")}
    for name in ("msvcr90.dll", "crt-probe.exe"):
        image = WORK / "original" / name
        step = run_logged([home / "support/analyzeHeadless", project, "crt-" + name,
                           "-import", image, "-overwrite", "-analysisTimeoutPerFile", "300", "-max-cpu", "4",
                           "-scriptPath", ROOT / "tools/ghidra", "-postScript", "ExportPinnedRecompListings.java",
                           analysis, digest(image)], args.out.with_name(args.out.stem + "-" + name + ".log"), 420, env)
        step["export_present"] = (analysis / name / "functions.tsv").is_file()
        report["steps"].append(step)
        if step["exit_code"] != 0 or not step["export_present"]:
            report["success"] = False
            write_report(args.out, report)
            return 1
        summary = (analysis / name / "summary.txt").read_text()
        if "sha256=" + digest(image) not in summary:
            raise ValueError("Export summary hash mismatch")
    verify_baseline(manifest["baseline"])
    report["success"] = True
    report["listing_inputs"] = fingerprint(sorted(analysis.rglob("*.asm")) + sorted(analysis.rglob("functions.tsv")))
    write_report(args.out, report)
    print("Assembly-only export complete:", args.out)
    return 0


def translate(args):
    manifest = verified_manifest()
    export_report = json.loads(args.export_report.read_text())
    if not export_report.get("success") or export_report.get("candidate_sha256") != DLL_SHA or export_report.get("kit_commit") != PIN:
        raise ValueError("A successful pinned export report is required")
    for name, sha in export_report["listing_inputs"].items():
        if digest(ROOT / name) != sha:
            raise ValueError("Listing changed without a recorded repair export: " + name)
    output = safe_output(WORK / ("strict-" + args.out.stem))
    if output.exists():
        raise ValueError("Translation output exists; preserve the earlier round")
    report = {"kit_commit": PIN, "candidate_sha256": DLL_SHA, "strict": True,
              "export_report": str(args.export_report.relative_to(ROOT)), "export_report_sha256": digest(args.export_report),
              "steps": [], "dll_executed": False, "host_built": False,
              "limitations": "Translation gate only; generated output must not be built or executed after a failed gate."}
    for key in ("msvcr90", "fixture"):
        out = output / key
        upstream = args.out.with_name(args.out.stem + "-" + key + "-upstream.json")
        command = [sys.executable, KIT / "tools/recomp/translate.py", "--game", WORK,
                   "--out", out, "--report", upstream]
        if key == "msvcr90":
            command += ["--module", "msvcr90"]
        step = run_logged(command, args.out.with_name(args.out.stem + "-" + key + ".log"), 300)
        step["module"] = key
        if upstream.is_file():
            step["upstream_report"] = str(upstream.relative_to(ROOT))
            step["upstream"] = json.loads(upstream.read_text())
        log = (ROOT / step["log"]).read_text()
        step["diagnostics"] = [line for line in log.splitlines() if any(marker in line for marker in
                               ("failed:", "dispatches to", "jump tables:", "recovery errors", "could not be modelled", "TranslateError", "FAILED", "unmodelled"))]
        report["steps"].append(step)
        if step["exit_code"] != 0:
            report["success"] = False
            report["stop_reason"] = "Strict %s translation failed; no host build or DLL execution allowed" % key
            break
        stats = step.get("upstream", {})
        if not stats or stats.get("failures") or stats.get("unmodelled") or stats.get("unmodelled_allowed") or stats.get("table_gaps_allowed"):
            report["success"] = False
            report["stop_reason"] = "Strict upstream report missing or reports failures/acceptances"
            break
    else:
        report["success"] = True
    verify_baseline(manifest["baseline"])
    report["baseline_unchanged"] = True
    write_report(args.out, report)
    print(json.dumps({k: v for k, v in report.items() if k != "steps"}, indent=2))
    return 0 if report["success"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "export", "translate"))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--dll", type=Path, default=DLL)
    parser.add_argument("--clang", type=Path, default=Path("/opt/homebrew/opt/llvm/bin/clang"))
    parser.add_argument("--dlltool", type=Path, default=Path("/opt/homebrew/opt/llvm/bin/llvm-dlltool"))
    parser.add_argument("--linker", type=Path, default=Path.home() / ".rustup/toolchains/stable-aarch64-apple-darwin/lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/lld-link")
    parser.add_argument("--ghidra-home", type=Path, default=ROOT / "local/tools/ghidra_12.1.4_PUBLIC")
    parser.add_argument("--java-home", type=Path, default=ROOT / "local/tools/jdk-21.0.12.1+1/Contents/Home")
    parser.add_argument("--export-report", type=Path, default=ROOT / "reports/recomp-crt-export.json")
    args = parser.parse_args()
    args.out = safe_output(args.out)
    if args.out.exists():
        parser.error("Report exists; choose a new output path")
    try:
        return {"prepare": prepare, "export": export, "translate": translate}[args.action](args)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        parser.exit(1, "CRT experiment refused/failed: %s\n" % exc)


if __name__ == "__main__":
    sys.exit(main())
