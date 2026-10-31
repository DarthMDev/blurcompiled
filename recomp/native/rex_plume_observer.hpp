#pragma once
#include "rex_plume_draw.hpp"
#include <rex/logging.h>
#include <algorithm>

// Development observer only. It never changes a render state, submits a Plume
// packet, reads GPU-private bytes or claims full eligibility from register state.
namespace blur::render::guest {
inline void observe_rex_plume_register_state(std::span<const std::uint32_t> words) {
    static thread_local std::uint64_t draws=0,core_matches=0,unsupported=0;
    ++draws;const auto previous=core_matches;
    try {
        const auto s=map_rex_ui_register_state(words,{});
        if(s.depth_test==Toggle::disabled && s.depth_write==Toggle::disabled && s.stencil==Toggle::disabled &&
           s.alpha_to_coverage==Toggle::disabled &&
           s.blending==Toggle::enabled && s.rgb_operation==BlendOperation::add && s.alpha_operation==BlendOperation::add &&
           ((s.cull==Cull::back && s.winding==Winding::clockwise &&
             s.source_rgb==BlendFactor::source_alpha && s.source_alpha==BlendFactor::source_alpha &&
             s.destination_rgb==BlendFactor::inverse_source_alpha && s.destination_alpha==BlendFactor::inverse_source_alpha &&
             s.color_write_mask==15) ||
            (s.cull==Cull::none &&
             s.source_rgb==BlendFactor::one && s.source_alpha==BlendFactor::one &&
             s.destination_rgb==BlendFactor::inverse_source_alpha && s.destination_alpha==BlendFactor::inverse_source_alpha &&
             s.color_write_mask==7)))++core_matches;
    }catch(const UnsupportedDraw&){++unsupported;}
     catch(const std::exception& error){REXLOG_ERROR("Plume state observer failed: {}",error.what());return;}
    if(draws==1 || (!previous && core_matches) || draws%65536==0)
        REXLOG_INFO("Plume state observer (no Plume rendering): draws={} register_core_matches={} unsupported={} packets_submitted=0 resources_unresolved=1",
                    draws,core_matches,unsupported);
}
// Called only after the existing SPIR-V path has updated CPU system constants
// and written its CPU-visible uniform storage. No private/GPU resource readback.
template<class Map>
inline void observe_rex_plume_shader_constants(std::span<const std::uint32_t> words,
    std::span<const std::uint8_t> vertex_binary,std::span<const std::uint8_t> pixel_binary,
    const Map& vertex_map,const Map& pixel_map,std::span<const std::uint8_t> system,
    const std::array<std::span<const std::uint8_t>,5>& uploaded) {
    static thread_local std::uint64_t draws=0,pairs=0,matches=0,mismatches=0,descriptor_matches=0,rejected=0;
    ++draws;const auto previous=matches;
    try {
        if(supported_ui_shader_pair(vertex_binary,pixel_binary)) {
            ++pairs;
            auto convert=[](const Map& source) {
                RexFloatConstantMap map;
                std::copy_n(source.float_bitmap,4,map.bitmap.begin());
                map.count=source.float_count;map.dynamic_addressing=source.float_dynamic_addressing;
                return map;
            };
            const auto blocks=pack_rex_ui_constants(words,convert(vertex_map),convert(pixel_map),system);
            bool equal=true;
            for(std::size_t i=0;i<blocks.size();++i)
                equal=equal && std::ranges::equal(blocks[i],uploaded[i]);
            if(equal)++matches;else ++mismatches;
            const auto prim=map_rex_ui_primitive_draw(words,{});
            auto state=map_rex_ui_register_state(words,prim.state);
            state=map_rex_ui_sampler_state(words,RexSamplerBinding{0},state);
            const auto vrange=rex_vertex_range(words,95);
            const auto trange=rex_ui_texture_guest_ranges(words,0);
            if(vrange.bytes==std::size_t(prim.guest_vertex_count)*24 && trange.base_bytes>0)
                ++descriptor_matches;
        } else if(supported_composite_shader_pair(vertex_binary,pixel_binary)) {
            ++pairs;
            auto convert=[](const Map& source) {
                RexFloatConstantMap map;
                std::copy_n(source.float_bitmap,4,map.bitmap.begin());
                map.count=source.float_count;map.dynamic_addressing=source.float_dynamic_addressing;
                return map;
            };
            const auto blocks=pack_rex_ui_constants(words,convert(vertex_map),convert(pixel_map),system,0,0);
            if(std::ranges::equal(blocks[0],uploaded[0]) &&
               std::ranges::equal(blocks[3],uploaded[3]) &&
               std::ranges::equal(blocks[4],uploaded[4]))++matches;
            else ++mismatches;
            const auto prim=map_rex_ui_primitive_draw(words,{});
            auto state=map_rex_ui_register_state(words,prim.state);
            state=map_rex_ui_sampler_state(words,RexSamplerBinding{0,RexSamplerFilter::use_fetch_const,
                RexSamplerFilter::use_fetch_const,RexSamplerFilter::use_fetch_const,
                RexAnisoFilter::use_fetch_const,RexAnisoFilter::disabled},state);
            state=map_rex_ui_sampler_state(words,RexSamplerBinding{1,RexSamplerFilter::use_fetch_const,
                RexSamplerFilter::use_fetch_const,RexSamplerFilter::use_fetch_const,
                RexAnisoFilter::use_fetch_const,RexAnisoFilter::disabled},state);
            const auto vrange=rex_vertex_range(words,95);
            if(prim.state.index_source==IndexSource::auto_vertex_list &&
               vrange.bytes==std::size_t(prim.guest_vertex_count)*16)
                ++descriptor_matches;
        }
    }catch(const std::exception&){++rejected;}
    if(draws==1 || (!previous && matches) || draws%65536==0)
        REXLOG_INFO("Plume constant observer (no Plume rendering): draws={} shader_pair_matches={} constant_matches={} constant_mismatches={} descriptor_matches={} rejected={} packets_submitted=0 resources_unresolved=1",
                    draws,pairs,matches,mismatches,descriptor_matches,rejected);
}
}
