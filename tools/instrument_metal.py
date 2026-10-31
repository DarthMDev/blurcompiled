#!/usr/bin/env python3
"""Apply the bounded live Plume hooks to verified SDK source text.

Pure source transformation shared by clean builds and local diagnostic builds.
No compiler commands or prior object files are needed.
"""


def instrument(text):
    # The pinned Metal updater memset-clears this buffer but omits both
    # index limits. Complete them from current registers, matching Vulkan.
    index_anchor = "  consts.vertex_base_index = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;\n"
    if text.count(index_anchor) != 1:
        raise SystemExit("SDK SPIR-V vertex index constant context changed")
    index_patch = '''  static_assert(offsetof(SpirvShaderTranslator::SystemConstants, vertex_index_min) == 496);
  static_assert(offsetof(SpirvShaderTranslator::SystemConstants, vertex_index_max) == 500);
  blur::render::guest::complete_rex_spirv_index_limits(
      {regs.values, blur::render::guest::rex_register_word_count},
      {reinterpret_cast<uint8_t*>(&consts), sizeof(consts)});
'''
    text = text.replace(index_anchor, index_anchor + index_patch)
    # Observe after copy-mode handling, before shader analysis. The bounded
    # Plume draw hook below runs later, after SPIR-V constants are current.
    anchor="  // Vertex shader analysis — use Shader* base type so both MSC and\n"
    if text.count(anchor)!=1:raise SystemExit("SDK observer insertion context changed")
    text='#include "rex_plume_observer.hpp"\n#include "rex_plume_active.hpp"\n'+text.replace(anchor,
        "  blur::render::guest::observe_rex_plume_register_state({regs.values, blur::render::guest::rex_register_word_count});\n\n"+anchor)
    # Instrument both backend branches. Runtime selection must be observed,
    # never inferred from the existence of an IssueDrawMsl implementation.
    draw_anchor = "  // Blend constants (fixed-function, same as MSC path).\n"
    if text.count(draw_anchor) != 1:
        raise SystemExit("SDK SPIRV-Cross draw anchor context changed")
    draw_hook = '''  // Bounded Plume submission; retain the SDK draw until target/resource
  // synchronization and complete pass coverage have been verified.
  blur::render::guest::dispatch_rex_plume_draw(
      vertex_shader, pixel_shader,
      {regs.values, blur::render::guest::rex_register_word_count},
      msl_vertex_shader->constant_register_map(),
      msl_pixel_shader ? msl_pixel_shader->constant_register_map() : Shader::ConstantRegisterMap{},
      {reinterpret_cast<const uint8_t*>(&spirv_system_constants_), sizeof(spirv_system_constants_)},
      shared_memory_ ? shared_memory_->GetXboxRamBase() : nullptr,
      swap_count_, current_draw_index_, "spirvcross");

'''
    text = text.replace(draw_anchor, draw_hook + draw_anchor)

    msc_anchor = "    constexpr size_t kStageVertex = 0;\n"
    if text.count(msc_anchor) != 1:
        raise SystemExit("SDK MSC draw anchor context changed")
    msc_hook = '''    UpdateSpirvSystemConstantValues(
        primitive_processing_result, primitive_polygonal,
        primitive_processing_result.line_loop_closing_index,
        primitive_processing_result.host_shader_index_endian, viewport_info,
        used_texture_mask, depth_control, normalized_color_mask);
    blur::render::guest::dispatch_rex_plume_draw(
        vertex_shader, pixel_shader,
        {regs.values, blur::render::guest::rex_register_word_count},
        vertex_shader->constant_register_map(),
        pixel_shader ? pixel_shader->constant_register_map() : Shader::ConstantRegisterMap{},
        {reinterpret_cast<const uint8_t*>(&spirv_system_constants_), sizeof(spirv_system_constants_)},
        shared_memory_ ? shared_memory_->GetXboxRamBase() : nullptr,
        swap_count_, current_draw_index_, "msc");

'''
    text = text.replace(msc_anchor, msc_hook + msc_anchor)

    copy_anchor="  MarkResolvedMemory(written_address, written_length);\n"
    if text.count(copy_anchor)!=1:raise SystemExit("SDK copy anchor context changed")
    copy_hook='''  uint8_t* guest_ram_ptr = shared_memory_ ? const_cast<uint8_t*>(shared_memory_->GetXboxRamBase()) : nullptr;
  if (register_file_) {
    blur::render::guest::handle_rex_plume_copy(
        register_file_->values[XE_GPU_REG_RB_COPY_DEST_INFO],
        register_file_->values[XE_GPU_REG_RB_COPY_CONTROL] & 0x7,
        1280u, 720u, 720u, 0u, {},
        guest_ram_ptr, written_address);
  }
'''
    text=text.replace(copy_anchor,copy_anchor+copy_hook)

    swap_anchor="  ++swap_count_;\n"
    if text.count(swap_anchor)!=1:raise SystemExit("SDK swap anchor context changed")
    swap_hook="  ++swap_count_;\n  blur::render::guest::handle_rex_plume_swap();\n"
    text=text.replace(swap_anchor,swap_hook)

    present_anchor="      ui::metal_runtime::MetalPresenter* metal_presenter = presenter;\n"
    if text.count(present_anchor)!=1:raise SystemExit("SDK present anchor context changed")
    present_hook='''      if (!blur::render::guest::get_rex_plume_runtime().swap_source_linear.empty()) {
        const auto& pl_pixels = blur::render::guest::get_rex_plume_runtime().swap_source_linear;
        if (source_texture->width() >= 1280 && source_texture->height() >= 720 && pl_pixels.size() >= 1280 * 720 * 4) {
          source_texture->replaceRegion(MTL::Region::Make2D(0, 0, 1280, 720), 0, pl_pixels.data(), 1280 * 4);
        }
      }
      ui::metal_runtime::MetalPresenter* metal_presenter = presenter;
'''
    text=text.replace(present_anchor,present_hook)
    return text


if __name__ == "__main__":
    import argparse
    from pathlib import Path
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    text = instrument(args.source.read_text())
    if not args.output.exists() or args.output.read_text() != text:
        args.output.write_text(text)
