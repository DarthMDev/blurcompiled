#include "rex_plume_state.hpp"
#include <rex/graphics/register_file.h>
#include <cstring>

namespace blur::render::guest {
namespace {
namespace gpu=rex::graphics;
namespace reg=gpu::reg;
namespace xenos=gpu::xenos;
static_assert(gpu::RegisterFile::kRegisterCount==rex_register_word_count);
template<class T> T get(std::span<const std::uint32_t> words) {
    static_assert(sizeof(T)==sizeof(std::uint32_t));
    T result;const auto value=words[T::register_index];std::memcpy(&result,&value,sizeof(result));return result;
}
Toggle toggle(bool enabled) {return enabled?Toggle::enabled:Toggle::disabled;}
BlendFactor factor(xenos::BlendFactor value) {
    if(value==xenos::BlendFactor::kZero)return BlendFactor::zero;
    if(value==xenos::BlendFactor::kOne)return BlendFactor::one;
    if(value==xenos::BlendFactor::kSrcAlpha)return BlendFactor::source_alpha;
    if(value==xenos::BlendFactor::kOneMinusSrcAlpha)return BlendFactor::inverse_source_alpha;
    return BlendFactor::unknown;
}
BlendOperation operation(xenos::BlendOp value) {
    return value==xenos::BlendOp::kAdd?BlendOperation::add:BlendOperation::unknown;
}
}
UiState map_rex_ui_register_state(std::span<const std::uint32_t> words,UiState s) {
    if(words.size()!=rex_register_word_count)throw std::invalid_argument("Expected the pinned SDK register file extent");
    const auto mode=get<reg::RB_MODECONTROL>(words);
    if(mode.edram_mode!=xenos::EdramMode::kColorDepth)throw UnsupportedDraw("Current EDRAM mode is not a color/depth draw");
    auto depth=get<reg::RB_DEPTHCONTROL>(words);
    // Match GetNormalizedDepthControl's redundant always-pass test removal,
    // then the backend's effective write state (z_enable && z_write_enable).
    if(depth.z_enable && !depth.z_write_enable && depth.zfunc==xenos::CompareFunction::kAlways)depth.z_enable=0;
    s.depth_test=toggle(depth.z_enable!=0);
    s.depth_write=toggle(depth.z_enable && depth.z_write_enable);
    s.stencil=toggle(depth.stencil_enable!=0);
    s.alpha_to_coverage=toggle(get<reg::RB_COLORCONTROL>(words).alpha_to_mask_enable!=0);
    const auto raster=get<reg::PA_SU_SC_MODE_CNTL>(words);
    if(!raster.cull_front && !raster.cull_back)s.cull=Cull::none;
    else if(!raster.cull_front && raster.cull_back)s.cull=Cull::back;
    else s.cull=Cull::unknown;
    s.winding=raster.face?Winding::clockwise:Winding::counter_clockwise;
    if(raster.poly_mode==xenos::PolygonModeEnable::kDualMode) {
        auto polygon=xenos::PolygonType::kTriangles;
        if(!raster.cull_front)polygon=std::min(polygon,raster.polymode_front_ptype);
        if(!raster.cull_back)polygon=std::min(polygon,raster.polymode_back_ptype);
        if(polygon!=xenos::PolygonType::kTriangles)s.topology=Topology::unknown;
    }
    // The bounded pass has no depth bias implementation. Reject any request
    // rather than applying a captured/default value to a new live draw.
    if(raster.poly_offset_front_enable || raster.poly_offset_back_enable || raster.poly_offset_para_enable)
        throw UnsupportedDraw("UI polygon offset is not supported");
    const auto mask=words[gpu::XE_GPU_REG_RB_COLOR_MASK]&0xffffu;
    if(mask&0xfff0u)throw UnsupportedDraw("Only RT0 color writes are supported");
    s.color_write_mask=mask&15u;
    const auto blend=get<reg::RB_BLENDCONTROL>(words);
    s.source_rgb=factor(blend.color_srcblend);s.destination_rgb=factor(blend.color_destblend);
    s.source_alpha=factor(blend.alpha_srcblend);s.destination_alpha=factor(blend.alpha_destblend);
    s.rgb_operation=operation(blend.color_comb_fcn);s.alpha_operation=operation(blend.alpha_comb_fcn);
    const bool copy=blend.color_srcblend==xenos::BlendFactor::kOne && blend.color_destblend==xenos::BlendFactor::kZero &&
        blend.color_comb_fcn==xenos::BlendOp::kAdd && blend.alpha_srcblend==xenos::BlendFactor::kOne &&
        blend.alpha_destblend==xenos::BlendFactor::kZero && blend.alpha_comb_fcn==xenos::BlendOp::kAdd;
    s.blending=toggle(!copy);
    return s;
}
}
