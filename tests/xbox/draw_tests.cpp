#include "rex_plume_draw.hpp"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace blur::render::guest;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
template<class E,class F> void rejects(F action) {
    try {action();}catch(const E&){return;}throw std::runtime_error("Unsupported draw input accepted");
}
std::vector<std::uint8_t> read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);check(bool(file),"Input open failed");
    const auto size=file.tellg();check(size>0 && size<(32<<20),"Invalid input size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()),size);check(bool(file),"Input read failed");return bytes;
}
std::vector<std::uint32_t> words(std::span<const std::uint8_t> bytes) {
    check(bytes.size()%4==0,"Word extent differs");std::vector<std::uint32_t> out;
    for(std::size_t i=0;i<bytes.size();i+=4)out.push_back(std::uint32_t(bytes[i])|
        std::uint32_t(bytes[i+1])<<8|std::uint32_t(bytes[i+2])<<16|std::uint32_t(bytes[i+3])<<24);
    return out;
}
RexFloatConstantMap captured_map(std::span<const std::uint8_t> bytes) {
    check(bytes.size()==32,"Captured float bitmap extent differs");const auto raw=words(bytes);
    RexFloatConstantMap map;
    for(unsigned i=0;i<4;++i) {
        map.bitmap[i]=raw[i*2]|std::uint64_t(raw[i*2+1])<<32;
        map.count+=std::popcount(map.bitmap[i]);
    }
    return map;
}
// Synchronized guest resource payloads only. All pipeline, sampler, primitive,
// converted index, target, viewport and scissor state is mapped from SDK registers
// and resolved host view descriptors.
UiSnapshot resource_fixture(const Packet& p) {
    UiSnapshot v;
    v.vertex_address=p.vertex_address;
    v.vertex_bytes=p.vertex_bytes;
    v.texture_mips=p.texture_mips;
    return v;
}
}
int main(int argc,char** argv) {
    try {
        std::vector<std::uint32_t> registers(rex_register_word_count);
        constexpr unsigned slot=0x4800+95*2;
        registers[slot]=0x103;registers[slot+1]=24; // native fetch: 0x100, 6 words
        auto range=rex_vertex_range(registers,95);check(range.address==0x100 && range.bytes==24,"Fetch range differs");
        for(unsigned index:{0u,94u})rejects<UnsupportedDraw>([&]{rex_vertex_range(registers,index);});
        rejects<std::invalid_argument>([&]{rex_vertex_range(registers,96);});
        rejects<std::invalid_argument>([&]{rex_vertex_range(std::span(registers).first(3),95);});
        registers[slot]=0x101;rejects<UnsupportedDraw>([&]{rex_vertex_range(registers,95);});
        registers[slot]=0xfffffff3;rejects<UnsupportedDraw>([&]{rex_vertex_range(registers,95);});
        registers[slot]=0x103;registers[slot+1]=0;rejects<UnsupportedDraw>([&]{rex_vertex_range(registers,95);});
        registers[slot+1]=(1u<<20)+4;rejects<UnsupportedDraw>([&]{rex_vertex_range(registers,95);});
        // Synthetic primitive & auto-index quad conversion checks.
        registers[0x21FC]=0x0008008du; // 8 vertices (2 quads), kQuadList, kAutoIndex
        registers[0x2100]=0x00ffffffu;
        const auto prim=map_rex_ui_primitive_draw(registers,{});
        check(prim.state.topology==Topology::triangles && prim.state.index_source==IndexSource::converted_auto_u16 &&
              prim.guest_vertex_count==8 &&
              prim.converted_indices==std::vector<std::uint16_t>{0,1,2,0,2,3,4,5,6,4,6,7},
              "Synthetic quad auto-index conversion differs");
        // Auto-indexed triangle list (e.g. 6-vertex fullscreen composite quad).
        registers[0x21FC]=0x00060084u; // 6 vertices, kTriangleList, kAutoIndex
        const auto tri_prim=map_rex_ui_primitive_draw(registers,{});
        check(tri_prim.state.topology==Topology::triangles &&
              tri_prim.state.index_source==IndexSource::auto_vertex_list &&
              tri_prim.guest_vertex_count==6 && tri_prim.converted_indices.empty(),
              "Synthetic auto-indexed triangle list mapping differs");
        registers[0x21FC]=0x00050084u;rejects<UnsupportedDraw>([&]{map_rex_ui_primitive_draw(registers,{});});
        registers[0x21FC]=0x0006008du;rejects<UnsupportedDraw>([&]{map_rex_ui_primitive_draw(registers,{});});
        registers[0x21FC]=0x00080004u;rejects<UnsupportedDraw>([&]{map_rex_ui_primitive_draw(registers,{});});
        registers[0x21FC]=0x0008008du;registers[0x2102]=1;
        rejects<UnsupportedDraw>([&]{map_rex_ui_primitive_draw(registers,{});});
        registers[0x2102]=0;
        // Synthetic render target, viewport & clamped scissor checks.
        registers[0x2208]=4u; // kColorDepth
        registers[0x2000]=1280u|(2u<<16); // pitch 1280, 4x MSAA -> 1280x512 EDRAM surface
        registers[0x2104]=0xfu;
        registers[0x2001]=0u; // base 0, k_8_8_8_8
        registers[0x2204]=1u<<19; // dx_clip_space_def
        registers[0x2206]=0x43fu;
        registers[0x2302]=4u;
        registers[0x210F]=std::bit_cast<std::uint32_t>(640.0f);
        registers[0x2110]=std::bit_cast<std::uint32_t>(640.0f);
        registers[0x2111]=std::bit_cast<std::uint32_t>(-360.0f);
        registers[0x2112]=std::bit_cast<std::uint32_t>(360.0f);
        registers[0x2113]=std::bit_cast<std::uint32_t>(1.0f);
        registers[0x2114]=std::bit_cast<std::uint32_t>(0.0f);
        registers[0x200E]=0u;registers[0x200F]=(8192u<<16)|8192u;
        registers[0x2081]=0u;registers[0x2082]=(256u<<16)|1280u;
        RexRenderTargetView target{ColorFormat::rgba8_unorm,Toggle::enabled,1280,512,4,0};
        const auto mapped_target=map_rex_ui_target_state(registers,target,{});
        check(mapped_target.state.target_format==ColorFormat::rgba8_unorm &&
              mapped_target.state.color_targets==1 &&
              mapped_target.state.zero_initial_target==Toggle::enabled &&
              mapped_target.state.logic_operation==Toggle::disabled &&
              mapped_target.width==1280 && mapped_target.height==512 && mapped_target.samples==4 &&
              mapped_target.viewport_width==1280 && mapped_target.viewport_height==720 &&
              mapped_target.scissor==std::array<std::uint32_t,4>{0,0,1280,256},
              "Synthetic target/viewport/scissor mapping differs");
        rejects<UnsupportedDraw>([&]{map_rex_ui_target_state(registers,{},{});});
        auto bad_target=target;bad_target.height=720;
        rejects<UnsupportedDraw>([&]{map_rex_ui_target_state(registers,bad_target,{});});
        // 1x MSAA RGBA16F persistent target (1280x2048 EDRAM surface, 1280x720 scissor).
        registers[0x2000]=1280u; // pitch 1280, 1x MSAA -> 1280x2048 EDRAM surface
        registers[0x2104]=0x7u; // RGB write mask
        registers[0x2001]=12u<<16; // base 0, k_16_16_16_16_FLOAT
        registers[0x2082]=(720u<<16)|1280u;
        RexRenderTargetView f16_target{ColorFormat::rgba16_float,Toggle::disabled,1280,2048,1,0};
        const auto mapped_f16=map_rex_ui_target_state(registers,f16_target,{});
        check(mapped_f16.state.target_format==ColorFormat::rgba16_float &&
              mapped_f16.state.zero_initial_target==Toggle::disabled &&
              mapped_f16.width==1280 && mapped_f16.height==2048 && mapped_f16.samples==1 &&
              mapped_f16.scissor==std::array<std::uint32_t,4>{0,0,1280,720},
              "Synthetic 1x MSAA RGBA16F target mapping differs");
        RexUiShaderState empty;rejects<UnsupportedDraw>([&]{make_rex_ui_packet(empty,{});});
        auto polluted=UiSnapshot{};std::array<std::uint32_t,1> fake_shader{};polluted.vertex_spirv=fake_shader;
        rejects<std::invalid_argument>([&]{make_rex_ui_packet(empty,polluted);});
        polluted={};std::array<std::uint8_t,1> fake_uniform{};polluted.uniforms[2]=fake_uniform;
        rejects<std::invalid_argument>([&]{make_rex_ui_packet(empty,polluted);});
        polluted={};std::array<std::uint16_t,6> fake_indices{};polluted.indices=fake_indices;
        rejects<std::invalid_argument>([&]{make_rex_ui_packet(empty,polluted);});
        polluted={};polluted.width=1280;
        rejects<std::invalid_argument>([&]{make_rex_ui_packet(empty,polluted);});
        if(argc==3 || argc==4) {
            const auto encoded=read(argv[1]);const auto p=decode(encoded);const std::filesystem::path capture=argv[2];
            registers=words(read(capture/"ui-registers.bin"));
            const auto vs=read(capture/"ui-vs.spv"),ps=read(capture/"ui-ps.spv"),system=read(capture/"ui-system-constants.bin");
            RexUiShaderState shader{registers,vs,ps,system,captured_map(read(capture/"ui-float-map-vs.bin")),
                                                         captured_map(read(capture/"ui-float-map-ps.bin"))};
            const auto blocks=pack_rex_ui_constants(registers,shader.vertex_floats,shader.pixel_floats,system);
            check(blocks==p.uniforms,"Captured UI constant blocks differ from current SDK register/map reconstruction");
            // Native view facts recorded by run5's ui_atlas_input and active RT0 events.
            // Source offsets are binary-specific reference input, not defaults.
            RexTextureView texture{RexNativeTextureFormat::bc1_unorm,RexNativeTextureKind::array_2d,
                Toggle::enabled,0x688,512,512,1,0,10,260927488,261058560};
            auto resources=resource_fixture(p);
            const auto rebuilt=make_rex_ui_packet(shader,resources,texture,target);
            const auto rebuilt_bytes=encode(rebuilt);
            check(rebuilt_bytes==encoded,"Composed SDK frontend packet differs from verified replay packet");
            const auto tex_ranges=rex_ui_texture_guest_ranges(registers,0);
            check(tex_ranges.base_address==texture.source_base &&
                  tex_ranges.mips_address==texture.source_mips &&
                  tex_ranges.width==512 && tex_ranges.height==512 && tex_ranges.mip_count==10,
                  "Captured UI texture guest layout ranges differ from resolved view");
            rejects<UnsupportedDraw>([&]{make_rex_ui_packet(shader,resources,texture);}); // unknown target view
            rejects<UnsupportedDraw>([&]{make_rex_ui_packet(shader,resources,{},target);}); // unknown texture view
            resources=resource_fixture(p);resources.vertex_address+=4;
            rejects<UnsupportedDraw>([&]{make_rex_ui_packet(shader,resources,texture,target);});
            resources=resource_fixture(p);resources.vertex_bytes=resources.vertex_bytes.first(resources.vertex_bytes.size()-24);
            rejects<UnsupportedDraw>([&]{make_rex_ui_packet(shader,resources,texture,target);});
            resources=resource_fixture(p);auto modified=ps;modified.back()^=1;shader.pixel_spirv=modified;
            rejects<UnsupportedDraw>([&]{make_rex_ui_packet(shader,resources,texture,target);});
            // Offline verification of the run5 composite pass (sequence 253233).
            const auto comp_regs=words(read(capture/"composite-registers.bin"));
            const auto comp_vs=read(capture/"composite-vs.spv"),comp_ps=read(capture/"composite-ps.spv");
            const auto comp_sys=read(capture/"composite-system-constants.bin");
            const auto comp_vtx=read(capture/"composite-vertex-95.bin");
            const auto comp_in0=read(capture/"composite-input-0.rgba");
            const auto comp_in1=read(capture/"composite-input-1.rgba");
            // Verify 4x MSAA RGBA8 UI tile resolve (aux-stage-19, 30, 41 -> composite-input-0.rgba).
            const auto aux19=read(capture/"aux-stage-19.samples4.rgba8");
            const auto aux30=read(capture/"aux-stage-30.samples4.rgba8");
            const auto aux41=read(capture/"aux-stage-41.samples4.rgba8");
            std::vector<std::uint8_t> resolved_comp_in0(1280u*720u*4u,0u);
            constexpr std::uint32_t kAuxCopyDestInfo=16777986u; // endian=2, format=k_8_8_8_8(6), swap=1
            resolve_rex_rgba8_msaa4_strip(
                RexMsaa4ResolveStrip{kAuxCopyDestInfo,6u,1280u,256u,256u,0u,aux19},720u,resolved_comp_in0);
            resolve_rex_rgba8_msaa4_strip(
                RexMsaa4ResolveStrip{kAuxCopyDestInfo,6u,1280u,256u,256u,256u,aux30},720u,resolved_comp_in0);
            resolve_rex_rgba8_msaa4_strip(
                RexMsaa4ResolveStrip{kAuxCopyDestInfo,6u,1280u,208u,256u,512u,aux41},720u,resolved_comp_in0);
            check(resolved_comp_in0==comp_in0,
                  "Resolved 4x MSAA UI strips differ from captured composite-input-0.rgba");
            std::cout<<"4x MSAA UI tile resolve verified against composite-input-0.rgba: exact_bytes="
                     <<resolved_comp_in0.size()<<"/"<<comp_in0.size()<<'\n';
            RexUiShaderState comp_shader{comp_regs,comp_vs,comp_ps,comp_sys,
                captured_map(read(capture/"composite-float-map-vs.bin")),
                captured_map(read(capture/"composite-float-map-ps.bin"))};
            const auto comp_blocks=pack_rex_ui_constants(comp_regs,comp_shader.vertex_floats,comp_shader.pixel_floats,comp_sys,0,0);
            check(comp_blocks[0]==read(capture/"composite-uniform-0.bin") &&
                  comp_blocks[3]==read(capture/"composite-uniform-3.bin") &&
                  comp_blocks[4]==read(capture/"composite-uniform-4.bin") &&
                  comp_blocks[1].size()==16 && comp_blocks[2].size()==16,
                  "Captured composite constant blocks differ from SDK register/map reconstruction");
            const auto comp_vrange=rex_vertex_range(comp_regs,95);
            check(comp_vrange.address==532776000u && comp_vrange.bytes==96u,"Composite vertex fetch range differs");
            RexCompositeResources comp_res{comp_vrange.address,comp_vtx,{resolved_comp_in0,comp_in1}};
            std::array<RexTextureView,2> comp_tex_views{{
                {RexNativeTextureFormat::rgba8_unorm,RexNativeTextureKind::array_2d,Toggle::disabled,0x60au,1280,720,1,0,1,271814656u,0u},
                {RexNativeTextureFormat::rgba8_unorm,RexNativeTextureKind::array_2d,Toggle::disabled,0x60au,320,180,1,0,1,271482880u,0u}}};
            const auto comp_packet=make_rex_composite_packet(comp_shader,comp_res,comp_tex_views,f16_target);
            check(comp_packet.width==1280 && comp_packet.height==2048 && comp_packet.samples==1 &&
                  comp_packet.vertex_count==6 && comp_packet.textures[0].width==1280 && comp_packet.textures[1].width==320,
                  "Composed composite packet dimensions or counts differ");
            const auto stage64=read(capture/"stage-64.rgba16f");
            const auto stage65=read(capture/"stage-65.rgba16f");
            Renderer gpu_renderer;
            const auto comp_result=gpu_renderer.draw_composite(comp_packet,stage64,1280,720,LoadAction::preserve);
            check(comp_result.rgba16f_bytes.size()==stage65.size(),"Composite GPU readback byte count differs from stage-65.rgba16f");
            std::size_t exact_halfs=0,alpha_mismatches=0;
            float max_abs_diff=0.0f;
            auto half_to_float=[](std::uint16_t h) {
                const std::uint32_t sign=(h&0x8000u)<<16;
                const std::uint32_t exp=(h>>10)&0x1fu;
                const std::uint32_t mant=h&0x3ffu;
                std::uint32_t f=0;
                if(exp==0) {
                    if(mant!=0) {
                        std::uint32_t m=mant,e=127-14;
                        while((m&0x400u)==0){m<<=1;--e;}
                        f=sign|(e<<23)|((m&0x3ffu)<<13);
                    } else f=sign;
                } else if(exp==31) {
                    f=sign|0x7f800000u|(mant<<13);
                } else {
                    f=sign|((exp+112u)<<23)|(mant<<13);
                }
                return std::bit_cast<float>(f);
            };
            for(std::size_t i=0;i<stage65.size();i+=2) {
                const auto got=std::uint16_t(comp_result.rgba16f_bytes[i])|(std::uint16_t(comp_result.rgba16f_bytes[i+1])<<8);
                const auto ref=std::uint16_t(stage65[i])|(std::uint16_t(stage65[i+1])<<8);
                if(got==ref)++exact_halfs;
                if((i%8)==6 && got!=ref)++alpha_mismatches;
                const float d=std::abs(half_to_float(got)-half_to_float(ref));
                if(d>max_abs_diff)max_abs_diff=d;
            }
            check(alpha_mismatches==0,"Composite RGB write mask modified destination alpha");
            check(max_abs_diff<=0.002f,"Composite GPU output diverges from stage-65.rgba16f");
            std::cout<<"Composite pass GPU output verified against stage-65.rgba16f: exact_halfs="<<exact_halfs<<"/"<<(stage65.size()/2)<<" alpha_mismatches="<<alpha_mismatches<<" max_abs_diff="<<max_abs_diff<<'\n';
            // Verify terminal RGBA16F -> 7e3 EDRAM -> k_8_8_8_8 resolve against swap-source.rgba and resolve-destination.bin.
            const auto stage91=read(capture/"stage-91.rgba16f");
            const auto swap_source=read(capture/"swap-source.rgba");
            const auto resolve_dest=read(capture/"resolve-destination.bin");
            check(stage91.size()==1280u*720u*8u,"Unexpected stage-91.rgba16f byte size");
            std::vector<std::uint16_t> stage91_halfs(stage91.size()/2u);
            std::memcpy(stage91_halfs.data(),stage91.data(),stage91.size());
            constexpr std::uint32_t kTerminalCopyDestInfo=(6u<<7u)|(1u<<24u); // endian=0, format=k_8_8_8_8(6), swap=1
            const auto term_resolve=resolve_rex_rgba16f_to_7e3_rgba8(
                RexHdr7e3ResolveSpec{1280u,720u,kTerminalCopyDestInfo,1280u,true},stage91_halfs);
            check(term_resolve.linear_rgba8==swap_source,
                  "Terminal RGBA16F-to-7e3 resolve linear output differs from swap-source.rgba");
            check(term_resolve.tiled_destination==resolve_dest,
                  "Terminal RGBA16F-to-7e3 resolve tiled output differs from resolve-destination.bin");
            std::cout<<"Terminal RGBA16F-to-7e3 resolve verified against swap-source.rgba ("
                     <<term_resolve.linear_rgba8.size()<<" bytes) and resolve-destination.bin ("
                     <<term_resolve.tiled_destination.size()<<" bytes)\n";
            if(argc==4) {
                const auto output=std::filesystem::absolute(argv[3]).lexically_normal();
                check(!std::filesystem::exists(output),"Refusing to overwrite an output");
                const auto relative=output.lexically_relative(std::filesystem::canonical(capture));
                check(!relative.empty() && *relative.begin()=="..","Output must stay outside reference capture");
                std::ofstream file(output,std::ios::binary);file.write(reinterpret_cast<const char*>(rebuilt_bytes.data()),rebuilt_bytes.size());
                check(bool(file),"Output write failed");
            }
            std::cout<<"All five constant blocks, register/sampler/texture/target/primitive descriptors and converted indices match; composed packet is byte-identical.\n";
        }
        std::cout<<"Synthetic SDK draw checks: fetch range/type/bounds, quad auto-index conversion, target/viewport/scissor mapping, unknown shaders and replacement-state rejection passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
