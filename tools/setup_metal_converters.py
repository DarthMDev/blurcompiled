#!/usr/bin/env python3
"""Build DXBC conversion dependencies; Apple Shader Converter is user supplied."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
from setup_metal import ROOT, digest, extract, fetch


def copy_headers(source, target):
    # scandir d_type is unreliable on some external project volumes.
    import os
    import stat
    target.mkdir(parents=True, exist_ok=True)
    for name in os.listdir(source):
        src, dst = source / name, target / name
        mode = src.lstat().st_mode
        if stat.S_ISDIR(mode):
            copy_headers(src, dst)
        elif stat.S_ISREG(mode):
            shutil.copyfile(src, dst)
        else:
            raise ValueError(f"Unexpected header entry: {src}")


def prepare(output, cache, apple_converter, jobs):
    for required in ["include/metal_irconverter.h", "lib/libmetalirconverter.dylib"]:
        if not (apple_converter / required).is_file():
            raise ValueError(f"Missing user-installed Apple Metal Shader Converter: {required}")
    if output.exists():
        raise FileExistsError("Output exists; choose a fresh --output")
    pins = json.loads((ROOT / "recomp/metal/converter-dependencies.json").read_text())
    archives = [fetch(pin, cache) for pin in pins]
    for pin, archive in zip(pins, archives):
        extract(archive, output / "source" / pin["name"])
    dxc = output / "source/dxc"
    # Two portability adaptations from Alloy's dxilconv-macos.patch: preserve
    # -DENABLE_SPIRV_CODEGEN=OFF on macOS; avoid a forbidden libc++ trait override.
    cmake = dxc / "CMakeLists.txt"
    text = cmake.read_text()
    anchor = "if(NOT WIN32)\n  set(ENABLE_SPIRV_CODEGEN ON)"
    if text.count(anchor) != 1:
        raise ValueError("DXC SPIR-V option patch context changed")
    cmake.write_text(text.replace(anchor, "if(UNIX AND NOT APPLE)\n  set(ENABLE_SPIRV_CODEGEN ON)"))
    header = dxc / "include/llvm/ADT/StringRef.h"
    text = header.read_text()
    anchor = "namespace std {\n  template<>\n  struct is_nothrow_constructible"
    if text.count(anchor) != 1:
        raise ValueError("DXC libc++ trait patch context changed")
    text = text.replace(anchor, "#if !defined(_LIBCPP_VERSION)\n" + anchor)
    header.write_text(text.replace("// HLSL Change Ends", "#endif\n// HLSL Change Ends"))
    build = output / "dxc-build"
    headers = output / "source/directx-headers/include/directx"
    flags = ["LLVM_INCLUDE_TESTS", "LLVM_INCLUDE_EXAMPLES", "LLVM_INCLUDE_DOCS", "CLANG_BUILD_EXAMPLES", "CLANG_INCLUDE_TESTS", "HLSL_INCLUDE_TESTS", "ENABLE_SPIRV_CODEGEN", "SPIRV_BUILD_TESTS", "CLANG_ENABLE_STATIC_ANALYZER", "CLANG_ENABLE_ARCMT", "LLVM_ENABLE_BINDINGS", "LLVM_ENABLE_LIBXML2", "LLVM_ENABLE_ASSERTIONS"]
    subprocess.run(["cmake", "-S", str(dxc), "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_CXX_STANDARD=17", "-DCMAKE_CXX_STANDARD_REQUIRED=ON", "-DCMAKE_CXX_EXTENSIONS=OFF", "-DCMAKE_OSX_ARCHITECTURES=arm64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5", "-DCMAKE_CXX_FLAGS=-stdlib=libc++ -Wno-deprecated-declarations -Wno-deprecated", f"-DD3D12_INCLUDE_DIR={headers}", f"-DDXGI_INCLUDE_DIR={headers}", "-DLLVM_TARGETS_TO_BUILD=None", "-DLLVM_ENABLE_EH=ON", "-DLLVM_ENABLE_RTTI=ON", "-DLLVM_ENABLE_PIC=ON", *[f"-D{f}=OFF" for f in flags]], check=True)
    subprocess.run(["cmake", "--build", str(build), "--target", "dxilconv", "LLVMDxcSupport", "--parallel", str(jobs)], check=True)
    return stage_converters(output, apple_converter, pins)


def stage_converters(output, apple_converter, pins):
    dxc = output / "source/dxc"
    build = output / "dxc-build"
    stage = output / "prefix"
    copy_headers(dxc / "include", stage / "dxc/include")
    copy_headers(dxc / "projects/dxilconv/include", stage / "dxc/include")
    copy_headers(output / "source/metal-cpp", stage / "metal-cpp/include")
    copy_headers(output / "source/spirv-cross", stage / "spirv-cross")
    import os
    import stat
    (stage / "msc/include").mkdir(parents=True)
    for name in os.listdir(apple_converter / "include"):
        if not (name.startswith("metal_irconverter") or name.startswith("ir_") or name in ["LICENSE.txt", "Acknowledgements.rtf"]):
            continue
        source = apple_converter / "include" / name
        target = stage / "msc/include" / name
        if stat.S_ISDIR(source.lstat().st_mode):
            copy_headers(source, target)
        else:
            shutil.copyfile(source, target)
    (stage / "lib").mkdir()
    for name in ["libdxilconv.dylib", "libLLVMDxcSupport.a"]:
        shutil.copyfile(build / "lib" / name, stage / "lib" / name)
    shutil.copyfile(apple_converter / "lib/libmetalirconverter.dylib", stage / "lib/libmetalirconverter.dylib")
    (output / "source-receipt.json").write_text(json.dumps({"dependencies": pins, "apple_converter": str(apple_converter), "apple_dylib_sha256": digest(apple_converter / "lib/libmetalirconverter.dylib"), "apple_header_sha256": digest(apple_converter / "include/metal_irconverter.h")}, indent=2) + "\n")
    return stage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apple-converter", type=Path, required=True, help="Official Apple converter prefix containing include/ and lib/")
    parser.add_argument("--output", type=Path, default=ROOT / "local/metal-converters")
    parser.add_argument("--cache", type=Path, default=ROOT / "local/source-archives")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    output, cache = args.output.resolve(), args.cache.resolve()
    if not 1 <= args.jobs <= 16:
        parser.error("--jobs must be between 1 and 16")
    for path in [output, cache]:
        if path == ROOT / "local" or not path.is_relative_to((ROOT / "local").resolve()):
            parser.error("Output and cache must be beneath ignored local/")
    print(f"Metal dependencies built: {prepare(output, cache, args.apple_converter.resolve(), args.jobs)}")


if __name__ == "__main__":
    main()
