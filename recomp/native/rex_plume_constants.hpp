#pragma once
#include "rex_plume_state.hpp"

namespace blur::render::guest {
// New host input copied from the CURRENT analyzed SDK shader, not a cached
// capture. A shader pair must pass supported_ui_shader_pair before submission.
struct RexFloatConstantMap {
    std::array<std::uint64_t,4> bitmap{};
    std::uint32_t count=0;
    bool dynamic_addressing=false;
};
using UiConstantBlocks=std::array<std::vector<std::uint8_t>,5>;
// Complete the two fields omitted by the pinned Metal SPIR-V updater.
// Layout offsets belong to this pinned SDK constant buffer, not guest memory.
void complete_rex_spirv_index_limits(std::span<const std::uint32_t> registers,
                                     std::span<std::uint8_t> system_constants);

// The selected UI layout (4 VS / 6 PS float4s) is default; other bounded passes
// (such as the composite pass with 0 VS / 0 PS float4s) pass explicit counts.
// System constants must come from the current completed CPU system-constant update;
// they cannot be reconstructed from raw registers alone. No GPU reads occur.
UiConstantBlocks pack_rex_ui_constants(std::span<const std::uint32_t> registers,
    const RexFloatConstantMap& vertex, const RexFloatConstantMap& pixel,
    std::span<const std::uint8_t> system_constants,
    unsigned required_vs_floats = 4, unsigned required_ps_floats = 6);
}
