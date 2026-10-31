#include "render/guest_draw.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace blur::render::guest;
namespace {
void rejects(std::function<void()> action) {
    try { action(); } catch(const std::invalid_argument&) {return;}
    throw std::runtime_error("Invalid guest packet accepted");
}
void u32(std::vector<std::uint8_t>& bytes,std::uint32_t v) { for(int i=0;i<4;++i) bytes.push_back(std::uint8_t(v>>(i*8))); }
void blob(std::vector<std::uint8_t>& bytes,const std::vector<std::uint8_t>& data) {
    u32(bytes,std::uint32_t(data.size())); bytes.insert(bytes.end(),data.begin(),data.end());
}
std::vector<std::uint8_t> fixture() {
    std::vector<std::uint8_t> bytes;
    for(auto n:{0x31475042u,1u,8u,8u,8u,8u,0u,0u,8u,8u,4u,0u}) u32(bytes,n);
    blob(bytes,std::vector<std::uint8_t>(24));
    for(int i=0;i<2;++i) { u32(bytes,20); for(auto n:{0x07230203u,0x10000u,0u,1u,0u}) u32(bytes,n); }
    for(int i=0;i<5;++i) blob(bytes,std::vector<std::uint8_t>(16));
    u32(bytes,3); for(int i=0;i<3;++i) u32(bytes,0);
    u32(bytes,1); u32(bytes,4); u32(bytes,4); blob(bytes,std::vector<std::uint8_t>(8));
    return bytes;
}
std::vector<std::uint8_t> composite_fixture() {
    CompositePacket p;
    p.width=8; p.height=8; p.viewport_width=8; p.viewport_height=8;
    p.scissor={0,0,8,8}; p.samples=1; p.vertex_address=0; p.vertex_count=3;
    p.vertex_bytes=std::vector<std::uint8_t>(3*16, 0);
    p.vertex_spirv={0x07230203u,0x10000u,0u,1u,0u};
    p.pixel_spirv={0x07230203u,0x10000u,0u,1u,0u};
    for(auto& u:p.uniforms) u=std::vector<std::uint8_t>(16, 0);
    p.textures[0]={4,4,std::vector<std::uint8_t>(4*4*4, 0)};
    p.textures[1]={2,2,std::vector<std::uint8_t>(2*2*4, 0)};
    return encode_composite(p);
}
}
int main() {
    try {
        const auto bytes=fixture(); auto packet=decode(bytes);
        if(encode(packet)!=bytes) throw std::runtime_error("Native packet serialization differs from fixture");
        if(packet.width!=8 || packet.samples!=4 || packet.texture_mips.size()!=1) throw std::runtime_error("Decoded fixture differs");
        for(std::size_t i=0;i<bytes.size();++i) rejects([&]{decode(std::span(bytes).first(i));});
        auto trailing=bytes; trailing.push_back(0); rejects([&]{decode(trailing);});
        auto bad=bytes; bad[0]=0; rejects([&]{decode(bad);});
        bad=bytes; bad[4]=2; rejects([&]{decode(bad);});
        packet.indices[0]=1; rejects([&]{validate(packet);}); packet.indices[0]=0;
        packet.vertex_address=0xfffffffcu; rejects([&]{validate(packet);}); packet.vertex_address=0;
        packet.samples=8; rejects([&]{validate(packet);}); packet.samples=4;
        packet.scissor={7,0,2,8}; rejects([&]{validate(packet);}); packet.scissor={0,0,8,8};
        packet.texture_mips[0].bytes.pop_back(); rejects([&]{validate(packet);});

        const auto comp_bytes=composite_fixture();
        auto comp_packet=decode_composite(comp_bytes);
        if(encode_composite(comp_packet)!=comp_bytes) throw std::runtime_error("Composite packet serialization differs");
        if(comp_packet.width!=8 || comp_packet.samples!=1 || comp_packet.vertex_count!=3)
            throw std::runtime_error("Decoded composite packet differs");
        for(std::size_t i=0;i<comp_bytes.size();++i) rejects([&]{decode_composite(std::span(comp_bytes).first(i));});
        auto comp_trailing=comp_bytes; comp_trailing.push_back(0); rejects([&]{decode_composite(comp_trailing);});
        auto comp_bad=comp_bytes; comp_bad[0]=0; rejects([&]{decode_composite(comp_bad);});
        comp_bad=comp_bytes; comp_bad[4]=2; rejects([&]{decode_composite(comp_bad);});
        comp_packet.samples=4; rejects([&]{validate_composite(comp_packet);}); comp_packet.samples=1;
        comp_packet.vertex_count=4; rejects([&]{validate_composite(comp_packet);}); comp_packet.vertex_count=3;
        comp_packet.textures[0].rgba8_bytes.pop_back(); rejects([&]{validate_composite(comp_packet);});

        std::cout<<"Guest packet synthetic round-trip, all truncations, bounds and geometry validation: passed\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
