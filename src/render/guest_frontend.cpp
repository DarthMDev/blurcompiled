#include "render/guest_frontend.hpp"

namespace blur::render::guest {
namespace {
void require(bool condition,const char* message) {if(!condition)throw UnsupportedDraw(message);}
}
Packet make_ui_packet(const UiSnapshot& v) {
    const auto& s=v.state;
    require(s.topology==Topology::triangles && s.index_source==IndexSource::converted_auto_u16,
            "Unsupported/unknown UI primitive or index source");
    require(s.target_format==ColorFormat::rgba8_unorm && s.color_targets==1 && s.color_write_mask==15 &&
            s.texture_format==TextureFormat::bc1_unorm_array,"Unsupported/unknown UI attachment or texture format");
    require(s.vertex_stride==24 && s.vertex_fetch==95,"Unsupported UI vertex layout/fetch");
    require(s.zero_initial_target!=Toggle::unknown,"UI target initialization state is unknown");
    require(s.depth_test==Toggle::disabled && s.depth_write==Toggle::disabled && s.stencil==Toggle::disabled &&
            s.alpha_to_coverage==Toggle::disabled && s.logic_operation==Toggle::disabled,"Unsupported/unknown UI depth/stencil/coverage/logic state");
    require(s.blending==Toggle::enabled && s.source_rgb==BlendFactor::source_alpha && s.source_alpha==BlendFactor::source_alpha &&
            s.destination_rgb==BlendFactor::inverse_source_alpha && s.destination_alpha==BlendFactor::inverse_source_alpha &&
            s.rgb_operation==BlendOperation::add && s.alpha_operation==BlendOperation::add,"Unsupported/unknown UI blending");
    require(s.cull==Cull::back && s.winding==Winding::clockwise,"Unsupported/unknown UI face state");
    require(s.min_filter==TextureFilter::linear && s.mag_filter==TextureFilter::linear && s.mip_filter==TextureFilter::linear &&
            s.address_u==Address::wrap && s.address_v==Address::wrap && s.address_w==Address::clamp,
            "Unsupported/unknown UI sampler filter/address state");
    require(s.normalized_coordinates==Toggle::enabled && s.anisotropy==Toggle::enabled && s.identity_view==Toggle::enabled &&
            s.max_anisotropy==4 && s.min_lod==0 && s.max_lod==1000 && s.lod_bias==0,"Unsupported/unknown UI sampler LOD/view state");
    // Bound borrowed spans before allocating their owned copies.
    auto valid=[](bool condition) {if(!condition)throw std::invalid_argument("Invalid UI snapshot resource bounds");};
    valid(v.width && v.height && v.width<=2048 && v.height<=2048 &&
          v.viewport_width && v.viewport_height && v.viewport_width<=8192 && v.viewport_height<=8192 &&
          (v.samples==1 || v.samples==4));
    valid(!v.vertex_bytes.empty() && v.vertex_bytes.size()<=(1<<20) && v.vertex_bytes.size()%24==0 &&
          v.vertex_address%4==0 && std::uint64_t(v.vertex_address)+v.vertex_bytes.size()<=(512ull<<20));
    valid(v.vertex_spirv.size()>=5 && v.vertex_spirv.size()<=(1<<18) &&
          v.pixel_spirv.size()>=5 && v.pixel_spirv.size()<=(1<<18));
    for(const auto& uniform:v.uniforms)valid(!uniform.empty() && uniform.size()<=4096 && uniform.size()%16==0);
    valid(!v.indices.empty() && v.indices.size()<=65536 && v.indices.size()%3==0 &&
          !v.texture_mips.empty() && v.texture_mips.size()<=12);
    for(const auto& mip:v.texture_mips)
        valid(mip.width && mip.height && mip.width<=2048 && mip.height<=2048 &&
              mip.bytes.size()==std::size_t((mip.width+3)/4)*((mip.height+3)/4)*8);
    Packet p;
    p.width=v.width; p.height=v.height; p.viewport_width=v.viewport_width; p.viewport_height=v.viewport_height;
    p.samples=v.samples; p.scissor=v.scissor; p.vertex_address=v.vertex_address;
    p.vertex_bytes.assign(v.vertex_bytes.begin(),v.vertex_bytes.end());
    p.vertex_spirv.assign(v.vertex_spirv.begin(),v.vertex_spirv.end()); p.pixel_spirv.assign(v.pixel_spirv.begin(),v.pixel_spirv.end());
    for (std::size_t i=0;i<5;++i) p.uniforms[i].assign(v.uniforms[i].begin(),v.uniforms[i].end());
    p.indices.assign(v.indices.begin(),v.indices.end()); p.texture_mips.assign(v.texture_mips.begin(),v.texture_mips.end());
    validate(p);
    return p;
}
CompositePacket make_composite_packet(const CompositeSnapshot& v) {
    const auto& s=v.state;
    require(s.topology==Topology::triangles && s.index_source==IndexSource::auto_vertex_list,
            "Unsupported/unknown composite primitive or index source");
    require(s.target_format==ColorFormat::rgba16_float && s.color_targets==1 && s.color_write_mask==7 &&
            s.texture_format==TextureFormat::rgba8_unorm_array,
            "Unsupported/unknown composite attachment or texture format");
    require(s.vertex_stride==16 && s.vertex_fetch==95,"Unsupported composite vertex layout/fetch");
    require(s.zero_initial_target==Toggle::disabled,"Composite pass requires a preserved nonzero target");
    require(s.depth_test==Toggle::disabled && s.depth_write==Toggle::disabled && s.stencil==Toggle::disabled &&
            s.alpha_to_coverage==Toggle::disabled && s.logic_operation==Toggle::disabled,
            "Unsupported/unknown composite depth/stencil/coverage/logic state");
    require(s.blending==Toggle::enabled && s.source_rgb==BlendFactor::one && s.source_alpha==BlendFactor::one &&
            s.destination_rgb==BlendFactor::inverse_source_alpha && s.destination_alpha==BlendFactor::inverse_source_alpha &&
            s.rgb_operation==BlendOperation::add && s.alpha_operation==BlendOperation::add,
            "Unsupported/unknown composite premultiplied blending");
    require(s.cull==Cull::none && (s.winding==Winding::counter_clockwise || s.winding==Winding::clockwise),
            "Unsupported/unknown composite face state");
    for(std::size_t i=0;i<2;++i) {
        const auto& t=v.textures[i];
        const auto expected_addr=(i==0)?Address::clamp:Address::wrap;
        require(t.min_filter==TextureFilter::linear && t.mag_filter==TextureFilter::linear &&
                t.mip_filter==TextureFilter::basemap &&
                t.address_u==expected_addr && t.address_v==expected_addr && t.address_w==Address::clamp &&
                t.normalized_coordinates==Toggle::enabled && t.anisotropy==Toggle::disabled &&
                t.max_anisotropy==1 && t.min_lod==0 && t.max_lod==0 && t.lod_bias==0,
                "Unsupported/unknown composite sampler state");
    }
    auto valid=[](bool condition) {if(!condition)throw std::invalid_argument("Invalid composite snapshot resource bounds");};
    valid(v.width && v.height && v.width<=2048 && v.height<=2048 &&
          v.viewport_width && v.viewport_height && v.viewport_width<=8192 && v.viewport_height<=8192 &&
          v.samples==1);
    valid(v.vertex_count==6 && !v.vertex_bytes.empty() && v.vertex_bytes.size()==std::size_t(v.vertex_count)*16 &&
          v.vertex_address%4==0 && std::uint64_t(v.vertex_address)+v.vertex_bytes.size()<=(512ull<<20));
    valid(v.vertex_spirv.size()>=5 && v.vertex_spirv.size()<=(1<<18) &&
          v.pixel_spirv.size()>=5 && v.pixel_spirv.size()<=(1<<18));
    for(const auto& uniform:v.uniforms)valid(!uniform.empty() && uniform.size()<=4096 && uniform.size()%16==0);
    for(const auto& tex:v.textures)
        valid(tex.width && tex.height && tex.width<=2048 && tex.height<=2048 &&
              tex.rgba8_bytes.size()==std::size_t(tex.width)*tex.height*4);
    CompositePacket p;
    p.width=v.width; p.height=v.height; p.viewport_width=v.viewport_width; p.viewport_height=v.viewport_height;
    p.samples=v.samples; p.scissor=v.scissor; p.vertex_address=v.vertex_address; p.vertex_count=v.vertex_count;
    p.vertex_bytes.assign(v.vertex_bytes.begin(),v.vertex_bytes.end());
    p.vertex_spirv.assign(v.vertex_spirv.begin(),v.vertex_spirv.end());
    p.pixel_spirv.assign(v.pixel_spirv.begin(),v.pixel_spirv.end());
    for(std::size_t i=0;i<5;++i)p.uniforms[i].assign(v.uniforms[i].begin(),v.uniforms[i].end());
    for(std::size_t i=0;i<2;++i) {
        p.textures[i].width=v.textures[i].width;
        p.textures[i].height=v.textures[i].height;
        p.textures[i].rgba8_bytes.assign(v.textures[i].rgba8_bytes.begin(),v.textures[i].rgba8_bytes.end());
    }
    validate_composite(p);
    return p;
}
}
