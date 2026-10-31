#include "rex_plume_draw.hpp"
#include <rex/graphics/register_file.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace blur::render::guest {
RexVertexRange rex_vertex_range(std::span<const std::uint32_t> words,std::uint32_t index) {
    namespace gpu=rex::graphics;
    if(words.size()!=rex_register_word_count || index>=gpu::xenos::kVertexFetchConstantCount)
        throw std::invalid_argument("Invalid current SDK vertex fetch extent/index");
    gpu::xenos::xe_gpu_vertex_fetch_t fetch;
    std::memcpy(&fetch,words.data()+gpu::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0+index*2,sizeof(fetch));
    if(fetch.type!=gpu::xenos::FetchConstantType::kVertex)
        throw UnsupportedDraw("Current fetch descriptor is not a valid vertex resource");
    const std::uint64_t address=std::uint64_t(fetch.address)*4,bytes=std::uint64_t(fetch.size)*4;
    if(!bytes || bytes>(1u<<20) || address+bytes>(512ull<<20))
        throw UnsupportedDraw("Current vertex resource exceeds bounded physical memory");
    return {std::uint32_t(address),std::uint32_t(bytes)};
}
RexPrimitiveDraw map_rex_ui_primitive_draw(std::span<const std::uint32_t> words,UiState state) {
    namespace gpu=rex::graphics;
    namespace reg=gpu::reg;
    namespace xenos=gpu::xenos;
    if(words.size()!=rex_register_word_count)
        throw std::invalid_argument("Invalid current SDK register extent");
    auto require=[](bool ok,const char* message){if(!ok)throw UnsupportedDraw(message);};
    reg::VGT_DRAW_INITIATOR initiator{words[gpu::XE_GPU_REG_VGT_DRAW_INITIATOR]};
    reg::VGT_OUTPUT_PATH_CNTL output_path{words[gpu::XE_GPU_REG_VGT_OUTPUT_PATH_CNTL]};
    reg::VGT_INDX_OFFSET indx_offset{words[gpu::XE_GPU_REG_VGT_INDX_OFFSET]};
    reg::VGT_MIN_VTX_INDX min_indx{words[gpu::XE_GPU_REG_VGT_MIN_VTX_INDX]};
    reg::VGT_MAX_VTX_INDX max_indx{words[gpu::XE_GPU_REG_VGT_MAX_VTX_INDX]};
    reg::PA_SU_SC_MODE_CNTL mode{words[gpu::XE_GPU_REG_PA_SU_SC_MODE_CNTL]};
    require(output_path.path_select==xenos::VGTOutputPath::kVertexReuse &&
            initiator.major_mode==xenos::MajorMode::kImplicit && !initiator.not_eop &&
            initiator.source_select==xenos::SourceSelect::kAutoIndex,
            "Current VGT draw initiator is not an implicit auto-indexed primitive");
    const std::uint32_t count=initiator.num_indices;
    require(count>0 && !indx_offset.indx_offset && !min_indx.min_indx &&
            max_indx.max_indx>=count-1 && !mode.provoking_vtx_last,
            "Current VGT index offset/clamp or provoking vertex mode is unsupported");
    if(initiator.prim_type==xenos::PrimitiveType::kTriangleList) {
        require(count>=3 && count%3==0 && count<=0xffffu,
                "Current auto-indexed triangle list vertex count is invalid");
        RexPrimitiveDraw out{state,count,{}};
        out.state.topology=Topology::triangles;
        out.state.index_source=IndexSource::auto_vertex_list;
        return out;
    }
    require(initiator.prim_type==xenos::PrimitiveType::kQuadList &&
            initiator.index_size==xenos::IndexFormat::kInt16,
            "Current VGT draw initiator is not an implicit auto-indexed quad or triangle list");
    require(count>=4 && count%4==0 && count<=0xfffcu,
            "Current auto-indexed quad vertex count is invalid");
    RexPrimitiveDraw out{state,count,{}};
    out.state.topology=Topology::triangles;
    out.state.index_source=IndexSource::converted_auto_u16;
    const std::uint32_t quads=count/4;
    out.converted_indices.reserve(quads*6);
    for(std::uint32_t q=0;q<quads;++q) {
        const auto v0=static_cast<std::uint16_t>(q*4);
        out.converted_indices.insert(out.converted_indices.end(),
            {v0,static_cast<std::uint16_t>(v0+1),static_cast<std::uint16_t>(v0+2),
             v0,static_cast<std::uint16_t>(v0+2),static_cast<std::uint16_t>(v0+3)});
    }
    return out;
}
UiSnapshot map_rex_ui_target_state(std::span<const std::uint32_t> words,
                                   const RexRenderTargetView& target_view,
                                   UiSnapshot snapshot) {
    namespace gpu=rex::graphics;
    namespace reg=gpu::reg;
    namespace xenos=gpu::xenos;
    if(words.size()!=rex_register_word_count)
        throw std::invalid_argument("Invalid current SDK register extent");
    auto require=[](bool ok,const char* message){if(!ok)throw UnsupportedDraw(message);};
    reg::RB_MODECONTROL mode_control{words[gpu::XE_GPU_REG_RB_MODECONTROL]};
    reg::PA_SC_VIZ_QUERY viz_query{words[gpu::XE_GPU_REG_PA_SC_VIZ_QUERY]};
    reg::PA_SC_MPASS_PS_CNTL mpass{words[gpu::XE_GPU_REG_PA_SC_MPASS_PS_CNTL]};
    reg::SQ_PROGRAM_CNTL program_cntl{words[gpu::XE_GPU_REG_SQ_PROGRAM_CNTL]};
    reg::SQ_CONTEXT_MISC context_misc{words[gpu::XE_GPU_REG_SQ_CONTEXT_MISC]};
    require(mode_control.edram_mode==xenos::EdramMode::kColorDepth &&
            !viz_query.viz_query_ena && !viz_query.kill_pix_post_hi_z && !viz_query.kill_pix_post_detail_mask &&
            !mpass.mpass_ps_ena &&
            program_cntl.vs_export_mode==xenos::VertexShaderExportMode::kPosition1Vector &&
            !program_cntl.gen_index_pix && !program_cntl.gen_index_vtx && !program_cntl.param_gen &&
            !context_misc.sc_output_screen_xy,
            "Current render pass mode, export or visibility state is unsupported");
    reg::RB_SURFACE_INFO surface{words[gpu::XE_GPU_REG_RB_SURFACE_INFO]};
    constexpr std::uint32_t max_pitch_tiles=(1u<<xenos::kEdramPitchTilesBits)-1u;
    require(surface.surface_pitch>0 &&
            surface.surface_pitch<=max_pitch_tiles*xenos::kEdramTileWidthSamples,
            "Current EDRAM surface pitch is invalid");
    std::uint32_t samples=0,msaa_x_log2=0,msaa_y_log2=0;
    if(surface.msaa_samples==xenos::MsaaSamples::k1X){samples=1;}
    else if(surface.msaa_samples==xenos::MsaaSamples::k2X){samples=2;msaa_y_log2=1;}
    else if(surface.msaa_samples==xenos::MsaaSamples::k4X){samples=4;msaa_x_log2=1;msaa_y_log2=1;}
    else throw UnsupportedDraw("Unsupported MSAA sample mode");
    const std::uint32_t pitch_tiles=xenos::GetSurfacePitchTiles(
        surface.surface_pitch,surface.msaa_samples,false);
    require(pitch_tiles>0 && pitch_tiles<=max_pitch_tiles,"Invalid EDRAM pitch tiles");
    const std::uint32_t rt_width=pitch_tiles*(xenos::kEdramTileWidthSamples>>msaa_x_log2);
    const std::uint32_t tile_rows=(xenos::kEdramTileCount+pitch_tiles-1)/pitch_tiles;
    const std::uint32_t rt_height=std::min(tile_rows*(xenos::kEdramTileHeightSamples>>msaa_y_log2),
                                           xenos::kTexture2DCubeMaxWidthHeight);
    const std::uint32_t color_mask=words[gpu::XE_GPU_REG_RB_COLOR_MASK]&0xffffu;
    require((color_mask&0xfu)!=0 && (color_mask&~0xfu)==0,"Only single RT0 color output is supported");
    reg::RB_COLOR_INFO color_info{words[gpu::XE_GPU_REG_RB_COLOR_INFO]};
    require(!color_info.color_exp_bias && !color_info.color_base_bit_11,
            "Current RT0 color exponent bias or base bit 11 is unsupported");
    ColorFormat color_format=ColorFormat::unknown;
    const auto storage_format=xenos::GetStorageColorFormat(color_info.color_format);
    if(storage_format==xenos::ColorRenderTargetFormat::k_8_8_8_8)color_format=ColorFormat::rgba8_unorm;
    else if(storage_format==xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT ||
            storage_format==xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT)color_format=ColorFormat::rgba16_float;
    else throw UnsupportedDraw("Current RT0 color format is unsupported");
    require(target_view.format==color_format &&
            target_view.zero_initial_target!=Toggle::unknown &&
            target_view.width==rt_width && target_view.height==rt_height &&
            target_view.samples==samples && target_view.edram_base==color_info.color_base,
            "Resolved render target view does not match current EDRAM surface/color state");
    reg::PA_CL_CLIP_CNTL clip{words[gpu::XE_GPU_REG_PA_CL_CLIP_CNTL]};
    reg::PA_CL_VTE_CNTL vte{words[gpu::XE_GPU_REG_PA_CL_VTE_CNTL]};
    reg::PA_SU_VTX_CNTL vtx_cntl{words[gpu::XE_GPU_REG_PA_SU_VTX_CNTL]};
    reg::PA_SU_SC_MODE_CNTL sc_mode{words[gpu::XE_GPU_REG_PA_SU_SC_MODE_CNTL]};
    reg::PA_SC_WINDOW_OFFSET win_off{words[gpu::XE_GPU_REG_PA_SC_WINDOW_OFFSET]};
    require(!clip.clip_disable && clip.dx_clip_space_def && !clip.ucp_ena &&
            !clip.ucp_cull_only_ena && !clip.ps_ucp_mode && !clip.vtx_kill_or &&
            vte.vport_x_scale_ena && vte.vport_x_offset_ena &&
            vte.vport_y_scale_ena && vte.vport_y_offset_ena &&
            vte.vport_z_scale_ena && vte.vport_z_offset_ena &&
            vte.vtx_w0_fmt && !vte.vtx_xy_fmt && !vte.vtx_z_fmt &&
            vtx_cntl.pix_center==xenos::PixelCenter::kD3DZero &&
            (!sc_mode.vtx_window_offset_enable || (!win_off.window_x_offset && !win_off.window_y_offset)),
            "Current clip/viewport transform control state is unsupported");
    const float xscale=std::bit_cast<float>(words[gpu::XE_GPU_REG_PA_CL_VPORT_XSCALE]);
    const float xoffset=std::bit_cast<float>(words[gpu::XE_GPU_REG_PA_CL_VPORT_XOFFSET]);
    const float yscale=std::bit_cast<float>(words[gpu::XE_GPU_REG_PA_CL_VPORT_YSCALE]);
    const float yoffset=std::bit_cast<float>(words[gpu::XE_GPU_REG_PA_CL_VPORT_YOFFSET]);
    const float zscale=std::bit_cast<float>(words[gpu::XE_GPU_REG_PA_CL_VPORT_ZSCALE]);
    const float zoffset=std::bit_cast<float>(words[gpu::XE_GPU_REG_PA_CL_VPORT_ZOFFSET]);
    require(std::isfinite(xscale) && std::isfinite(yscale) &&
            xscale>0.0f && yscale<0.0f && xoffset==xscale && yoffset==-yscale &&
            zscale==1.0f && zoffset==0.0f,
            "Current viewport scale/offset registers are unsupported");
    const float vp_w=xscale*2.0f,vp_h=-yscale*2.0f;
    require(vp_w==std::floor(vp_w) && vp_h==std::floor(vp_h) &&
            vp_w>=1.0f && vp_h>=1.0f && vp_w<=8192.0f && vp_h<=8192.0f,
            "Current viewport extent is non-integer or out of bounds");
    reg::PA_SC_WINDOW_SCISSOR_TL win_tl{words[gpu::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL]};
    reg::PA_SC_WINDOW_SCISSOR_BR win_br{words[gpu::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR]};
    reg::PA_SC_SCREEN_SCISSOR_TL scr_tl{words[gpu::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL]};
    reg::PA_SC_SCREEN_SCISSOR_BR scr_br{words[gpu::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR]};
    std::int32_t tl_x=std::int32_t(win_tl.tl_x),tl_y=std::int32_t(win_tl.tl_y);
    std::int32_t br_x=std::int32_t(win_br.br_x),br_y=std::int32_t(win_br.br_y);
    if(!win_tl.window_offset_disable) {
        tl_x+=win_off.window_x_offset;tl_y+=win_off.window_y_offset;
        br_x+=win_off.window_x_offset;br_y+=win_off.window_y_offset;
    }
    tl_x=std::max(tl_x,std::int32_t(scr_tl.tl_x));
    tl_y=std::max(tl_y,std::int32_t(scr_tl.tl_y));
    br_x=std::min({br_x,std::int32_t(scr_br.br_x),std::int32_t(surface.surface_pitch)});
    br_y=std::min(br_y,std::int32_t(scr_br.br_y));
    tl_x=std::clamp(tl_x,std::int32_t(0),std::int32_t(surface.surface_pitch));
    tl_y=std::max(tl_y,std::int32_t(0));
    br_x=std::max(br_x,tl_x);
    br_y=std::max(br_y,tl_y);
    require(br_x>tl_x && br_y>tl_y &&
            std::uint32_t(br_x)<=rt_width && std::uint32_t(br_y)<=rt_height,
            "Current clamped scissor is empty or exceeds active render target");
    snapshot.state.target_format=color_format;
    snapshot.state.color_targets=1;
    snapshot.state.zero_initial_target=target_view.zero_initial_target;
    snapshot.state.logic_operation=Toggle::disabled;
    snapshot.width=rt_width;
    snapshot.height=rt_height;
    snapshot.samples=samples;
    snapshot.viewport_width=static_cast<std::uint32_t>(vp_w);
    snapshot.viewport_height=static_cast<std::uint32_t>(vp_h);
    snapshot.scissor={std::uint32_t(tl_x),std::uint32_t(tl_y),
                      std::uint32_t(br_x-tl_x),std::uint32_t(br_y-tl_y)};
    return snapshot;
}
Packet make_rex_ui_packet(const RexUiShaderState& s,UiSnapshot resources,
                         const RexTextureView& texture_view,
                         const RexRenderTargetView& target_view,
                         const RexGuestTextureData& guest_texture) {
    if(!resources.vertex_spirv.empty() || !resources.pixel_spirv.empty())
        throw std::invalid_argument("Resource views must not supply replacement shader modules");
    for(auto uniform:resources.uniforms)if(!uniform.empty())
        throw std::invalid_argument("Resource views must not supply replacement constant blocks");
    if(!resources.indices.empty())
        throw std::invalid_argument("Resource views must not supply replacement index buffers");
    if(resources.width || resources.height || resources.samples ||
       resources.viewport_width || resources.viewport_height ||
       resources.scissor!=std::array<std::uint32_t,4>{})
        throw std::invalid_argument("Resource views must not supply replacement target or viewport dimensions");
    const bool has_guest_texture=guest_texture.base_address || guest_texture.mips_address ||
                                 !guest_texture.base_bytes.empty() || !guest_texture.mips_bytes.empty();
    if(has_guest_texture && !resources.texture_mips.empty())
        throw std::invalid_argument("Supply either decoded texture mips or raw guest texture bytes, not both");
    if(!supported_ui_shader_pair(s.vertex_spirv,s.pixel_spirv))
        throw UnsupportedDraw("Current translated shader pair is unsupported");
    const auto primitive=map_rex_ui_primitive_draw(s.registers,resources.state);
    resources.state=primitive.state;
    resources.indices=primitive.converted_indices;
    resources.state=map_rex_ui_register_state(s.registers,resources.state);
    resources=map_rex_ui_target_state(s.registers,target_view,resources);
    std::vector<std::vector<std::uint8_t>> decoded_mip_storage;
    std::vector<TextureMip> decoded_mip_views;
    if(has_guest_texture) {
        decoded_mip_views=decode_rex_ui_bc1_mips(s.registers,0,guest_texture,decoded_mip_storage);
        resources.texture_mips=decoded_mip_views;
    }
    // Slot 0 and sampler binding 0 are part of the exact selected shader pair,
    // not general defaults.
    resources.state=map_rex_ui_texture_state(s.registers,0,texture_view,resources.texture_mips,resources.state);
    resources.state=map_rex_ui_sampler_state(s.registers,RexSamplerBinding{0},resources.state);
    // The exact module gate above establishes the selected shader ABI, including
    // fetch slot 95 and its 24-byte stride. These are not general SDK defaults.
    resources.state.vertex_fetch=95;resources.state.vertex_stride=24;
    const auto range=rex_vertex_range(s.registers,95);
    if(resources.vertex_address!=range.address || resources.vertex_bytes.size()!=range.bytes ||
       range.bytes!=std::size_t(primitive.guest_vertex_count)*resources.state.vertex_stride)
        throw UnsupportedDraw("Resolved vertex bytes do not match the current fetch range and draw count");
    const auto constants=pack_rex_ui_constants(s.registers,s.vertex_floats,s.pixel_floats,s.system_constants);
    auto decode_words=[](std::span<const std::uint8_t> bytes) {
        std::vector<std::uint32_t> words;words.reserve(bytes.size()/4);
        for(std::size_t i=0;i<bytes.size();i+=4)
            words.push_back(std::uint32_t(bytes[i])|std::uint32_t(bytes[i+1])<<8|
                std::uint32_t(bytes[i+2])<<16|std::uint32_t(bytes[i+3])<<24);
        return words;
    };
    const auto vertex=decode_words(s.vertex_spirv),pixel=decode_words(s.pixel_spirv);
    resources.vertex_spirv=vertex;resources.pixel_spirv=pixel;
    for(std::size_t i=0;i<5;++i)resources.uniforms[i]=constants[i];
    auto packet=make_ui_packet(resources);validate_supported(packet);return packet;
}
CompositePacket make_rex_composite_packet(const RexUiShaderState& s,
                                          const RexCompositeResources& resources,
                                          const std::array<RexTextureView,2>& texture_views,
                                          const RexRenderTargetView& target_view) {
    if(!supported_composite_shader_pair(s.vertex_spirv,s.pixel_spirv))
        throw UnsupportedDraw("Current translated composite shader pair is unsupported");
    const auto primitive=map_rex_ui_primitive_draw(s.registers,{});
    UiSnapshot target_snap;
    target_snap.state=map_rex_ui_register_state(s.registers,primitive.state);
    target_snap=map_rex_ui_target_state(s.registers,target_view,target_snap);
    CompositeSnapshot snap;
    snap.state=target_snap.state;
    snap.width=target_snap.width;snap.height=target_snap.height;
    snap.samples=target_snap.samples;
    snap.viewport_width=target_snap.viewport_width;snap.viewport_height=target_snap.viewport_height;
    snap.scissor=target_snap.scissor;
    snap.vertex_address=resources.vertex_address;
    snap.vertex_count=primitive.guest_vertex_count;
    snap.vertex_bytes=resources.vertex_bytes;
    snap.state.vertex_fetch=95;snap.state.vertex_stride=16;
    const auto range=rex_vertex_range(s.registers,95);
    if(resources.vertex_address!=range.address || resources.vertex_bytes.size()!=range.bytes ||
       range.bytes!=std::size_t(primitive.guest_vertex_count)*snap.state.vertex_stride)
        throw UnsupportedDraw("Resolved composite vertex bytes do not match current fetch range and draw count");
    std::array<std::vector<std::uint8_t>,2> swizzled_textures;
    for(std::uint32_t i=0;i<2;++i) {
        snap.state=map_rex_rgba8_texture_state(s.registers,i,texture_views[i],resources.texture_rgba8_bytes[i],snap.state);
        const auto sampler_state=map_rex_ui_sampler_state(s.registers,
            RexSamplerBinding{i,RexSamplerFilter::use_fetch_const,RexSamplerFilter::use_fetch_const,
                              RexSamplerFilter::use_fetch_const,RexAnisoFilter::use_fetch_const,
                              RexAnisoFilter::disabled},{});
        swizzled_textures[i]=swizzle_rex_rgba8_pixels(resources.texture_rgba8_bytes[i],texture_views[i].host_swizzle);
        snap.textures[i]=CompositeTextureInput{
            texture_views[i].width,texture_views[i].height,swizzled_textures[i],
            sampler_state.min_filter,sampler_state.mag_filter,sampler_state.mip_filter,
            sampler_state.address_u,sampler_state.address_v,sampler_state.address_w,
            sampler_state.normalized_coordinates,sampler_state.anisotropy,
            sampler_state.min_lod,sampler_state.max_lod,sampler_state.lod_bias,
            sampler_state.max_anisotropy};
    }
    const auto constants=pack_rex_ui_constants(s.registers,s.vertex_floats,s.pixel_floats,s.system_constants,0,0);
    auto decode_words=[](std::span<const std::uint8_t> bytes) {
        std::vector<std::uint32_t> words;words.reserve(bytes.size()/4);
        for(std::size_t i=0;i<bytes.size();i+=4)
            words.push_back(std::uint32_t(bytes[i])|std::uint32_t(bytes[i+1])<<8|
                std::uint32_t(bytes[i+2])<<16|std::uint32_t(bytes[i+3])<<24);
        return words;
    };
    const auto vertex=decode_words(s.vertex_spirv),pixel=decode_words(s.pixel_spirv);
    snap.vertex_spirv=vertex;snap.pixel_spirv=pixel;
    for(std::size_t i=0;i<5;++i)snap.uniforms[i]=constants[i];
    auto packet=make_composite_packet(snap);
    validate_supported_composite(packet);
    return packet;
}
}
