#!/usr/bin/env python3
"""Stage local Metal compiler dylibs and make the staged copies relocatable."""
import argparse
from pathlib import Path
import shutil
import subprocess


def stage(runtime, dependencies):
    runtime = runtime.resolve(strict=True)
    gpu = runtime / "librexgpu-xenos.dylib"
    if not gpu.is_file():
        raise ValueError("Missing freshly staged GPU plugin")
    libraries = [gpu]
    for name in ["libdxilconv.dylib", "libmetalirconverter.dylib"]:
        destination = runtime / name
        shutil.copyfile(dependencies / "lib" / name, destination)
        libraries.append(destination)
    for library in libraries:
        commands = subprocess.check_output(["otool", "-l", str(library)], text=True).splitlines()
        rpaths = []
        for i, line in enumerate(commands):
            if line.strip() == "cmd LC_RPATH":
                rpaths.append(commands[i + 2].strip().split(" (offset")[0].removeprefix("path "))
        changes = ["install_name_tool"]
        for path in dict.fromkeys(rpaths):
            if path.startswith("/"):
                changes.extend(["-delete_rpath", path])
        if "@loader_path" not in rpaths:
            changes.extend(["-add_rpath", "@loader_path"])
        linked = subprocess.check_output(["otool", "-L", str(library)], text=True).splitlines()[1:]
        for line in linked:
            dependency = line.strip().split(" (compatibility")[0]
            if dependency.startswith("/") and Path(dependency).name in ["libdxilconv.dylib", "libmetalirconverter.dylib"]:
                changes.extend(["-change", dependency, "@rpath/" + Path(dependency).name])
        if library != gpu:
            changes.extend(["-id", "@rpath/" + library.name])
        subprocess.run([*changes, str(library)], check=True)
        subprocess.run(["codesign", "--force", "--sign", "-", str(library)], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", type=Path)
    parser.add_argument("dependencies", type=Path)
    args = parser.parse_args()
    stage(args.runtime, args.dependencies.resolve(strict=True))
