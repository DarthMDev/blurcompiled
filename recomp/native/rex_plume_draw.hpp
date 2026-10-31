#pragma once
#include "rex_plume_constants.hpp"
#include "rex_plume_texture.hpp"

namespace blur::render::guest {
struct RexVertexRange {
    std::uint32_t address=0,bytes=0;
};
// Physical byte offsets only. The fetch descriptor does not prove GPU/CPU
// coherency, vertex stride, or ownership of the supplied resource bytes.
RexVertexRange rex_vertex_range(std::span<const std::uint32_t> registers,
                                std::uint32_t fetch_index);
struct RexPrimitiveDraw {
    UiState state;
    std::uint32_t guest_vertex_count=0;
    std::vector<std::uint16_t> converted_indices;
};
// Maps VGT draw initiator and index bounds into the SDK's deterministic
// auto-indexed quad-list triangle conversion. Does not read guest index RAM.
RexPrimitiveDraw map_rex_ui_primitive_draw(std::span<const std::uint32_t> registers,
                                           UiState state);
struct RexRenderTargetView {
    ColorFormat format=ColorFormat::unknown;
    Toggle zero_initial_target=Toggle::unknown;
    std::uint32_t width=0,height=0,samples=0,edram_base=0;
};
// Maps active EDRAM surface/color registers, viewport and clamped scissor, and
// checks them against the current resolved host render target view.
UiSnapshot map_rex_ui_target_state(std::span<const std::uint32_t> registers,
                                   const RexRenderTargetView& target_view,
                                   UiSnapshot snapshot);
struct RexUiShaderState {
    std::span<const std::uint32_t> registers;
    std::span<const std::uint8_t> vertex_spirv,pixel_spirv,system_constants;
    RexFloatConstantMap vertex_floats,pixel_floats;
};
// Compose a native packet from current SDK state and separately resolved,
// synchronized resource views. Resource shader/uniform/index spans and target
// dimensions must be empty; they are built exclusively from shader_state and
// verified view descriptors. No GPU access or submission occurs.
Packet make_rex_ui_packet(const RexUiShaderState& shader_state,UiSnapshot resources,
                         const RexTextureView& texture_view={},
                         const RexRenderTargetView& target_view={},
                         const RexGuestTextureData& guest_texture={});
struct RexCompositeResources {
    std::uint32_t vertex_address=0;
    std::span<const std::uint8_t> vertex_bytes;
    std::array<std::span<const std::uint8_t>,2> texture_rgba8_bytes; // fetch 0 (1280x720) and fetch 1 (320x180)
};
CompositePacket make_rex_composite_packet(const RexUiShaderState& shader_state,
                                          const RexCompositeResources& resources,
                                          const std::array<RexTextureView,2>& texture_views,
                                          const RexRenderTargetView& target_view);
}
