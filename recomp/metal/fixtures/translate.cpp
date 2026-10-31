// SPDX-License-Identifier: GPL-3.0-or-later
// CPU-only fixture translation, compiled inside the pinned GPU plugin so no
// hidden SDK C++ ABI needs to be exported or duplicated by the host.
#include <rex/graphics/pipeline/shader/spirv.h>
#include <rex/graphics/pipeline/shader/spirv_translator.h>
#include <rex/string/buffer.h>
#include <bit>
#include <cstdint>
#include <fstream>
extern "C" __attribute__((visibility("default"))) int blurcompiled_translate_fixture(
    uint32_t pixel, uint64_t hash, const uint32_t* words, uint32_t count,
    uint64_t modification, uint32_t msaa_flags, const char* output) {
  if (pixel > 1 || !words || count > 4096 || !count || !output) return 1;
  try {
    using namespace rex::graphics;
    // Fixed capture profile; never derive fixture bytes from the builder's GPU.
    SpirvShaderTranslator::Features features(true);
    features.cull_distance = false;
    features.denorm_flush_to_zero_float32 = false;
    SpirvShaderTranslator translator(features, msaa_flags & 1, msaa_flags & 2, msaa_flags & 4);
    SpirvShader shader(pixel ? xenos::ShaderType::kPixel : xenos::ShaderType::kVertex,
                       hash, words, count, std::endian::native);
    rex::string::StringBuffer disassembly;
    shader.AnalyzeUcode(disassembly);
    auto translation = shader.GetOrCreateTranslation(modification);
    if (!translator.TranslateAnalyzedShader(*translation)) return 2;
    auto& bytes = translation->translated_binary();
    std::ofstream file(output, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return file ? 0 : 3;
  } catch (...) { return 4; }
}
