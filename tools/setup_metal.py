#!/usr/bin/env python3
"""Prepare the pinned native Metal SDK sources; downloads no game files."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def fetch(pin, cache):
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / (pin["archive_sha256"] + ".tar.gz")
    if archive.exists():
        if digest(archive) != pin["archive_sha256"]:
            raise ValueError(f"Cached archive hash mismatch: {archive}")
        return archive
    temporary = archive.with_suffix(".download")
    try:
        request = urllib.request.Request(pin["archive"], headers={"User-Agent": "BlurCompiled-source-setup"})
        with urllib.request.urlopen(request, timeout=120) as src, temporary.open("xb") as dst:
            shutil.copyfileobj(src, dst)
        if digest(temporary) != pin["archive_sha256"]:
            raise ValueError(f"Downloaded archive hash mismatch: {pin['archive']}")
        temporary.rename(archive)
    finally:
        temporary.unlink(missing_ok=True)
    return archive


def extract(archive, target):
    """Strip one archive prefix. Validate every member before creating anything.

    Source archives can contain relative symlinks (libmspack); reject links
    escaping the extraction root, hard links, devices and duplicate paths.
    Avoid filesystem d_type-dependent directory walkers.
    """
    target = Path(target)
    if target.exists() and os.listdir(target):
        raise FileExistsError(f"Nonempty source destination: {target}")
    with tarfile.open(archive, "r:gz") as tar:
        members = tar.getmembers()
        if sum(m.size for m in members) > 1024**3:
            raise ValueError("Source archive exceeds extraction bound")
        prefix = None
        seen = set()
        entries = []
        for m in members:
            path = PurePosixPath(m.name)
            if path.is_absolute() or ".." in path.parts or "\\" in m.name or not path.parts:
                raise ValueError("Unsafe archive path")
            if prefix is None:
                prefix = path.parts[0]
            if path.parts[0] != prefix:
                raise ValueError("Source archive must have one root")
            relative = PurePosixPath(*path.parts[1:])
            if not path.parts[1:]:
                if not m.isdir():
                    raise ValueError("Archive root is not a directory")
                continue
            if str(relative) in seen:
                raise ValueError("Duplicate archive path")
            seen.add(str(relative))
            if not (m.isdir() or m.isfile() or m.issym()):
                raise ValueError("Unsupported archive member type")
            if m.issym():
                link = PurePosixPath(m.linkname)
                if link.is_absolute() or "\\" in m.linkname:
                    raise ValueError("Unsafe source symlink")
                depth = len(relative.parent.parts)
                for part in link.parts:
                    depth += -1 if part == ".." else 0 if part == "." else 1
                    if depth < 0:
                        raise ValueError("Source symlink escapes root")
            entries.append((m, relative))
        links = {str(p) for m, p in entries if m.issym()}
        for _, p in entries:
            if any(str(parent) in links for parent in p.parents):
                raise ValueError("Archive writes through a symlink")
        target.mkdir(parents=True, exist_ok=True)
        for m, p in entries:
            dest = target.joinpath(*p.parts)
            dest.parent.mkdir(parents=True, exist_ok=True)
            if m.isdir():
                dest.mkdir(exist_ok=True)
            elif m.issym():
                dest.symlink_to(m.linkname)
            else:
                with tar.extractfile(m) as src, dest.open("xb") as dst:
                    shutil.copyfileobj(src, dst)
                dest.chmod(0o755 if m.mode & 0o111 else 0o644)


def prepare(output, cache, source_archive=None, workers=4):
    pin = json.loads((ROOT / "recomp/metal/sdk-pin.json").read_text())
    deps = json.loads((ROOT / "recomp/metal/dependencies.json").read_text())
    if output.exists():
        raise FileExistsError("Output exists; choose a fresh --output")
    patch = ROOT / pin["patch"]
    if digest(patch) != pin["patch_sha256"]:
        raise ValueError("SDK patch hash mismatch")
    archive = source_archive or fetch(pin, cache)
    if digest(archive) != pin["archive_sha256"]:
        raise ValueError("SDK source archive hash mismatch")
    # Complete downloads first, so network failure leaves no half-prepared SDK.
    with ThreadPoolExecutor(max_workers=workers) as pool:
        archives = list(pool.map(lambda dep: fetch(dep, cache), deps))
    sdk = output / "sdk"
    extract(archive, sdk)
    subprocess.run(["patch", "-p1", "-F0", "--batch", "--forward", "-i", str(patch)], cwd=sdk, check=True)
    for name, expected in pin["files"].items():
        if digest(sdk / name) != expected:
            raise ValueError(f"Patched SDK hash mismatch: {name}")
    for dep, archive in zip(deps, archives):
        target = sdk / dep["path"]
        extract(archive, target)
        (target / ".blurcompiled-source.json").write_text(json.dumps(dep, indent=2) + "\n")
    (output / "source-receipt.json").write_text(json.dumps({"sdk": pin, "dependencies": deps}, indent=2) + "\n")
    return sdk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "local/metal-sdk")
    parser.add_argument("--cache", type=Path, default=ROOT / "local/source-archives")
    parser.add_argument("--source-archive", type=Path, help="Previously downloaded pinned SDK tar.gz")
    args = parser.parse_args()
    output, cache = args.output.resolve(), args.cache.resolve()
    for path in [output, cache]:
        if path == ROOT / "local" or not path.is_relative_to((ROOT / "local").resolve()):
            parser.error("Output and cache must be beneath ignored local/")
    print(f"Prepared SDK source: {prepare(output, cache, args.source_archive)}")


if __name__ == "__main__":
    main()
