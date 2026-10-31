#include "rex_plume_texture.hpp"
#include <iostream>

using namespace blur::render::guest;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class E,class F> void rejects(F action) {
    try{action();}catch(const E&){return;}throw std::runtime_error("Unsupported texture accepted");
}
struct Fixture {
    std::vector<std::uint32_t> words=std::vector<std::uint32_t>(rex_register_word_count);
    RexTextureView view{RexNativeTextureFormat::bc1_unorm,RexNativeTextureKind::array_2d,
        Toggle::enabled,0x688,8,4,1,0,4,4096,8192};
    std::vector<TextureMip> mips{{8,4,std::vector<std::uint8_t>(16)},
        {4,2,std::vector<std::uint8_t>(8)},{2,1,std::vector<std::uint8_t>(8)},{1,1,std::vector<std::uint8_t>(8)}};
    Fixture(){
        words[0x4800]=2;words[0x4801]=18|(1u<<12);words[0x4802]=7|(3u<<13);
        words[0x4803]=0x688<<1;words[0x4804]=3u<<6;words[0x4805]=(1u<<9)|(2u<<12);
    }
    UiState map(){return map_rex_ui_texture_state(words,0,view,mips,{});}
};
}
int main() {
    try {
        auto f=Fixture{};const auto state=f.map();
        check(state.texture_format==TextureFormat::bc1_unorm_array && state.identity_view==Toggle::enabled,"Native texture state was not mapped");
        check(state.min_filter==TextureFilter::unknown && state.zero_initial_target==Toggle::unknown &&
              state.target_format==ColorFormat::unknown,"Texture mapper invented sampler/attachment state");
        UiState supplied;supplied.zero_initial_target=Toggle::enabled;supplied.max_anisotropy=4;
        const auto preserved=map_rex_ui_texture_state(f.words,0,f.view,f.mips,supplied);
        check(preserved.zero_initial_target==Toggle::enabled && preserved.max_anisotropy==4,"Unrelated resource fields lost");
        // Upper fetch-page bits alias the SDK's 512 MiB physical aperture.
        f.words[0x4801]|=0x20000u<<12;f.words[0x4805]|=0x20000u<<12;f.map();
        f=Fixture{};f.words[0x4804]=15u<<6;f.map(); // effective mip max clamps to dimensions
        for(auto index:{0x4800u,0x4801u,0x4802u,0x4803u,0x4804u,0x4805u}) {
            f=Fixture{};f.words[index]=0;rejects<UnsupportedDraw>([&]{f.map();});
        }
        for(auto [index,mask]:{std::pair{0x4800u,1u<<2},std::pair{0x4801u,1u<<10},
                              std::pair{0x4803u,1u},std::pair{0x4803u,1u<<13},
                              std::pair{0x4805u,1u<<2}}) {
            f=Fixture{};f.words[index]|=mask;rejects<UnsupportedDraw>([&]{f.map();});
        }
        f=Fixture{};f.words[0x4802]=2048;rejects<UnsupportedDraw>([&]{f.map();});
        f=Fixture{};f.view={};rejects<UnsupportedDraw>([&]{f.map();});
        for(auto member:{&RexTextureView::width,&RexTextureView::height,&RexTextureView::layers,
                         &RexTextureView::first_mip,&RexTextureView::mip_count,&RexTextureView::source_base,&RexTextureView::source_mips}) {
            f=Fixture{};++(f.view.*member);rejects<UnsupportedDraw>([&]{f.map();});
        }
        f=Fixture{};f.view.identity_components=Toggle::unknown;rejects<UnsupportedDraw>([&]{f.map();});
        f=Fixture{};f.mips.pop_back();rejects<UnsupportedDraw>([&]{f.map();});
        f=Fixture{};f.mips[1].width=3;rejects<UnsupportedDraw>([&]{f.map();});
        f=Fixture{};f.mips[2].bytes.pop_back();rejects<UnsupportedDraw>([&]{f.map();});
        f=Fixture{};rejects<std::invalid_argument>([&]{map_rex_ui_texture_state(f.words,32,f.view,f.mips,{});});
        rejects<std::invalid_argument>([&]{map_rex_ui_texture_state(std::span(f.words).first(2),0,f.view,f.mips,{});});
        // A 1x1 base-only resource must ignore an unused raw mip pointer.
        f=Fixture{};f.words[0x4802]=0;f.view.width=f.view.height=1;f.view.mip_count=1;
        f.view.source_mips=0;f.mips={{1,1,std::vector<std::uint8_t>(8)}};f.map();
        // Sampler state mapping: linear fetch + anisotropic override (default 4x).
        f=Fixture{};f.words[0x4803]=(1u<<19)|(1u<<21)|(1u<<23);
        auto s=map_rex_ui_sampler_state(f.words,RexSamplerBinding{0},{});
        check(s.min_filter==TextureFilter::linear && s.mag_filter==TextureFilter::linear &&
              s.mip_filter==TextureFilter::linear && s.address_u==Address::wrap &&
              s.address_v==Address::wrap && s.address_w==Address::clamp &&
              s.normalized_coordinates==Toggle::enabled && s.anisotropy==Toggle::enabled &&
              s.max_anisotropy==4 && s.min_lod==0 && s.max_lod==1000 && s.lod_bias==0,
              "Sampler state was not mapped from fetch descriptor and anisotropic override");
        check(s.texture_format==TextureFormat::unknown && s.zero_initial_target==Toggle::unknown,
              "Sampler mapper invented texture/attachment state");
        // Clamp-to-edge on U/V and disabled anisotropic override.
        f.words[0x4800]|=(2u<<10)|(2u<<13);
        s=map_rex_ui_sampler_state(f.words,RexSamplerBinding{0,RexSamplerFilter::use_fetch_const,
            RexSamplerFilter::use_fetch_const,RexSamplerFilter::use_fetch_const,
            RexAnisoFilter::use_fetch_const,RexAnisoFilter::disabled},{});
        check(s.address_u==Address::clamp && s.address_v==Address::clamp &&
              s.anisotropy==Toggle::disabled && s.max_anisotropy==1,"Clamp/non-aniso sampler mapping failed");
        // Point filter without anisotropic override leaves TextureFilter::unknown for bounded UI contract.
        f=Fixture{};
        s=map_rex_ui_sampler_state(f.words,RexSamplerBinding{0,RexSamplerFilter::use_fetch_const,
            RexSamplerFilter::use_fetch_const,RexSamplerFilter::use_fetch_const,
            RexAnisoFilter::use_fetch_const,RexAnisoFilter::disabled},{});
        check(s.min_filter==TextureFilter::unknown && s.mag_filter==TextureFilter::unknown,"Point filter unexpectedly mapped to linear");
        // Reject invalid sampler fetch extent/index and unsupported LOD/border modifiers.
        rejects<std::invalid_argument>([&]{map_rex_ui_sampler_state(f.words,RexSamplerBinding{32},{});});
        rejects<std::invalid_argument>([&]{map_rex_ui_sampler_state(std::span(f.words).first(2),RexSamplerBinding{0},{});});
        f=Fixture{};f.words[0x4803]=(1u<<19)|(1u<<21)|(1u<<23);f.words[0x4804]|=1u<<12; // non-zero lod_bias
        rejects<UnsupportedDraw>([&]{map_rex_ui_sampler_state(f.words,RexSamplerBinding{0},{});});
        // Guest BC1 layout ranges and CPU untiling/endian-swap roundtrip.
        f=Fixture{};f.words[0x4800]|=1u<<22; // base pitch = 32 texels
        const auto ranges=rex_ui_texture_guest_ranges(f.words,0);
        check(ranges.base_address==4096 && ranges.mips_address==8192 &&
              ranges.width==8 && ranges.height==4 && ranges.mip_count==4 && !ranges.tiled,
              "Linear BC1 guest layout ranges differ");
        std::vector<std::uint8_t> guest_base(ranges.base_bytes),guest_mips(ranges.mips_bytes);
        guest_base[0]=0x11;guest_base[7]=0x88;
        std::vector<std::vector<std::uint8_t>> decoded_storage;
        const auto decoded=decode_rex_ui_bc1_mips(f.words,0,
            RexGuestTextureData{ranges.base_address,ranges.mips_address,guest_base,guest_mips},
            decoded_storage);
        check(decoded.size()==4 && decoded[0].bytes[0]==0x11 && decoded[0].bytes[7]==0x88,
              "Decoded linear BC1 mip payload differs");
        rejects<UnsupportedDraw>([&]{decode_rex_ui_bc1_mips(f.words,0,
            RexGuestTextureData{ranges.base_address+4,ranges.mips_address,guest_base,guest_mips},
            decoded_storage);});
        // Base-only 2D RGBA8 texture descriptor mapping and 12-bit Xenos component swizzling.
        std::vector<std::uint32_t> rgba_words(rex_register_word_count);
        rgba_words[0x4800]=2u|(2u<<10)|(2u<<13); // kTexture, clamp-to-edge X/Y
        rgba_words[0x4801]=6u|(2u<<6)|(0x10339u<<12); // k_8_8_8_8, 8-in-32 endian, page 0x10339
        rgba_words[0x4802]=3u|(1u<<13); // 4x2
        rgba_words[0x4803]=(0x60au<<1)|(1u<<19)|(1u<<21)|(1u<<23); // BGRA swizzle (0o3012), linear filters
        rgba_words[0x4804]=0u;
        rgba_words[0x4805]=1u<<9; // 2D, no mip page
        RexTextureView rgba_view{RexNativeTextureFormat::rgba8_unorm,RexNativeTextureKind::array_2d,
            Toggle::disabled,0x60au,4,2,1,0,1,0x10339000u,0u};
        std::vector<std::uint8_t> raw_rgba(4*2*4);
        raw_rgba[0]=10;raw_rgba[1]=20;raw_rgba[2]=30;raw_rgba[3]=40;
        const auto rgba_state=map_rex_rgba8_texture_state(rgba_words,0,rgba_view,raw_rgba,{});
        check(rgba_state.texture_format==TextureFormat::rgba8_unorm_array &&
              rgba_state.identity_view==Toggle::disabled,
              "RGBA8 non-identity texture view state was not mapped");
        const auto swizzled=swizzle_rex_rgba8_pixels(raw_rgba,rgba_view.host_swizzle);
        check(swizzled.size()==raw_rgba.size() &&
              swizzled[0]==30 && swizzled[1]==20 && swizzled[2]==10 && swizzled[3]==40,
              "BGRA component swizzle differs");
        const auto rgba_sampler=map_rex_ui_sampler_state(rgba_words,
            RexSamplerBinding{0,RexSamplerFilter::use_fetch_const,RexSamplerFilter::use_fetch_const,
                              RexSamplerFilter::use_fetch_const,RexAnisoFilter::use_fetch_const,
                              RexAnisoFilter::disabled},{});
        check(rgba_sampler.min_filter==TextureFilter::linear &&
              rgba_sampler.mag_filter==TextureFilter::linear &&
              rgba_sampler.mip_filter==TextureFilter::basemap &&
              rgba_sampler.address_u==Address::clamp && rgba_sampler.address_v==Address::clamp &&
              rgba_sampler.anisotropy==Toggle::disabled && rgba_sampler.max_lod==0,
              "Base-only RGBA8 sampler state was not clamped to basemap LOD 0");
        auto bad_rgba_view=rgba_view;bad_rgba_view.host_swizzle=0x688u;
        rejects<UnsupportedDraw>([&]{map_rex_rgba8_texture_state(rgba_words,0,bad_rgba_view,raw_rgba,{});});
        // 4x MSAA RGBA8 strip resolve and RGBA16F-to-7e3 RGBA8 terminal resolve.
        std::vector<std::uint8_t> msaa4_strip(2*2*16,0);
        // Pixel (0,0): 4 samples with R={10,14,18,22}->avg=16, G=40, B=80, A=255 -> swapped [80,40,16,255]
        for(std::size_t s_idx=0;s_idx<4;++s_idx) {
            msaa4_strip[s_idx*4+0]=static_cast<std::uint8_t>(10+s_idx*4);
            msaa4_strip[s_idx*4+1]=40;
            msaa4_strip[s_idx*4+2]=80;
            msaa4_strip[s_idx*4+3]=255;
        }
        std::vector<std::uint8_t> resolved_rgba8(2*4*4,0);
        constexpr std::uint32_t kMsaaCopyDestInfo=2u|(6u<<7u)|(1u<<24u); // endian=2, format=k_8_8_8_8(6), swap=1
        resolve_rex_rgba8_msaa4_strip(
            RexMsaa4ResolveStrip{kMsaaCopyDestInfo,6u,2u,2u,2u,1u,msaa4_strip},
            4u,resolved_rgba8);
        check(resolved_rgba8[(1*2+0)*4+0]==80 && resolved_rgba8[(1*2+0)*4+1]==40 &&
              resolved_rgba8[(1*2+0)*4+2]==16 && resolved_rgba8[(1*2+0)*4+3]==255,
              "4x MSAA RGBA8 strip average or RB swap differs");
        rejects<UnsupportedDraw>([&]{resolve_rex_rgba8_msaa4_strip(
            RexMsaa4ResolveStrip{kMsaaCopyDestInfo,0u,2u,2u,2u,0u,msaa4_strip},4u,resolved_rgba8);});
        constexpr std::uint32_t kHdrCopyDestInfo=(6u<<7u)|(1u<<24u); // endian=0, format=k_8_8_8_8(6), swap=1
        // 1x1 pixel with R=0.25 (0x3400), G=0.5 (0x3800), B=1.0 (0x3c00), A=1.0 (0x3c00)
        const std::array<std::uint16_t,4> hdr_pixel{0x3400u,0x3800u,0x3c00u,0x3c00u};
        const auto hdr_resolved=resolve_rex_rgba16f_to_7e3_rgba8(
            RexHdr7e3ResolveSpec{1u,1u,kHdrCopyDestInfo,32u,true},hdr_pixel);
        check(hdr_resolved.linear_rgba8.size()==4 &&
              hdr_resolved.linear_rgba8[0]==255 && hdr_resolved.linear_rgba8[1]==128 &&
              hdr_resolved.linear_rgba8[2]==64 && hdr_resolved.linear_rgba8[3]==255 &&
              hdr_resolved.tiled_destination.size()==32u*32u*4u &&
              hdr_resolved.tiled_destination[0]==255 && hdr_resolved.tiled_destination[2]==64,
              "RGBA16F-to-7e3 RGBA8 resolve output differs");
        rejects<UnsupportedDraw>([&]{resolve_rex_rgba16f_to_7e3_rgba8(
            RexHdr7e3ResolveSpec{1u,1u,0u,32u,false},hdr_pixel);});
        std::cout<<"Synthetic SDK texture & sampler mapping: dimensions, pages, mip clamping, sampler filters/aniso, guest BC1 untiling, RGBA8 swizzle, 4x MSAA/HDR resolves, native view/BC1 payloads and unknown-state checks passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
