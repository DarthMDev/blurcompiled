#!/usr/bin/env python3
"""Reconstruct bounded Plume fixtures locally from the supported user-owned XEX."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from setup_metal import ROOT, digest


def normalize(raw, shader):
    if hashlib.sha256(raw).hexdigest() != shader["template_sha256"]:
        raise ValueError(f"Shader template mismatch: {shader['name']}")
    values = list(struct.unpack(">" + "I" * shader["word_count"], raw))
    for index, mask in shader["xor_words"].items():
        values[int(index)] ^= mask
    native = struct.pack("<" + "I" * len(values), *values)
    if hashlib.sha256(native).hexdigest() != shader["ucode_sha256"]:
        raise ValueError("Normalized shader hash mismatch")
    return values, native


def prepare(game, output, extractor, plugin):
    pin = json.loads((ROOT / "recomp/metal/shader-fixtures.json").read_text())
    if digest(game / "default.xex") != pin["executable_sha256"]:
        raise ValueError("Unsupported game executable")
    if output.exists():
        raise FileExistsError("Fixture output already exists; choose a fresh directory")
    # Load the newly source-built plugin. The exported helper translates only
    # CPU shader data; it does not create a renderer or execute the game.
    library = ctypes.CDLL(str(plugin))
    translate = library.blurcompiled_translate_fixture
    translate.argtypes = [ctypes.c_uint32, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint32), ctypes.c_uint32, ctypes.c_uint64, ctypes.c_uint32, ctypes.c_char_p]
    translate.restype = ctypes.c_int
    output.mkdir(parents=True)
    arrays = []
    for shader in pin["shaders"]:
        name = shader["name"]
        template = output / (name + ".template")
        subprocess.run([str(extractor), str(game), shader["rva"][2:], format(shader["word_count"] * 4, "x"), str(template)], check=True)
        values, native = normalize(template.read_bytes(), shader)
        (output / (name + ".ucode")).write_bytes(native)
        words = (ctypes.c_uint32 * len(values))(*values)
        spv = output / (name + ".spv")
        result = translate(int(shader["stage"] == "pixel"), int(shader["ucode_hash"]), words, len(values), int(shader["modification"]), pin.get("msaa_flags", 3), str(spv).encode())
        if result or digest(spv) != shader["spirv_sha256"]:
            raise ValueError(f"SPIR-V translation differs from pinned capture: {name} (status {result})")
        arrays.extend([(name + "Ucode", "std::uint32_t", values), (name + "Spv", "std::uint8_t", list(spv.read_bytes()))])
    header = "#pragma once\n#include <array>\n#include <cstdint>\n#include <span>\nnamespace blur::render::guest {\n"
    for name, kind, values in arrays:
        header += f"inline constexpr std::array<{kind}, {len(values)}> kRex{name}{{{{\n"
        header += ",\n".join(",".join(hex(v) for v in values[i:i+16]) for i in range(0, len(values), 16)) + "\n}};\n"
    header += "}\n"
    (output / "rex_plume_embedded_shaders.hpp").write_text(header)
    (output / "fixture-receipt.json").write_text(json.dumps({"pin": pin, "extractor_sha256": digest(extractor), "plugin_sha256": digest(plugin)}, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=Path, required=True)
    parser.add_argument("--extractor", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "local/metal-fixtures")
    args = parser.parse_args()
    output = args.output.resolve()
    game = args.game_root.resolve(strict=True)
    if output == ROOT / "local" or not output.is_relative_to((ROOT / "local").resolve()) or output.is_relative_to(game) or game.is_relative_to(output):
        parser.error("Output must be beneath ignored local/ and separate from original game files")
    prepare(game, output, args.extractor.resolve(strict=True), args.plugin.resolve(strict=True))
    print(f"Verified local fixtures: {output}")


if __name__ == "__main__":
    main()
