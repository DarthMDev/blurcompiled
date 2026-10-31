#!/usr/bin/env python3
"""Verify a user-owned Xbox dump and prepare local ReXGlue code generation.

Never modifies or copies the original game. All generated material stays local.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
RECIPE = ROOT / "recomp/xbox"


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def prepare(game_root, output, *, root=ROOT, recipe=None):
    recipe = recipe or json.loads((RECIPE / "build-inputs.json").read_text())
    game_root = Path(game_root).resolve(strict=True)
    executable = game_root / recipe["executable"]
    if not executable.is_file():
        raise ValueError("Expected an extracted Xbox game directory containing default.xex")
    with executable.open("rb") as f:
        if f.read(4) != b"XEX2":
            raise ValueError("Input is not an Xbox 360 XEX2 executable")
    if executable.stat().st_size != recipe["executable_size"] or digest(executable) != recipe["executable_sha256"]:
        raise ValueError("Unsupported executable SHA-256; overrides apply only to the documented Xbox build")
    output = Path(output).resolve()
    local = (Path(root) / "local").resolve()
    if output == local or not output.is_relative_to(local):
        raise ValueError("Output must be a new directory beneath the repository's ignored local directory")
    if output.exists():
        raise FileExistsError("Refusing to overwrite an existing game workspace")
    if output == game_root or output.is_relative_to(game_root) or game_root.is_relative_to(output):
        raise ValueError("Generated workspace and original game directory must be separate")
    output.mkdir(parents=True)
    (output / "src").mkdir()
    for relative in ["CMakeLists.txt", "src/main.cpp"]:
        shutil.copyfile(RECIPE / relative, output / relative)
    names = [r["file"] for r in recipe["overrides"]]
    for name in names:
        shutil.copyfile(RECIPE / name, output / name)
    quote = lambda value: json.dumps(value, ensure_ascii=False)
    manifest = (
        '[project]\nname = "blur_x360"\nsdk_version = "0.10.0"\n'
        f'game_root = {quote(game_root.as_posix())}\n\n[entrypoint]\n'
        f'file_path = {quote(executable.as_posix())}\n'
        'out_directory_path = "generated/default"\n'
        f'includes = {json.dumps(names)}\n'
    )
    (output / "blur_x360_manifest.toml").write_text(manifest)
    status = {"status": "prepared; code generation not run", "game_root": str(game_root),
              "executable_sha256": recipe["executable_sha256"], "project": str(output),
              "original_game_modified": False, "game_executable_built": False}
    (output / "build-status.json").write_text(json.dumps(status, indent=2) + "\n")
    return output


def generate(project, rexglue):
    project = Path(project)
    rexglue = Path(rexglue).resolve(strict=True)
    recipe = json.loads((RECIPE / "build-inputs.json").read_text())
    version = subprocess.check_output([str(rexglue), "--version"], text=True).strip()
    if version != recipe["sdk_cli_version"]:
        raise ValueError(f"Expected ReXGlue {recipe['sdk_cli_version']}, got {version!r}")
    status_file = project / "build-status.json"
    status = json.loads(status_file.read_text())
    command = [str(rexglue), "codegen", str(project / "blur_x360_manifest.toml")]
    status.update(rexglue_version=version, rexglue_sha256=digest(rexglue), command=command)
    with (project / "codegen.log").open("w") as log:
        result = subprocess.run(command, cwd=project, stdout=log, stderr=subprocess.STDOUT)
    ready = all((project / name).is_file() for name in
                ["generated/rexglue.cmake", "generated/default/sources.cmake", "generated/default/blur_x360_init.h"])
    status.update(codegen_exit_code=result.returncode,
                  status="code generated; executable not built" if result.returncode == 0 and ready else "code generation failed")
    status_file.write_text(json.dumps(status, indent=2) + "\n")
    if result.returncode != 0 or not ready:
        raise RuntimeError(f"Code generation failed; see {project / 'codegen.log'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "local/blurcompiled-game")
    parser.add_argument("--rexglue", type=Path, help="Run codegen using the documented 0.10.0 CLI")
    args = parser.parse_args()
    project = prepare(args.game_root, args.output)
    if args.rexglue:
        generate(project, args.rexglue)
    print(f"Prepared Xbox project: {project}")
    print("Game executable has not been built; see docs/BUILD_GAME.md for the SDK build boundary.")


if __name__ == "__main__":
    main()
