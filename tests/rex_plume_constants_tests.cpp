#include "rex_plume_constants.hpp"
#include <fstream>
#include <iostream>

using namespace blur::render::guest;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
template<class E,class F> void rejects(F action) {
    try {action();}catch(const E&){return;}throw std::runtime_error("Invalid constant input accepted");
}
std::uint32_t word(const std::vector<std::uint8_t>& bytes,std::size_t i) {
    const auto n=i*4;return std::uint32_t(bytes.at(n))|std::uint32_t(bytes.at(n+1))<<8|
        std::uint32_t(bytes.at(n+2))<<16|std::uint32_t(bytes.at(n+3))<<24;
}
std::vector<std::uint8_t> read(const char* path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    check(bool(input),"Input open failed");const auto size=input.tellg();
    check(size>0 && size<(32<<20),"Invalid input size");std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
    input.seekg(0);input.read(reinterpret_cast<char*>(out.data()),size);check(bool(input),"Input read failed");return out;
}
}
int main(int argc,char** argv) {
    try {
        // A zeroed updater buffer must receive live limits, preserving every
        // other byte. Reusing the buffer must replace stale limits as well.
        {
            std::vector<std::uint32_t> regs(rex_register_word_count);
            std::vector<std::uint8_t> sys(512,0x5a);
            regs[0x2101]=0x01020304; regs[0x2100]=0x00fedcba;
            complete_rex_spirv_index_limits(regs,sys);
            check(word(sys,124)==0x01020304 && word(sys,125)==0x00fedcba,
                  "SPIR-V vertex index limits differ from current registers");
            for (std::size_t i=0;i<sys.size();++i)
                if (i<496 || i>=504) check(sys[i]==0x5a,"Unrelated system constant changed");
            regs[0x2101]=0; regs[0x2100]=107;
            complete_rex_spirv_index_limits(regs,sys);
            check(word(sys,124)==0 && word(sys,125)==107,"Stale SPIR-V index limits retained");
            rejects<std::invalid_argument>([&]{complete_rex_spirv_index_limits(std::span(regs).first(2),sys);});
            rejects<std::invalid_argument>([&]{complete_rex_spirv_index_limits(regs,std::span(sys).first(500));});
        }
        // Distinct float4s at every bitmap-bank edge; register values are bit
        // patterns (including NaNs), never float conversions or guest pointers.
        std::vector<std::uint32_t> words(rex_register_word_count,0xdeadbeef);
        std::array<std::uint8_t,512> system{};system[0]=37;system[511]=255;
        RexFloatConstantMap vs{{(1ull<<63)|1,1,0,1ull<<63},4,false};
        RexFloatConstantMap ps{{2,(1ull<<63)|2,(1ull<<63)|1,1ull<<62},6,false};
        constexpr std::array<unsigned,4> v_indices{0,63,64,255};
        constexpr std::array<unsigned,6> p_indices{1,65,127,128,191,254};
        for(unsigned i=0;i<4;++i)for(unsigned c=0;c<4;++c)words[0x4000+v_indices[i]*4+c]=0x7f800001+i*4+c;
        for(unsigned i=0;i<6;++i)for(unsigned c=0;c<4;++c)words[0x4400+p_indices[i]*4+c]=0xff800001+i*4+c;
        for(unsigned i=0;i<40;++i)words[0x4900+i]=0x01020300+i;
        for(unsigned i=0;i<192;++i)words[0x4800+i]=0x80706000+i;
        auto blocks=pack_rex_ui_constants(words,vs,ps,system);
        check(blocks[0].size()==512 && blocks[1].size()==64 && blocks[2].size()==96 &&
              blocks[3].size()==160 && blocks[4].size()==768,"Constant block extents differ");
        for(unsigned i=0;i<16;++i)check(word(blocks[1],i)==0x7f800001+i,"Vertex float4 order/bits differ");
        for(unsigned i=0;i<24;++i)check(word(blocks[2],i)==0xff800001+i,"Pixel float4 stage/order/bits differ");
        for(unsigned i=0;i<40;++i)check(word(blocks[3],i)==0x01020300+i,"Bool/loop register order differs");
        for(unsigned i=0;i<192;++i)check(word(blocks[4],i)==0x80706000+i,"Fetch register order differs");
        check(blocks[4][0]==0 && blocks[4][1]==0x60 && blocks[4][2]==0x70 && blocks[4][3]==0x80,"Constants are not little endian");
        words[0x4000]=0;system[0]=0;check(blocks[0][0]==37 && word(blocks[1],0)==0x7f800001,"Borrowed state escaped into owned blocks");
        auto bad=vs;bad.count=3;rejects<UnsupportedDraw>([&]{pack_rex_ui_constants(words,bad,ps,system);});
        bad=vs;bad.dynamic_addressing=true;rejects<UnsupportedDraw>([&]{pack_rex_ui_constants(words,bad,ps,system);});
        bad=vs;bad.bitmap[2]=1;bad.count=5;rejects<UnsupportedDraw>([&]{pack_rex_ui_constants(words,bad,ps,system);});
        rejects<UnsupportedDraw>([&]{pack_rex_ui_constants(words,ps,vs,system);});
        rejects<std::invalid_argument>([&]{pack_rex_ui_constants(std::span(words).first(4),vs,ps,system);});
        rejects<std::invalid_argument>([&]{pack_rex_ui_constants(words,vs,ps,std::span(system).first(511));});
        std::array<std::uint8_t,20> invalid{};
        check(!supported_ui_shader_pair({}, {}) && !supported_ui_shader_pair(invalid,invalid),"Unknown shader pair accepted");
        if(argc==3) {
            auto packet=decode(read(argv[1]));const auto registers=read(argv[2]);
            auto bytes=[](const auto& v){return std::span(reinterpret_cast<const std::uint8_t*>(v.data()),v.size()*4);};
            check(supported_ui_shader_pair(bytes(packet.vertex_spirv),bytes(packet.pixel_spirv)),"Recorded UI modules rejected");
            packet.pixel_spirv.back()^=1;
            check(!supported_ui_shader_pair(bytes(packet.vertex_spirv),bytes(packet.pixel_spirv)),"Changed UI module accepted");
            check(registers.size()==rex_register_word_count*4,"Captured register extent differs");
            for(std::size_t i=0;i<words.size();++i)words[i]=word(registers,i);
            blocks=pack_rex_ui_constants(words,vs,ps,packet.uniforms[0]);
            check(blocks[3]==packet.uniforms[3] && blocks[4]==packet.uniforms[4],"Captured bool/loop/fetch bytes differ");
            std::cout<<"Recorded UI shader identity and current-register bool/loop/fetch bytes match; captured float maps not tested\n";
        }
        std::cout<<"Synthetic constant packing: stage banks, bit preservation, bounds, ownership and unsupported maps passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
