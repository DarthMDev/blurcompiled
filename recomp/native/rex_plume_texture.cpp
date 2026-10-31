#include "rex_plume_texture.hpp"
#include <rex/graphics/register_file.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace blur::render::guest {
UiState map_rex_ui_texture_state(std::span<const std::uint32_t> words,
    std::uint32_t index,const RexTextureView& view,std::span<const TextureMip> mips,UiState state) {
    namespace gpu=rex::graphics;
    namespace xenos=gpu::xenos;
    if(words.size()!=rex_register_word_count || index>=xenos::kTextureFetchConstantCount)
        throw std::invalid_argument("Invalid current SDK texture fetch extent/index");
    auto require=[](bool ok,const char* message){if(!ok)throw UnsupportedDraw(message);};
    xenos::xe_gpu_texture_fetch_t fetch;
    std::memcpy(&fetch,words.data()+gpu::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0+index*6,sizeof(fetch));
    require(fetch.type==xenos::FetchConstantType::kTexture && fetch.format==xenos::TextureFormat::k_DXT1 &&
            fetch.dimension==xenos::DataDimension::k2DOrStacked && !fetch.stacked,
            "Current texture is not a supported 2D BC1 resource");
    require(fetch.sign_x==xenos::TextureSign::kUnsigned && fetch.sign_y==xenos::TextureSign::kUnsigned &&
            fetch.sign_z==xenos::TextureSign::kUnsigned && fetch.sign_w==xenos::TextureSign::kUnsigned &&
            !fetch.num_format && fetch.swizzle==0x688 && !fetch.exp_adjust && !fetch.force_bc_w_to_max && !fetch.border_size,
            "Current texture component interpretation differs from bounded UI shader contract");
    const auto width=std::uint32_t(fetch.size_2d.width)+1,height=std::uint32_t(fetch.size_2d.height)+1;
    require(width<=2048 && height<=2048,"Texture exceeds bounded UI dimensions");
    const auto last=std::uint32_t(std::bit_width(std::max(width,height))-1);
    // Follow the pinned SDK's physical page mask and effective subresource
    // clamping. Do not treat raw 20-bit fetch pages as native addresses.
    const auto base=std::uint32_t(fetch.base_address)&0x1ffffu;
    auto mip_page=std::uint32_t(fetch.mip_address)&0x1ffffu;
    const auto min_level=mip_page?std::min(std::uint32_t(fetch.mip_min_level),last):0;
    const auto max_level=mip_page?std::max(std::min(std::uint32_t(fetch.mip_max_level),last),min_level):0;
    if(!max_level)mip_page=0;
    require(base && min_level==0 && max_level==last,"UI texture requires its complete base and mip chain");
    require(view.format==RexNativeTextureFormat::bc1_unorm && view.kind==RexNativeTextureKind::array_2d &&
            view.identity_components==Toggle::enabled && view.layers==1 && view.first_mip==0 &&
            view.width==width && view.height==height && view.mip_count==last+1 &&
            view.source_base==base*4096 && view.source_mips==mip_page*4096,
            "Resolved native view does not match the current texture fetch");
    require(mips.size()==view.mip_count,"Decoded mip count differs from the resolved view");
    for(std::size_t i=0;i<mips.size();++i) {
        const auto w=std::max(1u,width>>i),h=std::max(1u,height>>i);
        require(mips[i].width==w && mips[i].height==h &&
                mips[i].bytes.size()==std::size_t((w+3)/4)*((h+3)/4)*8,
                "Decoded BC1 mip extent differs from the resolved view");
    }
    state.texture_format=TextureFormat::bc1_unorm_array;
    state.identity_view=Toggle::enabled;
    return state;
}
UiState map_rex_ui_sampler_state(std::span<const std::uint32_t> words,
    const RexSamplerBinding& binding,UiState state) {
    namespace gpu=rex::graphics;
    namespace xenos=gpu::xenos;
    if(words.size()!=rex_register_word_count || binding.fetch_index>=xenos::kTextureFetchConstantCount)
        throw std::invalid_argument("Invalid current SDK sampler fetch extent/index");
    auto require=[](bool ok,const char* message){if(!ok)throw UnsupportedDraw(message);};
    xenos::xe_gpu_texture_fetch_t fetch;
    std::memcpy(&fetch,words.data()+gpu::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0+binding.fetch_index*6,sizeof(fetch));
    require(fetch.type==xenos::FetchConstantType::kTexture &&
            fetch.dimension==xenos::DataDimension::k2DOrStacked && !fetch.stacked,
            "Current sampler fetch is not an unstacked 2D texture");
    require(!fetch.border_size && !fetch.lod_bias && !fetch.mip_min_level &&
            !fetch.grad_exp_adjust_h && !fetch.grad_exp_adjust_v && !fetch.aniso_bias,
            "Current sampler LOD/gradient/border modifiers are unsupported");
    auto resolve_filter=[](RexSamplerFilter override_mode,xenos::TextureFilter fetch_mode,bool allow_basemap) {
        if(override_mode!=RexSamplerFilter::use_fetch_const) {
            if(!allow_basemap && override_mode==RexSamplerFilter::basemap)
                throw UnsupportedDraw("Invalid non-mip basemap filter override");
            return override_mode;
        }
        switch(fetch_mode) {
            case xenos::TextureFilter::kPoint:return RexSamplerFilter::point;
            case xenos::TextureFilter::kLinear:return RexSamplerFilter::linear;
            case xenos::TextureFilter::kBaseMap:
                if(!allow_basemap)throw UnsupportedDraw("Invalid non-mip basemap fetch filter");
                return RexSamplerFilter::basemap;
            default:throw UnsupportedDraw("Unsupported texture fetch filter mode");
        }
    };
    auto min_filter=resolve_filter(binding.min_filter,fetch.min_filter,false);
    auto mag_filter=resolve_filter(binding.mag_filter,fetch.mag_filter,false);
    auto mip_filter=resolve_filter(binding.mip_filter,fetch.mip_filter,true);
    auto aniso_filter=binding.aniso_filter;
    if(aniso_filter==RexAnisoFilter::use_fetch_const) {
        if(std::uint32_t(fetch.aniso_filter)>std::uint32_t(xenos::AnisoFilter::kMax_16_1))
            throw UnsupportedDraw("Unsupported fetch anisotropic filter mode");
        aniso_filter=static_cast<RexAnisoFilter>(std::uint32_t(fetch.aniso_filter));
    }
    const auto width=std::uint32_t(fetch.size_2d.width)+1,height=std::uint32_t(fetch.size_2d.height)+1;
    const auto last=std::uint32_t(std::bit_width(std::max(width,height))-1);
    const auto mip_page=std::uint32_t(fetch.mip_address)&0x1ffffu;
    const auto min_level=mip_page?std::min(std::uint32_t(fetch.mip_min_level),last):0u;
    const auto max_level=mip_page?std::max(std::min(std::uint32_t(fetch.mip_max_level),last),min_level):0u;
    const bool has_mips=max_level>min_level;
    if(!has_mips)mip_filter=RexSamplerFilter::basemap;
    if(binding.anisotropic_override!=RexAnisoFilter::use_fetch_const &&
       binding.anisotropic_override>RexAnisoFilter::disabled && has_mips &&
       mip_filter!=RexSamplerFilter::basemap &&
       (min_filter==RexSamplerFilter::linear || mag_filter==RexSamplerFilter::linear) &&
       (mip_filter==RexSamplerFilter::point || mip_filter==RexSamplerFilter::linear)) {
        aniso_filter=binding.anisotropic_override;
    }
    aniso_filter=std::min(aniso_filter,RexAnisoFilter::max_16_1);
    if(aniso_filter!=RexAnisoFilter::disabled) {
        min_filter=mag_filter=mip_filter=RexSamplerFilter::linear;
    }
    auto map_filter=[](RexSamplerFilter f) {
        if(f==RexSamplerFilter::linear)return TextureFilter::linear;
        if(f==RexSamplerFilter::basemap)return TextureFilter::basemap;
        return TextureFilter::unknown;
    };
    auto map_address=[](xenos::ClampMode mode) {
        if(mode==xenos::ClampMode::kRepeat)return Address::wrap;
        if(mode==xenos::ClampMode::kClampToEdge)return Address::clamp;
        return Address::unknown;
    };
    state.min_filter=map_filter(min_filter);
    state.mag_filter=map_filter(mag_filter);
    state.mip_filter=map_filter(mip_filter);
    state.address_u=map_address(fetch.clamp_x);
    state.address_v=map_address(fetch.clamp_y);
    // Unstacked 2D textures force clamp_z to kClampToEdge in the pinned SDK.
    state.address_w=map_address(xenos::ClampMode::kClampToEdge);
    state.normalized_coordinates=Toggle::enabled;
    state.anisotropy=aniso_filter==RexAnisoFilter::disabled?Toggle::disabled:Toggle::enabled;
    state.max_anisotropy=aniso_filter==RexAnisoFilter::disabled?1u:(1u<<(std::uint32_t(aniso_filter)-1u));
    state.min_lod=0;
    state.max_lod=mip_filter==RexSamplerFilter::basemap?0u:1000u;
    state.lod_bias=0;
    return state;
}
namespace {
constexpr std::uint32_t align_up(std::uint32_t v,std::uint32_t a) {
    return (v+a-1u)&~(a-1u);
}
constexpr std::uint32_t log2_ceil(std::uint32_t v) {
    return v<=1u?0u:static_cast<std::uint32_t>(std::bit_width(v-1u));
}
void packed_mip_offset_bc1(std::uint32_t width,std::uint32_t height,std::uint32_t mip,
                           std::uint32_t& x_blocks,std::uint32_t& y_blocks) {
    const std::uint32_t log2_w=log2_ceil(width),log2_h=log2_ceil(height);
    const std::uint32_t log2_size=std::min(log2_w,log2_h);
    if(log2_size>4u+mip){x_blocks=0;y_blocks=0;return;}
    const std::uint32_t packed_base=log2_size>4u?(log2_size-4u):0u;
    const std::uint32_t packed_mip=mip-packed_base;
    if(packed_mip<3u) {
        if(log2_w>log2_h){x_blocks=0;y_blocks=(16u>>packed_mip)/4u;}
        else{x_blocks=(16u>>packed_mip)/4u;y_blocks=0;}
    } else {
        if(log2_w>log2_h) {
            x_blocks=((1u<<(log2_w-packed_base))>>(packed_mip-2u))/4u;
            y_blocks=0;
        } else {
            x_blocks=0;
            y_blocks=((1u<<(log2_h-packed_base))>>(packed_mip-2u))/4u;
        }
    }
}
std::uint32_t tiled_offset_2d_bc1(std::uint32_t x,std::uint32_t y,std::uint32_t pitch_blocks) {
    pitch_blocks=align_up(pitch_blocks,32u);
    const std::int32_t ix=static_cast<std::int32_t>(x),iy=static_cast<std::int32_t>(y);
    const std::int32_t macro=((ix>>5)+(iy>>5)*static_cast<std::int32_t>(pitch_blocks>>5))<<(3+7);
    const std::int32_t micro=((ix&7)+((iy&0xE)<<2))<<3;
    const std::int32_t offset=macro+((micro&~0xF)<<1)+(micro&0xF)+((iy&1)<<4);
    return static_cast<std::uint32_t>(
        ((offset&~0x1FF)<<3)+((iy&16)<<7)+((offset&0x1C0)<<2)+
        (((((iy&8)>>2)+(ix>>3))&3)<<6)+(offset&0x3F));
}
std::uint32_t tiled_upper_bound_2d_bc1(std::uint32_t right,std::uint32_t bottom,std::uint32_t pitch_blocks) {
    if(!right || !bottom)return 0;
    return tiled_offset_2d_bc1((right-1u)&~31u,(bottom-1u)&~31u,pitch_blocks)+(0x400u<<3);
}
struct Bc1GuestLayout {
    RexTextureGuestRanges ranges;
    rex::graphics::xenos::Endian endian{};
    std::uint32_t base_row_pitch_bytes=0;
    std::array<std::uint32_t,16> mip_row_pitch_bytes{};
    std::array<std::uint32_t,16> mip_offsets_bytes{};
};
Bc1GuestLayout compute_bc1_guest_layout(std::span<const std::uint32_t> words,std::uint32_t index) {
    namespace gpu=rex::graphics;
    namespace xenos=gpu::xenos;
    if(words.size()!=rex_register_word_count || index>=xenos::kTextureFetchConstantCount)
        throw std::invalid_argument("Invalid current SDK texture fetch extent/index");
    auto require=[](bool ok,const char* message){if(!ok)throw UnsupportedDraw(message);};
    xenos::xe_gpu_texture_fetch_t fetch;
    std::memcpy(&fetch,words.data()+gpu::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0+index*6,sizeof(fetch));
    require(fetch.type==xenos::FetchConstantType::kTexture && fetch.format==xenos::TextureFormat::k_DXT1 &&
            fetch.dimension==xenos::DataDimension::k2DOrStacked && !fetch.stacked,
            "Current texture is not a supported 2D BC1 resource");
    require(fetch.sign_x==xenos::TextureSign::kUnsigned && fetch.sign_y==xenos::TextureSign::kUnsigned &&
            fetch.sign_z==xenos::TextureSign::kUnsigned && fetch.sign_w==xenos::TextureSign::kUnsigned &&
            !fetch.num_format && fetch.swizzle==0x688 && !fetch.exp_adjust && !fetch.force_bc_w_to_max && !fetch.border_size,
            "Current texture component interpretation differs from bounded UI shader contract");
    const std::uint32_t width=std::uint32_t(fetch.size_2d.width)+1u;
    const std::uint32_t height=std::uint32_t(fetch.size_2d.height)+1u;
    require(width<=2048u && height<=2048u,"Texture exceeds bounded UI dimensions");
    const std::uint32_t last=std::uint32_t(std::bit_width(std::max(width,height))-1);
    const std::uint32_t base_page=std::uint32_t(fetch.base_address)&0x1ffffu;
    std::uint32_t mip_page=std::uint32_t(fetch.mip_address)&0x1ffffu;
    const std::uint32_t min_level=mip_page?std::min(std::uint32_t(fetch.mip_min_level),last):0u;
    const std::uint32_t max_level=mip_page?std::max(std::min(std::uint32_t(fetch.mip_max_level),last),min_level):0u;
    if(!max_level)mip_page=0;
    require(base_page && min_level==0u && max_level==last,"UI texture requires its complete base and mip chain");
    const std::uint32_t log2_min=log2_ceil(std::min(width,height));
    const std::uint32_t packed_level=fetch.packed_mips?(log2_min>4u?(log2_min-4u):0u):0xffffffffu;
    const std::uint32_t max_stored_level=std::min(max_level,packed_level);
    const std::uint32_t loop_last=packed_level==0u?std::uint32_t(max_level!=0u):max_stored_level;
    Bc1GuestLayout out{};
    out.endian=fetch.endianness;
    out.ranges.width=width;out.ranges.height=height;
    out.ranges.mip_count=last+1u;out.ranges.packed_level=packed_level;
    out.ranges.tiled=bool(fetch.tiled);
    out.ranges.base_address=base_page*4096u;
    out.ranges.mips_address=mip_page*4096u;
    std::uint32_t mip_offset=0;
    for(std::uint32_t loop_level=0;loop_level<=loop_last;++loop_level) {
        const bool is_base=loop_level==0u;
        const std::uint32_t level=packed_level==0u?0u:loop_level;
        const std::uint32_t row_texels=is_base?(std::uint32_t(fetch.pitch)<<5):
            std::max(std::bit_ceil(width)>>level,1u);
        const std::uint32_t slice_rows=is_base?height:std::max(std::bit_ceil(height)>>level,1u);
        require(row_texels>0u,"Texture base pitch must be non-zero");
        const std::uint32_t row_blocks_aligned=align_up((row_texels+3u)/4u,32u);
        std::uint32_t row_pitch_bytes=row_blocks_aligned*8u;
        if(!fetch.tiled && !is_base)row_pitch_bytes=align_up(row_pitch_bytes,256u);
        const std::uint32_t slice_block_rows=align_up((slice_rows+3u)/4u,32u);
        const std::uint32_t slice_stride_bytes=align_up(row_pitch_bytes*slice_block_rows,4096u);
        std::uint32_t x_blocks=0,y_blocks=0;
        if(level==packed_level) {
            const std::uint32_t sub_last=is_base?0u:max_level;
            for(std::uint32_t sub=packed_level;sub<=sub_last;++sub) {
                std::uint32_t px=0,py=0;
                packed_mip_offset_bc1(width,height,sub,px,py);
                x_blocks=std::max(x_blocks,px+(std::max(width>>sub,1u)+3u)/4u);
                y_blocks=std::max(y_blocks,py+(std::max(height>>sub,1u)+3u)/4u);
            }
        } else {
            x_blocks=(std::max(width>>level,1u)+3u)/4u;
            y_blocks=(std::max(height>>level,1u)+3u)/4u;
        }
        const std::uint32_t data_extent=fetch.tiled?
            tiled_upper_bound_2d_bc1(x_blocks,y_blocks,row_blocks_aligned):
            (row_pitch_bytes*(y_blocks-1u)+8u*x_blocks);
        if(is_base) {
            out.base_row_pitch_bytes=row_pitch_bytes;
            out.ranges.base_bytes=data_extent;
        } else {
            out.mip_row_pitch_bytes[level]=row_pitch_bytes;
            out.mip_offsets_bytes[level]=mip_offset;
            out.ranges.mips_bytes=std::max(out.ranges.mips_bytes,mip_offset+data_extent);
            mip_offset+=slice_stride_bytes;
        }
    }
    require(out.ranges.base_bytes>0u &&
            std::uint64_t(out.ranges.base_address)+out.ranges.base_bytes<=(512ull<<20) &&
            std::uint64_t(out.ranges.mips_address)+out.ranges.mips_bytes<=(512ull<<20),
            "Current texture resource exceeds bounded physical memory");
    return out;
}
}
RexTextureGuestRanges rex_ui_texture_guest_ranges(
    std::span<const std::uint32_t> words,std::uint32_t index) {
    return compute_bc1_guest_layout(words,index).ranges;
}
std::vector<TextureMip> decode_rex_ui_bc1_mips(
    std::span<const std::uint32_t> words,std::uint32_t index,
    const RexGuestTextureData& guest_data,
    std::vector<std::vector<std::uint8_t>>& storage_out) {
    namespace xenos=rex::graphics::xenos;
    const auto layout=compute_bc1_guest_layout(words,index);
    const auto& r=layout.ranges;
    if(guest_data.base_address!=r.base_address || guest_data.base_bytes.size()!=r.base_bytes ||
       guest_data.mips_address!=r.mips_address || guest_data.mips_bytes.size()!=r.mips_bytes)
        throw UnsupportedDraw("Resolved guest texture byte ranges do not match the current fetch layout");
    storage_out.clear();
    storage_out.resize(r.mip_count);
    std::vector<TextureMip> views;
    views.reserve(r.mip_count);
    for(std::uint32_t level=0;level<r.mip_count;++level) {
        const std::uint32_t w=std::max(1u,r.width>>level),h=std::max(1u,r.height>>level);
        const std::uint32_t w_blocks=(w+3u)/4u,h_blocks=(h+3u)/4u;
        const std::uint32_t stored_level=std::min(level,r.packed_level);
        const bool is_base=stored_level==0u && (r.packed_level!=0u || level==0u);
        const std::uint32_t row_pitch_bytes=is_base?layout.base_row_pitch_bytes:layout.mip_row_pitch_bytes[stored_level];
        const std::uint32_t row_pitch_blocks=row_pitch_bytes/8u;
        const std::uint32_t level_offset=is_base?0u:layout.mip_offsets_bytes[stored_level];
        const auto src_span=is_base?guest_data.base_bytes:guest_data.mips_bytes;
        std::uint32_t px=0,py=0;
        if(level>=r.packed_level)packed_mip_offset_bc1(r.width,r.height,level,px,py);
        auto& dst=storage_out[level];
        dst.resize(std::size_t(w_blocks)*h_blocks*8u);
        for(std::uint32_t by=0;by<h_blocks;++by) {
            for(std::uint32_t bx=0;bx<w_blocks;++bx) {
                const std::uint32_t gx=px+bx,gy=py+by;
                const std::uint32_t block_offset=r.tiled?
                    tiled_offset_2d_bc1(gx,gy,row_pitch_blocks):
                    (gy*row_pitch_bytes+gx*8u);
                const std::size_t src_pos=std::size_t(level_offset)+block_offset;
                if(src_pos+8u>src_span.size())
                    throw UnsupportedDraw("Guest BC1 block offset exceeds synchronized texture range");
                std::uint32_t w0=0,w1=0;
                std::memcpy(&w0,src_span.data()+src_pos,4);
                std::memcpy(&w1,src_span.data()+src_pos+4,4);
                w0=xenos::GpuSwap(w0,layout.endian);
                w1=xenos::GpuSwap(w1,layout.endian);
                const std::size_t dst_pos=(std::size_t(by)*w_blocks+bx)*8u;
                std::memcpy(dst.data()+dst_pos,&w0,4);
                std::memcpy(dst.data()+dst_pos+4,&w1,4);
            }
        }
        views.push_back(TextureMip{w,h,dst});
    }
    return views;
}
UiState map_rex_rgba8_texture_state(std::span<const std::uint32_t> words,
    std::uint32_t index,const RexTextureView& view,
    std::span<const std::uint8_t> rgba8_bytes,UiState state) {
    namespace gpu=rex::graphics;
    namespace xenos=gpu::xenos;
    if(words.size()!=rex_register_word_count || index>=xenos::kTextureFetchConstantCount)
        throw std::invalid_argument("Invalid current SDK RGBA8 texture fetch extent/index");
    auto require=[](bool ok,const char* message){if(!ok)throw UnsupportedDraw(message);};
    xenos::xe_gpu_texture_fetch_t fetch;
    std::memcpy(&fetch,words.data()+gpu::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0+index*6,sizeof(fetch));
    require(fetch.type==xenos::FetchConstantType::kTexture &&
            fetch.format==xenos::TextureFormat::k_8_8_8_8 &&
            fetch.dimension==xenos::DataDimension::k2DOrStacked && !fetch.stacked,
            "Current texture is not a supported 2D RGBA8 resource");
    require(fetch.sign_x==xenos::TextureSign::kUnsigned && fetch.sign_y==xenos::TextureSign::kUnsigned &&
            fetch.sign_z==xenos::TextureSign::kUnsigned && fetch.sign_w==xenos::TextureSign::kUnsigned &&
            !fetch.num_format && !fetch.exp_adjust && !fetch.force_bc_w_to_max && !fetch.border_size,
            "Current RGBA8 texture component interpretation is unsupported");
    const auto width=std::uint32_t(fetch.size_2d.width)+1u;
    const auto height=std::uint32_t(fetch.size_2d.height)+1u;
    require(width<=2048u && height<=2048u,"RGBA8 texture exceeds bounded dimensions");
    const auto last=std::uint32_t(std::bit_width(std::max(width,height))-1);
    const auto base=std::uint32_t(fetch.base_address)&0x1ffffu;
    auto mip_page=std::uint32_t(fetch.mip_address)&0x1ffffu;
    const auto min_level=mip_page?std::min(std::uint32_t(fetch.mip_min_level),last):0u;
    const auto max_level=mip_page?std::max(std::min(std::uint32_t(fetch.mip_max_level),last),min_level):0u;
    if(!max_level)mip_page=0;
    require(base && min_level==0u && max_level==0u && mip_page==0u,
            "Current RGBA8 texture mapper requires a base-only 2D resource");
    // Host format swizzle for k_8_8_8_8 is identity (0x688 = RGBA), so
    // GuestToHostSwizzle reduces to normalizing any 6/7 constant swizzle bits.
    std::uint32_t expected_swizzle=0;
    for(std::uint32_t c=0;c<4;++c) {
        std::uint32_t comp=(std::uint32_t(fetch.swizzle)>>(3*c))&0x7u;
        if(comp>=4u)comp&=0x5u;
        expected_swizzle|=comp<<(3*c);
    }
    const Toggle expected_identity=(expected_swizzle==0x688u)?Toggle::enabled:Toggle::disabled;
    require(view.format==RexNativeTextureFormat::rgba8_unorm &&
            view.kind==RexNativeTextureKind::array_2d &&
            view.identity_components==expected_identity &&
            view.host_swizzle==expected_swizzle &&
            view.layers==1u && view.first_mip==0u && view.mip_count==1u &&
            view.width==width && view.height==height &&
            view.source_base==base*4096u && view.source_mips==0u,
            "Resolved RGBA8 native view does not match the current texture fetch");
    require(rgba8_bytes.size()==std::size_t(width)*height*4u,
            "Decoded RGBA8 payload extent differs from the resolved view");
    state.texture_format=TextureFormat::rgba8_unorm_array;
    state.identity_view=expected_identity;
    return state;
}
std::vector<std::uint8_t> swizzle_rex_rgba8_pixels(
    std::span<const std::uint8_t> rgba8_bytes,std::uint32_t host_swizzle) {
    if(rgba8_bytes.size()%4u!=0 || (host_swizzle&~0xfffu)!=0)
        throw std::invalid_argument("Invalid RGBA8 byte span or 12-bit Xenos swizzle");
    std::array<std::uint8_t,4> selectors{};
    for(std::uint32_t c=0;c<4;++c) {
        const std::uint32_t sel=(host_swizzle>>(3*c))&0x7u;
        if(sel>5u)throw std::invalid_argument("Unsupported Xenos swizzle component selector");
        selectors[c]=static_cast<std::uint8_t>(sel);
    }
    std::vector<std::uint8_t> out(rgba8_bytes.size());
    if(host_swizzle==0x688u) {
        std::copy(rgba8_bytes.begin(),rgba8_bytes.end(),out.begin());
        return out;
    }
    for(std::size_t i=0;i<rgba8_bytes.size();i+=4) {
        const std::array<std::uint8_t,6> src{
            rgba8_bytes[i],rgba8_bytes[i+1],rgba8_bytes[i+2],rgba8_bytes[i+3],0u,255u};
        for(std::size_t c=0;c<4;++c)out[i+c]=src[selectors[c]];
    }
    return out;
}
namespace {
std::uint32_t tiled_offset_2d_32bpp(std::uint32_t x,std::uint32_t y,std::uint32_t pitch_pixels) {
    pitch_pixels=align_up(pitch_pixels,32u);
    const std::int32_t ix=static_cast<std::int32_t>(x),iy=static_cast<std::int32_t>(y);
    const std::int32_t macro=((ix>>5)+(iy>>5)*static_cast<std::int32_t>(pitch_pixels>>5))<<9;
    const std::int32_t micro=((ix&7)+((iy&14)<<2))<<2;
    const std::int32_t offset=macro+((micro&~15)<<1)+(micro&15)+((iy&1)<<4);
    return static_cast<std::uint32_t>(
        ((offset&~511)<<3)+((iy&16)<<7)+((offset&448)<<2)+
        (((((iy&8)>>2)+(ix>>3))&3)<<6)+(offset&63));
}
float half_to_float(std::uint16_t h) {
    const std::uint32_t sign=(std::uint32_t(h>>15)&1u)<<31;
    const std::uint32_t exp=(h>>10)&0x1fu;
    const std::uint32_t mant=h&0x3ffu;
    if(exp==0u) {
        if(mant==0u)return std::bit_cast<float>(sign);
        const float mag=std::ldexp(float(mant)*(1.0f/1024.0f),-14);
        return sign?-mag:mag;
    }
    if(exp==31u) {
        const std::uint32_t bits=sign|0x7f800000u|(mant<<13);
        return std::bit_cast<float>(bits);
    }
    const std::uint32_t bits=sign|((exp+112u)<<23)|(mant<<13);
    return std::bit_cast<float>(bits);
}
std::uint32_t float32_to_7e3(float value) {
    if(!std::isfinite(value) || value<=0.0f)return 0u;
    const float clamped=std::min(value,31.875f);
    const std::uint32_t f32=std::bit_cast<std::uint32_t>(clamped);
    std::uint32_t biased=0;
    if(f32<0x3e800000u) {
        const std::uint32_t exp=f32>>23u;
        const std::uint32_t shift=std::min(125u-exp,24u);
        const std::uint32_t mant=(f32&0x7fffffu)|0x800000u;
        biased=mant>>shift;
    } else {
        biased=f32+0xc2000000u;
    }
    const std::uint32_t round_bit=(biased>>16u)&1u;
    const std::uint32_t f10=biased+0x7fffu+round_bit;
    return (f10>>16u)&0x3ffu;
}
std::uint8_t unpack_7e3_to_unorm8(std::uint32_t f10) {
    f10&=0x3ffu;
    if(!f10)return 0u;
    std::uint32_t mant=f10&0x7fu;
    std::uint32_t exp=f10>>7u;
    float f32=0.0f;
    if(exp==0u) {
        f32=float(mant)*(1.0f/512.0f);
    } else {
        const std::uint32_t bits=((exp+124u)<<23u)|(mant<<16u);
        f32=std::bit_cast<float>(bits);
    }
    const float clamped=std::min(std::max(f32,0.0f),1.0f);
    return static_cast<std::uint8_t>(clamped*255.0f+0.5f);
}
}
void resolve_rex_rgba8_msaa4_strip(const RexMsaa4ResolveStrip& strip,
    std::uint32_t dest_height,std::span<std::uint8_t> dest_rgba8_out) {
    auto require=[](bool ok,const char* message){if(!ok)throw UnsupportedDraw(message);};
    if(!strip.width || !strip.height || strip.source_height<strip.height ||
       !dest_height || strip.dest_row_offset+strip.height>dest_height)
        throw std::invalid_argument("Invalid 4x MSAA RGBA8 strip dimensions");
    if(strip.msaa4_rgba8_bytes.size()!=std::size_t(strip.width)*strip.source_height*16u ||
       dest_rgba8_out.size()!=std::size_t(strip.width)*dest_height*4u)
        throw std::invalid_argument("Invalid 4x MSAA RGBA8 strip byte span extent");
    const std::uint32_t endian=strip.rb_copy_dest_info&0x7u;
    const std::uint32_t array=(strip.rb_copy_dest_info>>3u)&0xfu;
    const std::uint32_t format=(strip.rb_copy_dest_info>>7u)&0x3fu;
    const std::uint32_t exp_bias=(strip.rb_copy_dest_info>>16u)&0x3fu;
    const std::uint32_t swap=(strip.rb_copy_dest_info>>24u)&0x1u;
    require(endian==2u && array==0u && format==6u && exp_bias==0u && swap==1u &&
            strip.sample_select==6u,
            "Unsupported 4x MSAA RGBA8 resolve RB_COPY_DEST_INFO or sample_select");
    for(std::uint32_t y=0;y<strip.height;++y) {
        const std::size_t src_row=std::size_t(y)*strip.width*16u;
        const std::size_t dst_row=std::size_t(strip.dest_row_offset+y)*strip.width*4u;
        for(std::uint32_t x=0;x<strip.width;++x) {
            const std::uint8_t* s=strip.msaa4_rgba8_bytes.data()+src_row+std::size_t(x)*16u;
            std::array<std::uint8_t,4> avg{};
            for(std::size_t c=0;c<4;++c) {
                const std::uint32_t sum=std::uint32_t(s[c])+s[4+c]+s[8+c]+s[12+c];
                avg[c]=static_cast<std::uint8_t>((sum+2u)>>2u);
            }
            std::uint8_t* d=dest_rgba8_out.data()+dst_row+std::size_t(x)*4u;
            d[0]=avg[2];
            d[1]=avg[1];
            d[2]=avg[0];
            d[3]=avg[3];
        }
    }
}
RexHdr7e3ResolveOutput resolve_rex_rgba16f_to_7e3_rgba8(
    const RexHdr7e3ResolveSpec& spec,std::span<const std::uint16_t> rgba16f_halfs) {
    auto require=[](bool ok,const char* message){if(!ok)throw UnsupportedDraw(message);};
    if(!spec.width || !spec.height ||
       rgba16f_halfs.size()!=std::size_t(spec.width)*spec.height*4u)
        throw std::invalid_argument("Invalid RGBA16F resolve dimensions or half-float span");
    const std::uint32_t endian=spec.rb_copy_dest_info&0x7u;
    const std::uint32_t array=(spec.rb_copy_dest_info>>3u)&0xfu;
    const std::uint32_t format=(spec.rb_copy_dest_info>>7u)&0x3fu;
    const std::uint32_t exp_bias=(spec.rb_copy_dest_info>>16u)&0x3fu;
    const std::uint32_t swap=(spec.rb_copy_dest_info>>24u)&0x1u;
    require(endian==0u && array==0u && format==6u && exp_bias==0u && swap==1u,
            "Unsupported RGBA16F-to-7e3 resolve RB_COPY_DEST_INFO");
    const std::uint32_t pitch=spec.dest_pitch_pixels?spec.dest_pitch_pixels:spec.width;
    require(pitch>=spec.width && (pitch%32u)==0u,
            "Unsupported RGBA16F resolve destination pitch");
    std::array<std::uint8_t,1024> lut_rgb{};
    for(std::uint32_t code=0;code<1024u;++code)
        lut_rgb[code]=unpack_7e3_to_unorm8(code);
    constexpr std::array<std::uint8_t,4> lut_a{0u,85u,170u,255u};
    RexHdr7e3ResolveOutput out{};
    out.linear_rgba8.resize(std::size_t(spec.width)*spec.height*4u);
    if(spec.write_tiled_destination) {
        const std::uint32_t padded_height=align_up(spec.height,32u);
        out.tiled_destination.resize(std::size_t(pitch)*padded_height*4u,0u);
    }
    for(std::uint32_t y=0;y<spec.height;++y) {
        for(std::uint32_t x=0;x<spec.width;++x) {
            const std::size_t idx=std::size_t(y)*spec.width+x;
            const float r=half_to_float(rgba16f_halfs[idx*4u+0u]);
            const float g=half_to_float(rgba16f_halfs[idx*4u+1u]);
            const float b=half_to_float(rgba16f_halfs[idx*4u+2u]);
            const float a=half_to_float(rgba16f_halfs[idx*4u+3u]);
            const std::uint8_t r8=lut_rgb[float32_to_7e3(r)];
            const std::uint8_t g8=lut_rgb[float32_to_7e3(g)];
            const std::uint8_t b8=lut_rgb[float32_to_7e3(b)];
            const float a_clamped=(!std::isfinite(a) || a<=0.0f)?0.0f:std::min(a,1.0f);
            const std::uint8_t a8=lut_a[static_cast<std::uint32_t>(a_clamped*3.0f+0.5f)&3u];
            const std::size_t lin_off=idx*4u;
            out.linear_rgba8[lin_off+0u]=b8;
            out.linear_rgba8[lin_off+1u]=g8;
            out.linear_rgba8[lin_off+2u]=r8;
            out.linear_rgba8[lin_off+3u]=a8;
            if(spec.write_tiled_destination) {
                const std::uint32_t tiled_off=tiled_offset_2d_32bpp(x,y,pitch);
                std::memcpy(out.tiled_destination.data()+tiled_off,out.linear_rgba8.data()+lin_off,4u);
            }
        }
    }
    return out;
}
}
