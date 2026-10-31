#!/usr/bin/env python3
"""Install the public hash-pinned SDK; never downloads game files."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import platform
import shutil
import stat
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def install(archive, target, pin):
    archive, target = Path(archive), Path(target)
    h = hashlib.sha256()
    with archive.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    if h.hexdigest() != pin["archive_sha256"]:
        raise ValueError("SDK archive SHA-256 mismatch")
    if target.exists():
        raise FileExistsError("SDK destination exists; refusing to overwrite it")
    with zipfile.ZipFile(archive) as z:
        entries = z.infolist()
        if sum(e.file_size for e in entries) > 1024 * 1024 * 1024:
            raise ValueError("SDK archive exceeds extraction bound")
        for e in entries:
            path = PurePosixPath(e.filename)
            if path.is_absolute() or ".." in path.parts or "\\" in e.filename or not path.parts or path.parts[0] != pin["platform"]:
                raise ValueError("Unsafe SDK archive member")
            if stat.S_ISLNK(e.external_attr >> 16):
                raise ValueError("SDK archive symlinks are not supported")
        target.mkdir(parents=True)
        for e in entries:
            destination = target.joinpath(*PurePosixPath(e.filename).parts)
            if e.is_dir():
                destination.mkdir(parents=True, exist_ok=True)
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                with z.open(e) as source, destination.open("xb") as output:
                    shutil.copyfileobj(source, output)
                if e.external_attr >> 16 & 0o111:
                    destination.chmod(0o755)
    prefix = target / pin["platform"]
    if not (prefix / "bin/rexglue").is_file():
        raise ValueError("SDK archive lacks the code generator")
    (target / "installation.json").write_text(json.dumps(pin, indent=2) + "\n")
    return prefix


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, help="Use an already downloaded pinned SDK ZIP")
    parser.add_argument("--output", type=Path, default=ROOT / "local/rexglue-sdk")
    args = parser.parse_args()
    pin = json.loads((ROOT / "recomp/rexglue-sdk-pin.json").read_text())
    if platform.system() != "Darwin" or platform.machine().lower() not in ["arm64", "aarch64"]:
        parser.error("The game-build SDK recipe currently validates macOS Apple Silicon only")
    target = args.output.resolve()
    if target == ROOT / "local" or not target.is_relative_to((ROOT / "local").resolve()):
        parser.error("SDK output must be beneath the ignored local directory")
    if args.archive:
        prefix = install(args.archive, target, pin)
    else:
        with tempfile.TemporaryDirectory(prefix="blurcompiled-sdk-") as tmp:
            archive = Path(tmp) / "sdk.zip"
            with urllib.request.urlopen(pin["archive"], timeout=60) as source, archive.open("wb") as output:
                shutil.copyfileobj(source, output)
            prefix = install(archive, target, pin)
    print(f"Verified SDK installed at {prefix}")


if __name__ == "__main__":
    main()
