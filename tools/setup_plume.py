#!/usr/bin/env python3
"""Install the hash-pinned Metal source subset into ignored local/plume; no Git."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import tarfile
import urllib.request


def main():
    root = Path(__file__).resolve().parents[1]
    pin = json.loads((root / "recomp/plume-pin.json").read_text())
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, help="Use an already downloaded pinned archive")
    args = parser.parse_args()
    target = root / "local/plume"
    if target.exists():
        if all((target / name).is_file() and hashlib.sha256((target / name).read_bytes()).hexdigest() == sha
               for name, sha in pin["files"].items()):
            print("Pinned Plume source already verified")
            return
        raise SystemExit("Existing local/plume differs from the pin; choose a new directory before installing")
    data = args.archive.read_bytes() if args.archive else urllib.request.urlopen(pin["archive"], timeout=60).read()
    if hashlib.sha256(data).hexdigest() != pin["archive_sha256"]:
        raise SystemExit("Plume archive SHA-256 mismatch")
    # Extract only known regular files, with fixed destinations from the pin.
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        prefix = "plume-" + pin["revision"] + "/"
        sources = {}
        for name, sha in pin["files"].items():
            member = archive.getmember(prefix + name)
            if not member.isfile():
                raise SystemExit(f"Not a regular source file: {name}")
            source = archive.extractfile(member).read()
            if hashlib.sha256(source).hexdigest() != sha:
                raise SystemExit(f"Source hash mismatch: {name}")
            sources[name] = source
        for name, source in sources.items():
            destination = target / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(source)
    print(f"Installed pinned Plume {pin['revision']} ({len(sources)} source files)")


if __name__ == "__main__":
    main()
