#include "render/guest_frontend.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

using namespace blur::render::guest;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
template<class E,class F> void rejects(F action) {
    try {action();} catch(const E&) {return;}throw std::runtime_error("Invalid frontend state accepted");
}
// Test-only state for the explicitly fixed BPG1 contract. A live SDK frontend
// must query current state instead of using a factory like this one.
UiState eligible() {
    UiState s;
    s.topology=Topology::triangles;s.index_source=IndexSource::converted_auto_u16;
    s.target_format=ColorFormat::rgba8_unorm;s.texture_format=TextureFormat::bc1_unorm_array;
    s.color_targets=1;s.color_write_mask=15;s.vertex_stride=24;s.vertex_fetch=95;
    s.zero_initial_target=Toggle::enabled;
    s.depth_test=s.depth_write=s.stencil=s.alpha_to_coverage=s.logic_operation=Toggle::disabled;
    s.blending=Toggle::enabled;s.source_rgb=s.source_alpha=BlendFactor::source_alpha;
    s.destination_rgb=s.destination_alpha=BlendFactor::inverse_source_alpha;
    s.rgb_operation=s.alpha_operation=BlendOperation::add;
    s.cull=Cull::back;s.winding=Winding::clockwise;
    s.min_filter=s.mag_filter=s.mip_filter=TextureFilter::linear;
    s.address_u=s.address_v=Address::wrap;s.address_w=Address::clamp;
    s.normalized_coordinates=s.anisotropy=s.identity_view=Toggle::enabled;
    s.min_lod=0;s.max_lod=1000;s.lod_bias=0;s.max_anisotropy=4;
    return s;
}
UiSnapshot view(const Packet& p) {
    UiSnapshot v;
    v.state=eligible();v.width=p.width;v.height=p.height;v.viewport_width=p.viewport_width;v.viewport_height=p.viewport_height;
    v.scissor=p.scissor;v.samples=p.samples;v.vertex_address=p.vertex_address;v.vertex_bytes=p.vertex_bytes;
    v.vertex_spirv=p.vertex_spirv;v.pixel_spirv=p.pixel_spirv;v.indices=p.indices;v.texture_mips=p.texture_mips;
    for(unsigned i=0;i<5;++i)v.uniforms[i]=p.uniforms[i];return v;
}
Packet fixture() {
    Packet p;p.width=p.height=p.viewport_width=p.viewport_height=8;p.scissor={0,0,8,8};
    p.vertex_bytes.resize(24);p.vertex_spirv=p.pixel_spirv={0x07230203u,0x10000u,0u,1u,0u};
    for(auto& u:p.uniforms)u.resize(16);
    p.indices={0,0,0};p.texture_mips={{4,4,std::vector<uint8_t>(8)}};return p;
}
}
int main(int argc,char** argv) {
    try {
        auto p=fixture();auto v=view(p);const auto owned=make_ui_packet(v);
        p.vertex_bytes[0]=123;p.uniforms[0][0]=45;p.texture_mips[0].bytes[0]=67;
        check(!owned.vertex_bytes[0] && !owned.uniforms[0][0] && !owned.texture_mips[0].bytes[0],"Borrowed input escaped into packet storage");
        check(encode(decode(encode(owned)))==encode(owned),"Frontend serialization round trip differs");
        v.state=UiState{};rejects<UnsupportedDraw>([&]{make_ui_packet(v);});v.state=eligible();
        for(auto member:{&UiState::zero_initial_target,&UiState::depth_test,&UiState::depth_write,&UiState::stencil,
                         &UiState::alpha_to_coverage,&UiState::logic_operation,&UiState::blending,
                         &UiState::normalized_coordinates,&UiState::anisotropy,&UiState::identity_view}) {
            const auto saved=v.state.*member;v.state.*member=Toggle::unknown;
            rejects<UnsupportedDraw>([&]{make_ui_packet(v);});v.state.*member=saved;
        }
        v.state.zero_initial_target=Toggle::disabled;
        check(encode(make_ui_packet(v))==encode(make_ui_packet(view(p))),"Preserved-target UI snapshot rejected or altered packet bytes");
        v.state=eligible();
        v.state.depth_write=Toggle::enabled;rejects<UnsupportedDraw>([&]{make_ui_packet(v);});v.state=eligible();
        v.state.destination_alpha=BlendFactor::source_alpha;rejects<UnsupportedDraw>([&]{make_ui_packet(v);});v.state=eligible();
        v.state.lod_bias=std::numeric_limits<float>::quiet_NaN();rejects<UnsupportedDraw>([&]{make_ui_packet(v);});v.state=eligible();
        v.samples=8;rejects<std::invalid_argument>([&]{make_ui_packet(v);});v.samples=4;
        v.vertex_address=0xfffffffcu;rejects<std::invalid_argument>([&]{make_ui_packet(v);});
        std::cout<<"Native UI frontend state gating, bounds, resource ownership and serialization: passed\n";
        if(argc==2) {
            const auto size=std::filesystem::file_size(argv[1]);check(size<=(32<<20),"Packet exceeds test bound");
            std::vector<uint8_t> bytes(size);std::ifstream file(argv[1],std::ios::binary);
            file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size));check(bool(file),"Packet read failed");
            const auto captured=decode(bytes);const auto rebuilt=make_ui_packet(view(captured));
            check(encode(rebuilt)==bytes,"Native resource-view builder differs from captured packet");
            std::cout<<"Captured packet rebuilt byte-for-byte from native resource views: passed\n";
        }
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
