#!/usr/bin/env python3
"""Generate one hash-pinned CRC routine; compare ARM64 C with original x86.

Original instructions/listings stay in ignored local directories. Verification
uses synthetic memory and Unicorn, not a running Windows game process.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
KIT = ROOT / "local/recomp-kit"
EXE = ROOT / "local/recomp-blur/original/Blur.exe"
SHA = "dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f"
VA, RVA, SIZE = 0x00cb6460, 0x008b6460, 0x8e


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def check_kit():
    """Reject a different or modified runtime/translator checkout."""
    pin = json.loads((ROOT / "recomp/kit-pin.json").read_text())
    actual = subprocess.check_output(["git", "-C", str(KIT), "rev-parse", "HEAD"], text=True).strip()
    if actual != pin["commit"]:
        raise ValueError("recomp-kit revision differs from recomp/kit-pin.json")
    if subprocess.check_output(["git", "-C", str(KIT), "status", "--porcelain"], text=True).strip():
        raise ValueError("recomp-kit checkout is modified; record a new pin before verification")
    return pin


def check_inputs():
    """Reject different kit/executable identities before using any addresses."""
    pin = check_kit()
    data = EXE.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA:
        raise ValueError("Blur.exe SHA-256 mismatch; offsets cannot be reused")
    return data, pin


def generate(out):
    check_inputs()
    sys.path.insert(0, str(KIT / "tools/recomp"))
    import translate as t
    cfg = t.game_config.load(ROOT / "recomp")
    t.configure(cfg)
    image = t.Image(str(EXE))
    listing = cfg["listings_path"] / "functions/00cb6460.asm"
    fn = t.Function(VA, "crc_table_candidate", SIZE, t.parse_listing(str(listing)))
    fn.measure(image)
    translator = t.Translator(image, {VA}, SimpleNamespace(eager_flags=True))
    translator.prepare(fn)
    body = "\n".join(translator.translate(fn))
    if translator.unmodelled or "FN(" in body:
        raise ValueError("CRC slice needs an unsupported instruction or another function")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text('#include "x86.h"\n' + body + "\n")
    print("Generated CRC table builder from the pinned original assembly")


def verify(library, out):
    """Compare full registers, defined flags, output/guard bytes and stack."""
    data, pin = check_inputs()
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn import x86_const as x
    pe = pefile.PE(data=data)
    code = pe.get_data(RVA, SIZE)
    dll = ctypes.CDLL(str(library.resolve()))
    dll.blur_slice_memory.restype = ctypes.POINTER(ctypes.c_uint8)
    dll.blur_slice_run.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32)]
    mem = dll.blur_slice_memory()
    if not mem:
        raise MemoryError("Unable to allocate guest memory")
    base = ctypes.addressof(mem.contents)
    reg_ids = [getattr(x, "UC_X86_REG_" + n) for n in ("EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI")]
    expected = []
    for i in range(256):
        value = i
        for _ in range(8):
            value = (value >> 1) ^ (0xedb88320 if value & 1 else 0)
        expected.append(value)
    expected_bytes = struct.pack("<256I", *expected)
    rng = random.Random(0x8b6460)
    for case in range(16):
        output = 0x02000100 + case * 0x1001  # Includes unaligned guest writes.
        esp, stop = 0x03000800, 0x04000000
        regs = [rng.getrandbits(32) for _ in range(8)]
        regs[4] = esp
        flags = 0x202 | ((case & 1) << 10)
        guarded = bytes([0xa5]) * (1024 + 32)
        stack = bytearray([0x5a] * 4096)
        struct.pack_into("<II", stack, 0x800, stop, output)
        ctypes.memmove(base + output - 16, guarded, len(guarded))
        ctypes.memmove(base + 0x03000000, bytes(stack), len(stack))
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(VA & ~4095, 4096)
        uc.mem_write(VA, code)
        uc.mem_map(output & ~4095, 8192)
        uc.mem_write(output - 16, guarded)
        uc.mem_map(0x03000000, 4096)
        uc.mem_write(0x03000000, bytes(stack))
        uc.mem_map(stop, 4096)
        for reg, value in zip(reg_ids, regs):
            uc.reg_write(reg, value)
        uc.reg_write(x.UC_X86_REG_EFLAGS, flags)
        uc.emu_start(VA, stop, count=20000)
        if uc.reg_read(x.UC_X86_REG_EIP) != stop:
            raise AssertionError("Original routine did not return within instruction budget")
        native_regs = (ctypes.c_uint32 * 9)(*regs, 0)
        native_flags = ctypes.c_uint32(flags)
        if dll.blur_slice_run(native_regs, ctypes.byref(native_flags)) != 1:
            raise AssertionError("Native slice could not run")
        require(list(native_regs)[:8] == [uc.reg_read(r) for r in reg_ids], "Register mismatch")
        require(native_regs[8] == stop, "Native routine did not return to the expected address")
        require(native_flags.value & 0xcd5 == uc.reg_read(x.UC_X86_REG_EFLAGS) & 0xcd5, "Defined flag mismatch")
        native = ctypes.string_at(base + output - 16, len(guarded))
        require(native == bytes(uc.mem_read(output - 16, len(guarded))), "Output/guard mismatch")
        require(native == guarded[:16] + expected_bytes + guarded[-16:], "Independent CRC mismatch")
        require(ctypes.string_at(base + 0x03000000, 4096) == bytes(uc.mem_read(0x03000000, 4096)), "Stack mismatch")
    result = dict(executable_sha256=SHA, kit_commit=pin["commit"], rva=hex(RVA),
                  confidence="Validated translated routine against original instructions in Unicorn and independent CRC",
                  cases=16, entries_per_case=256, registers_match=True, defined_flags_match=True,
                  stack_and_guards_match=True, output_sha256=hashlib.sha256(expected_bytes).hexdigest(),
                  limitations="One integer-only routine; no Windows process, asset loading, graphics, threading or gameplay tested")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("generate", "verify"))
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--library", type=Path)
    args = parser.parse_args()
    if args.action == "generate":
        generate(args.out)
    elif args.library:
        verify(args.library, args.out)
    else:
        parser.error("verify requires --library")


if __name__ == "__main__":
    main()
