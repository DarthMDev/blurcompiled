#include "rex_plume_constants.hpp"
#include <rex/graphics/register_file.h>
#include <bit>

namespace blur::render::guest {
namespace {
namespace gpu=rex::graphics;
void append_word(std::vector<std::uint8_t>& out,std::uint32_t word) {
    for(unsigned i=0;i<4;++i)out.push_back(std::uint8_t(word>>(i*8)));
}
std::vector<std::uint8_t> copy_words(std::span<const std::uint32_t> words) {
    std::vector<std::uint8_t> bytes;bytes.reserve(words.size()*4);
    for(auto word:words)append_word(bytes,word);
    return bytes;
}
std::vector<std::uint8_t> floats(std::span<const std::uint32_t> words,
    const RexFloatConstantMap& map,std::size_t base,unsigned required) {
    unsigned count=0;
    for(auto bits:map.bitmap)count+=std::popcount(bits);
    if(map.dynamic_addressing || count!=map.count || count!=required)
        throw UnsupportedDraw("Current analyzed float map differs from the bounded UI layout");
    if(!count)return std::vector<std::uint8_t>(16,0);
    std::vector<std::uint8_t> bytes;bytes.reserve(count*16);
    for(unsigned bank=0;bank<4;++bank) {
        auto bits=map.bitmap[bank];
        while(bits) {
            const auto index=std::countr_zero(bits);bits&=bits-1;
            const auto offset=base+bank*256+index*4;
            for(auto word:words.subspan(offset,4))append_word(bytes,word);
        }
    }
    return bytes;
}
}
void complete_rex_spirv_index_limits(std::span<const std::uint32_t> words,
                                     std::span<std::uint8_t> system) {
    if (words.size() != rex_register_word_count || system.size() != 512)
        throw std::invalid_argument("Current register/system constant extent differs from the pinned SDK");
    // Match the pinned Vulkan SPIR-V updater's current raw register values.
    const std::array<std::uint32_t,2> limits{
        words[gpu::XE_GPU_REG_VGT_MIN_VTX_INDX],
        words[gpu::XE_GPU_REG_VGT_MAX_VTX_INDX]};
    for (unsigned i=0; i<2; ++i)
        for (unsigned byte=0; byte<4; ++byte)
            system[496+i*4+byte]=std::uint8_t(limits[i]>>(byte*8));
}
UiConstantBlocks pack_rex_ui_constants(std::span<const std::uint32_t> words,
    const RexFloatConstantMap& vertex,const RexFloatConstantMap& pixel,
    std::span<const std::uint8_t> system,
    unsigned required_vs_floats,unsigned required_ps_floats) {
    if(words.size()!=rex_register_word_count || system.size()!=512)
        throw std::invalid_argument("Current register/system constant extent differs from the pinned SDK");
    UiConstantBlocks out;
    out[1]=floats(words,vertex,gpu::XE_GPU_REG_SHADER_CONSTANT_000_X,required_vs_floats);
    out[2]=floats(words,pixel,gpu::XE_GPU_REG_SHADER_CONSTANT_256_X,required_ps_floats);
    out[3]=copy_words(words.subspan(gpu::XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031,40));
    out[4]=copy_words(words.subspan(gpu::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0,192));
    out[0].assign(system.begin(),system.end());
    return out;
}
}
