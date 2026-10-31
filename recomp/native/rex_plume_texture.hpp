#pragma once
#include "rex_plume_state.hpp"

namespace blur::render::guest {
enum class RexNativeTextureFormat { unknown,bc1_unorm,rgba8_unorm };
enum class RexNativeTextureKind { unknown,array_2d };
// New host facts obtained from the CURRENT resolved native texture/view.
// Source addresses are physical byte offsets, not host handles or pointers.
// Identity and format must describe the actual view, including sRGB/swizzles.
struct RexTextureView {
    RexNativeTextureFormat format=RexNativeTextureFormat::unknown;
    RexNativeTextureKind kind=RexNativeTextureKind::unknown;
    Toggle identity_components=Toggle::unknown;
    std::uint32_t host_swizzle=0x688;
    std::uint32_t width=0,height=0,layers=0,first_mip=0,mip_count=0;
    std::uint32_t source_base=0,source_mips=0;
};
enum class RexSamplerFilter { point,linear,basemap,use_fetch_const };
enum class RexAnisoFilter { disabled=0,max_1_1=1,max_2_1=2,max_4_1=3,max_8_1=4,max_16_1=5,use_fetch_const=7 };
struct RexSamplerBinding {
    std::uint32_t fetch_index=0;
    RexSamplerFilter mag_filter=RexSamplerFilter::use_fetch_const;
    RexSamplerFilter min_filter=RexSamplerFilter::use_fetch_const;
    RexSamplerFilter mip_filter=RexSamplerFilter::use_fetch_const;
    RexAnisoFilter aniso_filter=RexAnisoFilter::use_fetch_const;
    RexAnisoFilter anisotropic_override=RexAnisoFilter::max_4_1;
};
// Checks the supported unsigned 2D BC1 fetch against a resolved one-layer
// native array view and complete decoded mip chain. Does not decode guest
// tiling, read memory, establish synchronization or map sampler overrides.
UiState map_rex_ui_texture_state(std::span<const std::uint32_t> registers,
    std::uint32_t fetch_index,const RexTextureView& view,
    std::span<const TextureMip> decoded_mips,UiState state);
// Maps the SDK's effective sampler parameters (including dimension-clamped
// addressing and anisotropic override) from the current fetch descriptor and
// shader sampler binding. Does not validate or touch texture view payloads.
UiState map_rex_ui_sampler_state(std::span<const std::uint32_t> registers,
    const RexSamplerBinding& binding,UiState state);
struct RexTextureGuestRanges {
    std::uint32_t base_address=0,base_bytes=0;
    std::uint32_t mips_address=0,mips_bytes=0;
    std::uint32_t width=0,height=0,mip_count=0,packed_level=0;
    bool tiled=false;
};
struct RexGuestTextureData {
    std::uint32_t base_address=0,mips_address=0;
    std::span<const std::uint8_t> base_bytes,mips_bytes;
};
// Computes physical guest base/mip byte ranges and layout metadata for a
// supported 2D BC1 texture fetch using the pinned SDK layout rules.
RexTextureGuestRanges rex_ui_texture_guest_ranges(
    std::span<const std::uint32_t> registers,std::uint32_t fetch_index);
// Untiles, unpacks packed mip tails and endian-swaps guest 2D BC1 base/mip
// spans into owned linear mip storage.
std::vector<TextureMip> decode_rex_ui_bc1_mips(
    std::span<const std::uint32_t> registers,std::uint32_t fetch_index,
    const RexGuestTextureData& guest_data,
    std::vector<std::vector<std::uint8_t>>& storage_out);
// Maps a base-only unsigned 2D RGBA8 fetch against a resolved native view and
// base payload, validating dimensions, physical base address, and host swizzle.
UiState map_rex_rgba8_texture_state(std::span<const std::uint32_t> registers,
    std::uint32_t fetch_index,const RexTextureView& view,
    std::span<const std::uint8_t> rgba8_bytes,UiState state);
// Applies the pinned SDK's 12-bit Xenos component swizzle (R=0,G=1,B=2,A=3,0=4,1=5)
// to linear RGBA8 texel bytes so the resulting host texture can be bound with an
// identity view without requiring a separate swizzled MetalTextureView.
std::vector<std::uint8_t> swizzle_rex_rgba8_pixels(
    std::span<const std::uint8_t> rgba8_bytes,std::uint32_t host_swizzle);
struct RexMsaa4ResolveStrip {
    std::uint32_t rb_copy_dest_info=0;
    std::uint32_t sample_select=0;
    std::uint32_t width=0,height=0,source_height=0;
    std::uint32_t dest_row_offset=0;
    std::span<const std::uint8_t> msaa4_rgba8_bytes;
};
// Resolves a 4x MSAA RGBA8 strip (sample_select=6 / all-four average, endian=2,
// dest_format=k_8_8_8_8, swap_rb=1) into a destination RGBA8 image buffer.
void resolve_rex_rgba8_msaa4_strip(const RexMsaa4ResolveStrip& strip,
    std::uint32_t dest_height,std::span<std::uint8_t> dest_rgba8_out);
struct RexHdr7e3ResolveSpec {
    std::uint32_t width=0,height=0;
    std::uint32_t rb_copy_dest_info=0;
    std::uint32_t dest_pitch_pixels=0;
    bool write_tiled_destination=false;
};
struct RexHdr7e3ResolveOutput {
    std::vector<std::uint8_t> linear_rgba8;
    std::vector<std::uint8_t> tiled_destination;
};
// Resolves an RGBA16F render target crop through Xenos k_2_10_10_10_FLOAT (7e3)
// EDRAM packing and k_8_8_8_8 destination conversion (swap_rb=1, exp_bias=0)
// into linear swap RGBA8 bytes and optional 32bpp 2D tiled guest memory bytes.
RexHdr7e3ResolveOutput resolve_rex_rgba16f_to_7e3_rgba8(
    const RexHdr7e3ResolveSpec& spec,std::span<const std::uint16_t> rgba16f_halfs);
}
