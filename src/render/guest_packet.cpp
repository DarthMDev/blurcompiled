#include "render/guest_draw.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace blur::render::guest {
namespace {
constexpr std::uint64_t shared_size = 512ull * 1024 * 1024;
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t offset = 0;
    std::uint32_t u32() {
        if (bytes.size() - offset < 4) throw std::invalid_argument("Truncated BPG1 word");
        auto* p = bytes.data() + offset; offset += 4;
        return std::uint32_t(p[0]) | std::uint32_t(p[1])<<8 | std::uint32_t(p[2])<<16 | std::uint32_t(p[3])<<24;
    }
    std::vector<std::uint8_t> blob(std::uint32_t size, std::uint32_t limit) {
        if (size > limit || size > bytes.size() - offset) throw std::invalid_argument("Truncated/oversized BPG1 blob");
        auto begin = bytes.begin() + offset; offset += size;
        return {begin, begin + size};
    }
    std::vector<std::uint32_t> spirv() {
        auto size = u32();
        if (!size || size > (1<<20) || size % 4 || size > bytes.size()-offset)
            throw std::invalid_argument("Invalid SPIR-V length");
        std::vector<std::uint32_t> words;
        words.reserve(size/4);
        while (size) { words.push_back(u32()); size -= 4; }
        return words;
    }
};
void require(bool condition, const char* message) { if (!condition) throw std::invalid_argument(message); }
}
void validate(const Packet& p) {
    require(p.width && p.height && p.width<=2048 && p.height<=2048, "Guest target must be 1..2048");
    require(p.viewport_width && p.viewport_height && p.viewport_width<=8192 && p.viewport_height<=8192, "Invalid guest viewport");
    const auto& s=p.scissor;
    require(s[2] && s[3] && s[0]<p.width && s[1]<p.height && s[2]<=p.width-s[0] && s[3]<=p.height-s[1], "Invalid guest scissor");
    require(p.samples==1 || p.samples==4, "Only 1x/4x targets supported");
    require(!p.vertex_bytes.empty() && p.vertex_bytes.size()<=(1<<20) && p.vertex_address%4==0 &&
            std::uint64_t(p.vertex_address)+p.vertex_bytes.size()<=shared_size, "Invalid physical vertex span");
    require(p.vertex_spirv.size()>=5 && p.vertex_spirv.size()<=(1<<18) && p.vertex_spirv[0]==0x07230203 &&
            p.pixel_spirv.size()>=5 && p.pixel_spirv.size()<=(1<<18) && p.pixel_spirv[0]==0x07230203, "Invalid SPIR-V module");
    for (const auto& u:p.uniforms) require(!u.empty() && u.size()<=4096 && u.size()%16==0,"Invalid guest uniform block");
    require(!p.indices.empty() && p.indices.size()<=65536 && p.indices.size()%3==0,"Invalid triangle index count");
    require(p.vertex_bytes.size()%24==0,"Expected 24-byte UI vertices");
    for (auto index:p.indices) require(index<p.vertex_bytes.size()/24,"UI index exceeds captured vertex span");
    require(!p.texture_mips.empty() && p.texture_mips.size()<=12,"Invalid mip count");
    auto width=p.texture_mips.front().width, height=p.texture_mips.front().height;
    require(width && height && width<=2048 && height<=2048,"Invalid BC1 extent");
    const auto max_mips = std::bit_width(std::max(width,height));
    require(p.texture_mips.size()<=max_mips,"Too many texture mips");
    for (const auto& mip:p.texture_mips) {
        require(mip.width==width && mip.height==height && mip.bytes.size()==std::size_t((width+3)/4)*((height+3)/4)*8,
                "Invalid BC1 mip extent/size");
        width=std::max(1u,width/2); height=std::max(1u,height/2);
    }
}
void validate_composite(const CompositePacket& p) {
    require(p.width && p.height && p.width<=2048 && p.height<=2048, "Guest composite target must be 1..2048");
    require(p.viewport_width && p.viewport_height && p.viewport_width<=8192 && p.viewport_height<=8192, "Invalid composite viewport");
    const auto& s=p.scissor;
    require(s[2] && s[3] && s[0]<p.width && s[1]<p.height && s[2]<=p.width-s[0] && s[3]<=p.height-s[1], "Invalid composite scissor");
    require(p.samples==1, "Composite pass requires a 1x target");
    require(p.vertex_count>=3 && p.vertex_count%3==0 && p.vertex_count<=65535, "Invalid composite triangle vertex count");
    require(!p.vertex_bytes.empty() && p.vertex_bytes.size()==std::size_t(p.vertex_count)*16 &&
            p.vertex_address%4==0 && std::uint64_t(p.vertex_address)+p.vertex_bytes.size()<=shared_size,
            "Invalid physical composite vertex span");
    require(p.vertex_spirv.size()>=5 && p.vertex_spirv.size()<=(1<<18) && p.vertex_spirv[0]==0x07230203 &&
            p.pixel_spirv.size()>=5 && p.pixel_spirv.size()<=(1<<18) && p.pixel_spirv[0]==0x07230203,
            "Invalid composite SPIR-V module");
    for (const auto& u:p.uniforms) require(!u.empty() && u.size()<=4096 && u.size()%16==0, "Invalid composite uniform block");
    for (const auto& t:p.textures) {
        require(t.width && t.height && t.width<=2048 && t.height<=2048 &&
                t.rgba8_bytes.size()==std::size_t(t.width)*t.height*4,
                "Invalid composite RGBA8 texture extent/size");
    }
}
Packet decode(std::span<const std::uint8_t> bytes) {
    require(bytes.size()<=(32<<20), "BPG1 packet exceeds 32 MiB");
    Reader r{bytes};
    require(r.u32()==0x31475042 && r.u32()==1,"Unknown BPG1 magic/version");
    Packet p;
    p.width=r.u32(); p.height=r.u32(); p.viewport_width=r.u32(); p.viewport_height=r.u32();
    for (auto& v:p.scissor) v=r.u32();
    p.samples=r.u32(); p.vertex_address=r.u32();
    p.vertex_bytes=r.blob(r.u32(),1<<20);
    p.vertex_spirv=r.spirv(); p.pixel_spirv=r.spirv();
    for (auto& u:p.uniforms) u=r.blob(r.u32(),4096);
    auto count=r.u32();
    require(count<=65536 && std::uint64_t(count)*4<=bytes.size()-r.offset,"Invalid BPG1 index count");
    for (std::uint32_t i=0;i<count;++i) {
        auto index=r.u32(); require(index<=65535,"BPG1 index exceeds 16 bits"); p.indices.push_back(std::uint16_t(index));
    }
    auto levels=r.u32(); require(levels<=12,"Invalid BPG1 mip count");
    for (std::uint32_t i=0;i<levels;++i) {
        const auto w=r.u32(), h=r.u32(); auto data=r.blob(r.u32(),4<<20);
        p.texture_mips.push_back({w,h,std::move(data)});
    }
    require(r.offset==bytes.size(),"Trailing BPG1 data");
    validate(p);
    return p;
}
std::vector<std::uint8_t> encode(const Packet& p) {
    validate(p);
    std::vector<std::uint8_t> bytes;
    auto u32 = [&](std::uint32_t value) {
        for (unsigned i=0;i<4;++i) bytes.push_back(std::uint8_t(value>>(i*8)));
    };
    auto blob = [&](std::span<const std::uint8_t> source) {
        u32(std::uint32_t(source.size())); bytes.insert(bytes.end(),source.begin(),source.end());
    };
    u32(0x31475042); u32(1);
    u32(p.width); u32(p.height); u32(p.viewport_width); u32(p.viewport_height);
    for (auto value:p.scissor) u32(value);
    u32(p.samples); u32(p.vertex_address); blob(p.vertex_bytes);
    for (const auto* module:{&p.vertex_spirv,&p.pixel_spirv}) {
        u32(std::uint32_t(module->size()*4)); for (auto value:*module) u32(value);
    }
    for (const auto& uniform:p.uniforms) blob(uniform);
    u32(std::uint32_t(p.indices.size())); for (auto index:p.indices) u32(index);
    u32(std::uint32_t(p.texture_mips.size()));
    for (const auto& mip:p.texture_mips) { u32(mip.width); u32(mip.height); blob(mip.bytes); }
    if (bytes.size()>(32<<20)) throw std::invalid_argument("Encoded BPG1 packet exceeds 32 MiB");
    return bytes;
}
CompositePacket decode_composite(std::span<const std::uint8_t> bytes) {
    require(bytes.size()<=(32<<20), "BPG2 composite packet exceeds 32 MiB");
    Reader r{bytes};
    require(r.u32()==0x32475042 && r.u32()==1,"Unknown BPG2 magic/version");
    CompositePacket p;
    p.width=r.u32(); p.height=r.u32(); p.viewport_width=r.u32(); p.viewport_height=r.u32();
    for (auto& v:p.scissor) v=r.u32();
    p.samples=r.u32(); p.vertex_address=r.u32(); p.vertex_count=r.u32();
    p.vertex_bytes=r.blob(r.u32(),1<<20);
    p.vertex_spirv=r.spirv(); p.pixel_spirv=r.spirv();
    for (auto& u:p.uniforms) u=r.blob(r.u32(),4096);
    auto tex_count=r.u32();
    require(tex_count==2,"Composite packet requires exactly 2 textures");
    for (std::uint32_t i=0;i<2;++i) {
        p.textures[i].width=r.u32(); p.textures[i].height=r.u32();
        p.textures[i].rgba8_bytes=r.blob(r.u32(),16<<20);
    }
    require(r.offset==bytes.size(),"Trailing BPG2 data");
    validate_composite(p);
    return p;
}
std::vector<std::uint8_t> encode_composite(const CompositePacket& p) {
    validate_composite(p);
    std::vector<std::uint8_t> bytes;
    auto u32 = [&](std::uint32_t value) {
        for (unsigned i=0;i<4;++i) bytes.push_back(std::uint8_t(value>>(i*8)));
    };
    auto blob = [&](std::span<const std::uint8_t> source) {
        u32(std::uint32_t(source.size())); bytes.insert(bytes.end(),source.begin(),source.end());
    };
    u32(0x32475042); u32(1);
    u32(p.width); u32(p.height); u32(p.viewport_width); u32(p.viewport_height);
    for (auto value:p.scissor) u32(value);
    u32(p.samples); u32(p.vertex_address); u32(p.vertex_count); blob(p.vertex_bytes);
    for (const auto* module:{&p.vertex_spirv,&p.pixel_spirv}) {
        u32(std::uint32_t(module->size()*4)); for (auto value:*module) u32(value);
    }
    for (const auto& uniform:p.uniforms) blob(uniform);
    u32(2);
    for (const auto& tex:p.textures) {
        u32(tex.width); u32(tex.height); blob(tex.rgba8_bytes);
    }
    if (bytes.size()>(32<<20)) throw std::invalid_argument("Encoded BPG2 composite packet exceeds 32 MiB");
    return bytes;
}
}
