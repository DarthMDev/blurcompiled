#pragma once
#include "render/guest_draw.hpp"

namespace blur::render::guest {
enum class Toggle { unknown, disabled, enabled };
enum class Topology { unknown, triangles };
enum class IndexSource { unknown, converted_auto_u16, auto_vertex_list };
enum class ColorFormat { unknown, rgba8_unorm, rgba16_float };
enum class TextureFormat { unknown, bc1_unorm_array, rgba8_unorm_array };
enum class BlendFactor { unknown, zero, one, source_alpha, inverse_source_alpha };
enum class BlendOperation { unknown, add };
enum class Cull { unknown, none, back };
enum class Winding { unknown, counter_clockwise, clockwise };
enum class TextureFilter { unknown, linear, basemap };
enum class Address { unknown, wrap, clamp };
// Values must be mapped from the CURRENT SDK draw, not filled from a historical
// replay. Unknowns are rejected. These are new host types, not GPU register IDs.
struct UiState {
    Topology topology=Topology::unknown;
    IndexSource index_source=IndexSource::unknown;
    ColorFormat target_format=ColorFormat::unknown;
    TextureFormat texture_format=TextureFormat::unknown;
    std::uint32_t color_targets=0, color_write_mask=0, vertex_stride=0, vertex_fetch=0;
    Toggle zero_initial_target=Toggle::unknown;
    Toggle depth_test=Toggle::unknown, depth_write=Toggle::unknown, stencil=Toggle::unknown;
    Toggle alpha_to_coverage=Toggle::unknown, logic_operation=Toggle::unknown;
    Toggle blending=Toggle::unknown;
    BlendFactor source_rgb=BlendFactor::unknown, destination_rgb=BlendFactor::unknown;
    BlendFactor source_alpha=BlendFactor::unknown, destination_alpha=BlendFactor::unknown;
    BlendOperation rgb_operation=BlendOperation::unknown, alpha_operation=BlendOperation::unknown;
    Cull cull=Cull::unknown;
    Winding winding=Winding::unknown;
    TextureFilter min_filter=TextureFilter::unknown, mag_filter=TextureFilter::unknown, mip_filter=TextureFilter::unknown;
    Address address_u=Address::unknown, address_v=Address::unknown, address_w=Address::unknown;
    Toggle normalized_coordinates=Toggle::unknown, anisotropy=Toggle::unknown, identity_view=Toggle::unknown;
    float min_lod=0, max_lod=0, lod_bias=0;
    std::uint32_t max_anisotropy=0;
};
struct UiSnapshot {
    UiState state;
    std::uint32_t width=0,height=0,viewport_width=0,viewport_height=0,samples=0;
    std::array<std::uint32_t,4> scissor{};
    std::uint32_t vertex_address=0;
    std::span<const std::uint8_t> vertex_bytes;
    std::span<const std::uint32_t> vertex_spirv,pixel_spirv;
    std::array<std::span<const std::uint8_t>,5> uniforms;
    std::span<const std::uint16_t> indices;
    std::span<const TextureMip> texture_mips;
};
// Synchronously copies borrowed resource views into owned host packet storage.
// GPU-written/private resources need a completed readback before these views
// are supplied. This builder never assumes CPU RAM is current GPU content.
Packet make_ui_packet(const UiSnapshot& snapshot);
struct CompositeTextureInput {
    std::uint32_t width=0,height=0;
    std::span<const std::uint8_t> rgba8_bytes;
    TextureFilter min_filter=TextureFilter::unknown,mag_filter=TextureFilter::unknown,mip_filter=TextureFilter::unknown;
    Address address_u=Address::unknown,address_v=Address::unknown,address_w=Address::unknown;
    Toggle normalized_coordinates=Toggle::unknown,anisotropy=Toggle::unknown;
    float min_lod=0,max_lod=0,lod_bias=0;
    std::uint32_t max_anisotropy=0;
};
struct CompositeSnapshot {
    UiState state;
    std::uint32_t width=0,height=0,viewport_width=0,viewport_height=0,samples=0;
    std::array<std::uint32_t,4> scissor{};
    std::uint32_t vertex_address=0,vertex_count=0;
    std::span<const std::uint8_t> vertex_bytes;
    std::span<const std::uint32_t> vertex_spirv,pixel_spirv;
    std::array<std::span<const std::uint8_t>,5> uniforms;
    std::array<CompositeTextureInput,2> textures;
};
CompositePacket make_composite_packet(const CompositeSnapshot& snapshot);
}
