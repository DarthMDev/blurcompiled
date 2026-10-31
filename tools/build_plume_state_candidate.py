#!/usr/bin/env python3
"""Build an isolated SDK Plume candidate from verified reference objects.

Does not execute a game or mutate SDK source/reference outputs. Uses Ninja only
to read recorded commands; invokes the selected compiler/linker with new outputs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def patch_spirv_fetch_exponent(text):
    """Port the BSD-licensed SDK fix without changing reference source.

    Evidence: shipa-2/rexglue-xerenge commit
    6c94ffdc5d5c5e3fe630d6d887361f4f605b7472. A diagnostic switch permits a
    same-binary comparison with the old word-4 behavior, using fresh caches.
    """
    old = '''        // Apply the exponent bias from the bits 13:18 of the fetch constant
        // word 4.
        spv::Id result_exponent_bias = builder_->createBinBuiltinCall(
            type_float_, ext_inst_glsl_std_450_, GLSLstd450Ldexp, const_float_1_,
            builder_->createTriOp(spv::OpBitFieldSExtract, type_int_, fetch_constant_word_4_signed,
                                  builder_->makeUintConstant(13), builder_->makeUintConstant(6)));
'''
    replacement = '''        // Result exponent is signed bits 13:18 of fetch word 3. Word 4
        // contains LOD bias and must not attenuate the sampled color.
        const bool exponent_fix = REXCVAR_GET(blur_spirv_fetch_exponent_fix);
        static std::atomic_flag exponent_reported = ATOMIC_FLAG_INIT;
        if (!exponent_reported.test_and_set(std::memory_order_relaxed)) {
          REXLOG_INFO("Blur SPIR-V texture exponent: fetch word {}", exponent_fix ? 3 : 4);
        }
        spv::Id exponent_word = fetch_constant_word_4_signed;
        if (exponent_fix) {
          id_vector_temp_.clear();
          id_vector_temp_.push_back(const_int_0_);
          id_vector_temp_.push_back(
              builder_->makeIntConstant(int((fetch_constant_word_0_index + 3) >> 2)));
          id_vector_temp_.push_back(
              builder_->makeIntConstant(int((fetch_constant_word_0_index + 3) & 3)));
          exponent_word = builder_->createUnaryOp(
              spv::OpBitcast, type_int_,
              builder_->createLoad(
                  builder_->createAccessChain(spv::StorageClassUniform, uniform_fetch_constants_,
                                              id_vector_temp_), spv::NoPrecision));
        }
        spv::Id result_exponent_bias = builder_->createBinBuiltinCall(
            type_float_, ext_inst_glsl_std_450_, GLSLstd450Ldexp, const_float_1_,
            builder_->createTriOp(spv::OpBitFieldSExtract, type_int_, exponent_word,
                                  builder_->makeUintConstant(13), builder_->makeUintConstant(6)));
'''
    if text.count(old) != 1 or text.count("namespace rex::graphics {\n") != 1:
        raise SystemExit("SDK SPIR-V exponent patch context changed")
    text = text.replace(old, replacement)
    return text.replace("namespace rex::graphics {\n", '''#include <atomic>
#include <rex/cvar.h>
#include <rex/logging.h>
REXCVAR_DEFINE_BOOL(blur_spirv_fetch_exponent_fix, true, "GPU",
                   "Use fetch word 3 for SPIR-V texture result exponent bias.");

namespace rex::graphics {
''', 1)


def use_textual_prefix_header(arguments, source_directory):
    """Compile from the verified headers, without reusing a timestamped PCH.

    Recovered dependencies retain their contents but may have archive mtimes.
    Keep the original prefix-header contents and all other compiler flags.
    """
    result = []
    removed = rewritten = 0
    index = 0
    while index < len(arguments):
        if arguments[index:index+2] == ["-Xclang", "-include-pch"]:
            if arguments[index+2:index+3] != ["-Xclang"]:
                raise SystemExit("Unexpected precompiled-header argument layout")
            removed += 1
            index += 4
        elif arguments[index:index+2] == ["-Xclang", "-include"]:
            if arguments[index+2:index+3] != ["-Xclang"]:
                raise SystemExit("Unexpected prefix-header argument layout")
            original = Path(arguments[index+3])
            if original.name != "cmake_pch.hxx":
                raise SystemExit("Unexpected reference prefix header")
            copied = source_directory/"reference_prefix.hpp"
            data = original.read_bytes()
            if copied.exists() and copied.read_bytes() != data:
                raise SystemExit("Reference prefix headers differ between translation units")
            copied.write_bytes(data)
            result += ["-include", str(copied)]
            rewritten += 1
            index += 4
        else:
            result.append(arguments[index])
            index += 1
    if removed != 1 or rewritten != 1:
        raise SystemExit("Expected one explicit PCH and one textual prefix header")
    return result


def main():
    root=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output",type=Path,help="New ignored candidate directory")
    parser.add_argument("--spirv-exponent-fix", action="store_true",
                        help="Patch an isolated SPIR-V translator object and select that backend")
    parser.add_argument("--resolve-pitch-trial", action="store_true",
                        help="Enable the bounded fullscreen source-pitch diagnostic trial")
    parser.add_argument("--fixture-dir", type=Path, default=root/"local/metal-fixtures",
                        help="Verified local shader fixture directory")
    args=parser.parse_args();output=args.output.resolve()
    if not (args.fixture_dir / "rex_plume_embedded_shaders.hpp").is_file():
        raise SystemExit("Generate local shader fixtures first, or supply --fixture-dir")
    if output.exists():raise SystemExit("Output already exists; refusing to overwrite a checkpoint")
    reference=root/"local/x360-feasibility/internal-workspaces-20261025/blur-metal-candidate-20261026"
    sdk=reference/"sdk";build=reference/"build";runtime=reference/"capture-v2/runtime"
    pin=json.loads((root/"recomp/rex-plume-state-pin.json").read_text())
    for name,expected in pin["files"].items():
        if sha(sdk/name)!=expected:raise SystemExit(f"Reference SDK pin mismatch: {name}")
    source=sdk/"src/graphics/metal_runtime/metal_command_processor.cpp"
    object_name="src/graphics/CMakeFiles/rexgpu-xenos.dir/metal_runtime/metal_command_processor.cpp.o"
    original_plugin=runtime/"librexgpu-xenos.dylib"
    protected={str(p):sha(p) for p in (source,build/object_name,original_plugin)}
    ninja=shutil.which("ninja") or shutil.which("ninja",path="/opt/homebrew/bin:/usr/local/bin")
    if not ninja:raise SystemExit("Ninja is required to read the reference build commands")
    def commands(target):
        return subprocess.run([ninja,"-C",str(build),"-t","commands",str(target)],check=True,text=True,capture_output=True).stdout.splitlines()
    compiles=[shlex.split(line) for line in commands(object_name) if " -c " in line and str(source) in line]
    if len(compiles)!=1:raise SystemExit("Ambiguous reference compiler command")
    compile_args=compiles[0]
    if compile_args[compile_args.index("-o")+1]!=object_name or compile_args[-1]!=str(source):
        raise SystemExit("Unexpected reference compile output/source")
    raw=shlex.split(commands(original_plugin)[-1])
    if raw[:2]!=[":","&&"] or raw[-2:]!=["&&",":"]:raise SystemExit("Unexpected reference link wrapper")
    link_args=raw[2:-2]
    if any(token in {"&&",";","|"} for token in link_args):raise SystemExit("Unsupported reference link command")
    if link_args[link_args.index("-o")+1]!=str(original_plugin) or link_args.count(object_name)!=1:
        raise SystemExit("Unexpected reference plugin output/object")
    library=root/"build-plume/libblur-rex-plume-state.a"
    if not library.is_file():raise SystemExit("Build the BLUR_REX_PLUME_FRONTEND CMake target first")
    text=source.read_text()
    if args.resolve_pitch_trial:
        anchor = '''  if (!render_target_cache_->Resolve(*memory_, written_address, written_length,
                                     copy_command_buffer)) {'''
        if text.count(anchor) != 1:
            raise SystemExit("SDK resolve-pitch trial insertion context changed")
        text = text.replace(anchor, '''  const bool resolved = [&] {
    auto& surface_word = register_file_->values[XE_GPU_REG_RB_SURFACE_INFO];
    struct RestoreSurface {
      uint32_t& word;
      uint32_t original;
      ~RestoreSurface() { word = original; }
    } restore{surface_word, surface_word};
    if (REXCVAR_GET(blur_resolve_pitch_trial)) {
      const auto fetch = register_file_->GetVertexFetch(0);
      const uint64_t address = uint64_t(fetch.address) * sizeof(uint32_t);
      if (fetch.type == xenos::FetchConstantType::kVertex && fetch.size == 6 &&
          address <= 0x20000000ull - 24) {
        const auto* guest = reinterpret_cast<const float*>(memory_->TranslatePhysical(uint32_t(address)));
        float vertices[6];
        for (size_t i = 0; i < 6; ++i) vertices[i] = xenos::GpuSwap(guest[i], fetch.endian);
        const auto trial = blur::render::guest::fullscreen_resolve_pitch_trial(
            {register_file_->values, blur::render::guest::rex_register_word_count}, vertices);
        if (trial) {
          surface_word = *trial;
          static uint64_t applied = 0;
          if (++applied <= 4 || applied % 256 == 0)
            REXLOG_WARN("Blur resolve pitch trial: source 320 -> 1280 for full-screen copy; applied={} control={:08X}",
                        applied, register_file_->values[XE_GPU_REG_RB_COPY_CONTROL]);
        }
      }
    }
    return render_target_cache_->Resolve(*memory_, written_address, written_length,
                                         copy_command_buffer);
  }();
  if (!resolved) {''')
        text = '''#include "rex_resolve_pitch_trial.hpp"
#include <rex/cvar.h>
REXCVAR_DEFINE_BOOL(blur_resolve_pitch_trial, false, "GPU",
                   "Diagnostic: test fullscreen source pitch against destination; disabled by default.");
''' + text
    if args.spirv_exponent_fix:
        # The preserved SDK disables its existing SPIRV-Cross implementation.
        # Opt into it only in this isolated diagnostic build, leaving the
        # reference SDK and normal candidate path unchanged.
        guard = '''  if (REXCVAR_GET(metal_use_spirvcross)) {
    REXLOG_ERROR("This ReXGlue Metal backend requires Metal Shader Converter; SPIRV-Cross is not supported");
    return false;
  }
'''
        if text.count(guard) != 1:
            raise SystemExit("SDK SPIRV-Cross setup guard context changed")
        text = text.replace(guard, '''  if (REXCVAR_GET(metal_use_spirvcross)) {
    REXLOG_WARN("Blur isolated diagnostic: enabling experimental SPIRV-Cross backend");
  }
''')
    from instrument_metal import instrument
    text = instrument(text)
    output.mkdir(parents=True);(output/"source").mkdir();(output/"runtime").mkdir()
    new_source=output/"source/metal_command_processor.cpp";new_source.write_text(text)
    new_object=output/"metal_command_processor.cpp.o"
    compile_args=[str(new_source) if a==str(source) else str(new_object) if a==object_name else
                  str(new_object)+".d" if a==object_name+".d" else a for a in compile_args]
    compile_args[1:1]=["-I"+str(args.fixture_dir.resolve()),"-I"+str(root/"src"),"-I"+str(root/"recomp/native"),"-I"+str(source.parent)]
    compile_args = use_textual_prefix_header(compile_args, output/"source")
    new_plugin=output/"runtime/librexgpu-xenos.dylib"
    link_args=[str(new_plugin) if a==str(original_plugin) else str(new_object) if a==object_name else a for a in link_args]
    extra_compiles = []
    patches = []
    if args.resolve_pitch_trial:
        patches.append({"name":"Bounded fullscreen source-pitch diagnostic trial",
                        "runtime_control":"blur_resolve_pitch_trial",
                        "default":False,
                        "policy_sha256":sha(root/"recomp/native/rex_resolve_pitch_trial.hpp"),
                        "register_restored_after_resolve":True})
    if args.spirv_exponent_fix:
        patches.append({"name":"Enable existing experimental SPIRV-Cross setup path",
                        "scope":"Isolated diagnostic candidate only; reference guard preserved"})
        fetch_source = sdk/"src/graphics/pipeline/shader/spirv_translator_fetch.cpp"
        fetch_object = "src/graphics/CMakeFiles/rexgpu-xenos.dir/pipeline/shader/spirv_translator_fetch.cpp.o"
        protected.update({str(p):sha(p) for p in (fetch_source, build/fetch_object)})
        fetch_commands = [shlex.split(line) for line in commands(fetch_object)
                          if " -c " in line and str(fetch_source) in line]
        if len(fetch_commands) != 1 or link_args.count(fetch_object) != 1:
            raise SystemExit("Ambiguous SPIR-V fetch compile/link context")
        fetch_args = fetch_commands[0]
        if fetch_args[fetch_args.index("-o")+1] != fetch_object or fetch_args[-1] != str(fetch_source):
            raise SystemExit("Unexpected SPIR-V fetch output/source")
        new_fetch_source = output/"source/spirv_translator_fetch.cpp"
        new_fetch_source.write_text(patch_spirv_fetch_exponent(fetch_source.read_text()))
        new_fetch_object = output/"spirv_translator_fetch.cpp.o"
        fetch_args = [str(new_fetch_source) if a==str(fetch_source) else
                      str(new_fetch_object) if a==fetch_object else
                      str(new_fetch_object)+".d" if a==fetch_object+".d" else a for a in fetch_args]
        fetch_args = use_textual_prefix_header(fetch_args, output/"source")
        extra_compiles.append(fetch_args)
        link_args = [str(new_fetch_object) if a==fetch_object else a for a in link_args]
        patches.append({"name":"SPIR-V texture result exponent word 3",
                        "upstream":"https://github.com/shipa-2/rexglue-xerenge/commit/6c94ffdc5d5c5e3fe630d6d887361f4f605b7472",
                        "source_sha256":sha(new_fetch_source),
                        "runtime_control":"blur_spirv_fetch_exponent_fix"})
    link_args.append(str(library))
    link_args.append(str(root/"build-plume/libblur-guest-packet.a"))
    record={"reference_sdk":str(sdk),"compile":compile_args,"extra_compiles":extra_compiles,
            "patches":patches,"link":link_args,"protected_inputs":protected,
            "observer":"CPU register state, active Plume draw submission and resolve routing; game not launched","game_launched":False}
    (output/"build-inputs.json").write_text(json.dumps(record,indent=2)+"\n")
    with (output/"build.log").open("w") as log:
        for command in (compile_args,*extra_compiles,link_args):
            try:subprocess.run(command,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
            except subprocess.CalledProcessError:
                raise SystemExit(f"Candidate build failed; see {output/'build.log'}") from None
    for name in ("blur_x360","librexruntime.dylib","libTracyClient.dylib","blur_x360.toml"):
        path=runtime/name
        if path.is_file():shutil.copy2(path,output/"runtime"/name)
    bridge=root/"build-plume/libblur-plume-bridge.dylib"
    if bridge.is_file():shutil.copy2(bridge,output/"runtime/libblur-plume-bridge.dylib")
    for name,expected in protected.items():
        if sha(name)!=expected:raise SystemExit(f"Protected reference changed: {name}")
    inputs={}
    for argument in link_args:
        if not argument.startswith("-") and Path(argument).suffix in (".o",".a",".dylib"):
            path=Path(argument);path=path if path.is_absolute() else build/path
            if path.is_file():inputs[str(path)]=sha(path)
    proposal={
        "status":"built and tested; not launched",
        "purpose":"validate Plume active frontend, submission and resolve routing; no automatic launch",
        "args":[
            str(output/"runtime/blur_x360"),
            "--gpu_plugin=xenos",
            "--no-fullscreen",
            "--window_width=960",
            "--window_height=540",
            f"--game_data_root={root}/local/x360-feasibility/internal-workspaces-20261025/blur-correspondence-runtime-20261025/game-root",
            f"--user_data_root={output}/run-001/user",
            f"--cache_root={output}/run-001/cache",
            f"--log_file={output}/run-001/runtime.log",
            "--log_level=info"
        ],
        "new_private_run":str(output/"run-001"),
        "fullscreen":False,
        "capture_armed":False,
        "automatic_shutdown":False,
        "game_launched":False
    }
    if args.spirv_exponent_fix:
        proposal["args"] += ["--metal_use_spirvcross=true", "--blur_spirv_fetch_exponent_fix=true"]
    if args.resolve_pitch_trial:
        proposal["args"] += ["--blur_resolve_pitch_trial=true", "--metal_debug_log_resolves_every=60"]
        if not args.spirv_exponent_fix:
            proposal["args"].append("--metal_use_spirvcross=false")
    (output/"launch-proposal.json").write_text(json.dumps(proposal,indent=2)+"\n")
    record.update({"status":"isolated active Plume candidate built; not launched","link_input_sha256":inputs,
                   "artifacts":{str(p.relative_to(output)):sha(p) for p in (output/"runtime").iterdir() if p.is_file()},
                   "source_sha256":sha(new_source),"reference_reverified":True})
    (output/"result.json").write_text(json.dumps(record,indent=2)+"\n")
    print(f"Active Plume candidate built at {output}; protected reference hashes unchanged; no game launched")


if __name__=="__main__":main()
