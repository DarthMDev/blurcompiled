#include "rex_plume_state.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace blur::render::guest;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
template<class E,class F> void rejects(F action) {
    try {action();}catch(const E&){return;}throw std::runtime_error("Unsupported SDK state was accepted");
}
constexpr unsigned color_mask=0x2104,depth=0x2200,blend=0x2201,color=0x2202,raster=0x2205,mode=0x2208;
std::vector<uint32_t> fixture() {
    std::vector<uint32_t> words(rex_register_word_count);
    words[color_mask]=15;words[mode]=4;words[depth]=4;
    words[blend]=0x07060706;words[color]=0;words[raster]=6;
    return words;
}
void verify_ui_register_fields(const UiState& s) {
    check(s.depth_test==Toggle::disabled && s.depth_write==Toggle::disabled && s.stencil==Toggle::disabled,"Effective UI depth/stencil state differs");
    check(s.alpha_to_coverage==Toggle::disabled && s.cull==Cull::back && s.winding==Winding::clockwise,"UI coverage/face state differs");
    check(s.blending==Toggle::enabled && s.source_rgb==BlendFactor::source_alpha && s.source_alpha==BlendFactor::source_alpha &&
          s.destination_rgb==BlendFactor::inverse_source_alpha && s.destination_alpha==BlendFactor::inverse_source_alpha &&
          s.rgb_operation==BlendOperation::add && s.alpha_operation==BlendOperation::add && s.color_write_mask==15,"UI blending/write mask differs");
}
}
int main(int argc,char** argv) {
    try {
        auto words=fixture();auto s=map_rex_ui_register_state(words,{});verify_ui_register_fields(s);
        check(s.target_format==ColorFormat::unknown && s.zero_initial_target==Toggle::unknown &&
              s.logic_operation==Toggle::unknown && s.min_filter==TextureFilter::unknown,"Mapper invented resource-dependent state");
        UiState resolved;resolved.target_format=ColorFormat::rgba8_unorm;resolved.identity_view=Toggle::enabled;
        resolved.depth_test=resolved.depth_write=Toggle::enabled;
        s=map_rex_ui_register_state(words,resolved);verify_ui_register_fields(s);
        check(s.target_format==ColorFormat::rgba8_unorm && s.identity_view==Toggle::enabled,"Mapper lost supplied resolved resource state");
        words[depth]=6;s=map_rex_ui_register_state(words,{});
        check(s.depth_test==Toggle::enabled && s.depth_write==Toggle::enabled,"Enabled depth write was lost");
        words[depth]=2|(7u<<4);s=map_rex_ui_register_state(words,{});
        check(s.depth_test==Toggle::disabled && s.depth_write==Toggle::disabled,"Redundant always-pass depth normalization differs");
        words[depth]=1;s=map_rex_ui_register_state(words,{});check(s.stencil==Toggle::enabled,"Stencil request lost");
        words=fixture();words[color]=16;s=map_rex_ui_register_state(words,{});check(s.alpha_to_coverage==Toggle::enabled,"Alpha coverage request lost");
        words=fixture();words[raster]=3;s=map_rex_ui_register_state(words,{});check(s.cull==Cull::unknown,"Unsupported cull state fabricated");
        words=fixture();words[raster]|=1u<<11;rejects<UnsupportedDraw>([&]{map_rex_ui_register_state(words,{});});
        words=fixture();words[color_mask]|=0xf0;rejects<UnsupportedDraw>([&]{map_rex_ui_register_state(words,{});});
        words=fixture();words[mode]=6;rejects<UnsupportedDraw>([&]{map_rex_ui_register_state(words,{});});
        words=fixture();words[blend]=0x00010001;s=map_rex_ui_register_state(words,{});check(s.blending==Toggle::disabled,"Copy blending normalization differs");
        // Composite premultiplied alpha blend (src=one, dst=inverse_source_alpha), Cull::none, and RGB write mask (7).
        words=fixture();words[color_mask]=7;words[blend]=0x07010701;words[raster]=4;
        s=map_rex_ui_register_state(words,{});
        check(s.blending==Toggle::enabled && s.source_rgb==BlendFactor::one && s.source_alpha==BlendFactor::one &&
              s.destination_rgb==BlendFactor::inverse_source_alpha && s.destination_alpha==BlendFactor::inverse_source_alpha &&
              s.cull==Cull::none && s.winding==Winding::clockwise && s.color_write_mask==7,
              "Composite premultiplied blend, Cull::none or RGB write mask mapping differs");
        rejects<std::invalid_argument>([&]{map_rex_ui_register_state(std::span(words).first(2),{});});
        std::cout<<"Pinned SDK state mapping: synthetic effective depth, blend, raster, bounds and unknown-state checks passed\n";
        if(argc==2) {
            const auto size=std::filesystem::file_size(argv[1]);check(size==rex_register_word_count*4,"Captured register extent differs");
            std::vector<uint8_t> bytes(size);std::ifstream input(argv[1],std::ios::binary);
            input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size));check(bool(input),"Captured register read failed");
            for(std::size_t i=0;i<words.size();++i)words[i]=uint32_t(bytes[i*4])|uint32_t(bytes[i*4+1])<<8|uint32_t(bytes[i*4+2])<<16|uint32_t(bytes[i*4+3])<<24;
            s=map_rex_ui_register_state(words,{});verify_ui_register_fields(s);
            std::cout<<"Captured run5 UI register state matches the recorded effective pipeline; resource eligibility remains unknown\n";
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
